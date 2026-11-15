/**
 * @file ReloadLibrary_uTest.cpp
 * @brief RELOAD_LIBRARY on a running executive: the swapped-in component
 *        gets its log.
 *
 * Boots a real ApexExecutive with one linked ReloadTarget, places the
 * plugin build of the same component in the inactive library bank, locks
 * the component and issues RELOAD_LIBRARY. The commands run on the
 * executive's task thread, at the seam that thread offers once per cycle,
 * so the handler shares no state with another running thread; the test
 * reads the outcome from the files the executive writes. Nothing here
 * depends on timing: every wait is a poll for a state, with a bound that
 * only stops a broken run from hanging.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using executive::ApexExecutive;
using executive::test::ReloadTarget;
using system_core::system_component::CommandResult;
using Clock = std::chrono::steady_clock;

/// Bound on each poll; reached only when the state never arrives.
constexpr std::chrono::seconds POLL_LIMIT{20};

constexpr const char* LINKED = "linked into the test";
constexpr const char* LOADED = "loaded from a library";

/// Executive under test: one linked ReloadTarget, nothing scheduled.
class ReloadExecutive final : public ApexExecutive {
public:
  explicit ReloadExecutive(const std::filesystem::path& fsRoot)
      : ApexExecutive("ReloadLibrary_uTest",
                      {"--shutdown-mode", "cycle", "--shutdown-cycle", "1000000000", "--rt-mode",
                       "lag-tolerant", "--rt-max-lag", "1000000", "--skip-cleanup",
                       "--startup-delay", "0", "--verbosity", "0"},
                      fsRoot) {}

  ReloadTarget target{LINKED};

  [[nodiscard]] std::filesystem::path logDir() noexcept { return fileSystem().logDir(); }
  [[nodiscard]] std::filesystem::path inactiveLibDir() noexcept {
    return fileSystem().inactiveLibDir();
  }

  /**
   * @brief Run a command whose payload is the target's fullUid on the task
   *        thread, at its next cycle.
   * @return The handler's status, or NOT_RUN when no cycle took the
   *         command within POLL_LIMIT.
   */
  static constexpr int NOT_RUN = -1;
  [[nodiscard]] int runOnTaskThread(Opcode opcode) noexcept {
    result_.store(NOT_RUN, std::memory_order_release);
    pending_.store(static_cast<int>(opcode), std::memory_order_release);
    const auto DEADLINE = Clock::now() + POLL_LIMIT;
    while (result_.load(std::memory_order_acquire) == NOT_RUN && Clock::now() < DEADLINE) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return result_.load(std::memory_order_acquire);
  }

protected:
  [[nodiscard]] bool registerComponents() noexcept override {
    return registerComponent(&target, fileSystem().logDir());
  }

  void onWaitDeadlineSelected(std::uint64_t, std::uint64_t) noexcept override {
    const int OPCODE = pending_.exchange(NOT_RUN, std::memory_order_acq_rel);
    if (OPCODE == NOT_RUN) {
      return;
    }
    const std::uint32_t UID = target.fullUid();
    std::array<std::uint8_t, sizeof(UID)> payload{};
    std::memcpy(payload.data(), &UID, sizeof(UID));
    std::vector<std::uint8_t> response;
    result_.store(handleCommand(static_cast<std::uint16_t>(OPCODE),
                                apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                                response),
                  std::memory_order_release);
  }

private:
  std::atomic<int> pending_{NOT_RUN};
  std::atomic<int> result_{NOT_RUN};
};

std::string slurp(const std::filesystem::path& path) {
  std::ifstream ifs(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

/// Poll until the file holds NEEDLE; false when POLL_LIMIT passes first.
bool waitForLine(const std::filesystem::path& path, const std::string& needle) {
  const auto DEADLINE = Clock::now() + POLL_LIMIT;
  while (Clock::now() < DEADLINE) {
    if (slurp(path).find(needle) != std::string::npos) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

class ReloadLibraryTest : public ::testing::Test {
protected:
  void SetUp() override {
    fsRoot_ = std::filesystem::path(::testing::TempDir()) /
              ("exec_reload_" +
               std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(fsRoot_);
    std::filesystem::create_directories(fsRoot_);
    exec_ = std::make_unique<ReloadExecutive>(fsRoot_);
    ASSERT_EQ(exec_->init(), 0) << "executive init failed";
    runner_ = std::thread([this]() { (void)exec_->run(); });

    // Running: the clock has ticked, so registration is behind us.
    const auto DEADLINE = Clock::now() + POLL_LIMIT;
    while (exec_->getHealthPacket().clockCycles < 2u && Clock::now() < DEADLINE) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_GE(exec_->getHealthPacket().clockCycles, 2u) << "the executive did not start";
  }

  void TearDown() override {
    if (runner_.joinable()) {
      exec_->shutdown();
      runner_.join();
    }
    exec_.reset();
    std::error_code ec;
    std::filesystem::remove_all(fsRoot_, ec);
  }

  std::filesystem::path fsRoot_;
  std::unique_ptr<ReloadExecutive> exec_;
  std::thread runner_;
};

} // namespace

/** @test The component RELOAD_LIBRARY swaps in writes to the component's log file. */
TEST_F(ReloadLibraryTest, SwappedInComponentHasItsLog) {
  const auto LOG_FILE = exec_->logDir() / "models" / "ReloadTarget_0.log";
  const auto SWAP_LOG_FILE = exec_->logDir() / "core" / "swap.log";
  ASSERT_TRUE(waitForLine(LOG_FILE, ReloadTarget::startupLine(LINKED)))
      << "the linked instance's start-up line is not in " << LOG_FILE;

  // The replacement, under the name the executive looks for in the inactive bank.
  const auto PLUGIN = std::filesystem::path(APEX_TEST_PLUGIN_DIR) / "ExecutiveTestPlugin_Reload.so";
  ASSERT_TRUE(std::filesystem::exists(PLUGIN)) << PLUGIN;
  std::filesystem::create_directories(exec_->inactiveLibDir());
  std::filesystem::copy_file(PLUGIN, exec_->inactiveLibDir() / "ReloadTarget_0.so");

  ASSERT_EQ(exec_->runOnTaskThread(ApexExecutive::Opcode::CMD_LOCK_COMPONENT),
            static_cast<int>(CommandResult::SUCCESS));
  ASSERT_EQ(exec_->runOnTaskThread(ApexExecutive::Opcode::RELOAD_LIBRARY),
            static_cast<int>(CommandResult::SUCCESS))
      << slurp(SWAP_LOG_FILE);

  EXPECT_TRUE(waitForLine(LOG_FILE, ReloadTarget::startupLine(LOADED)))
      << "the swapped-in instance wrote no start-up line to " << LOG_FILE << ":\n"
      << slurp(LOG_FILE);

  // The swap log names the log the new instance was given and the archive.
  const std::string SWAP_LOG = slurp(SWAP_LOG_FILE);
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_LOG_ATTACHED: uid=0x00FB00 log=" + LOG_FILE.string()),
            std::string::npos)
      << SWAP_LOG;
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_LOG_ARCHIVED: uid=0x00FB00"), std::string::npos);
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_OK: uid=0x00FB00"), std::string::npos);

  // One archive of the component's log, holding the linked instance's line.
  std::vector<std::filesystem::path> archives;
  for (const auto& entry : std::filesystem::directory_iterator(fsRoot_ / "swap_history")) {
    archives.push_back(entry.path());
  }
  ASSERT_EQ(archives.size(), 1u);
  EXPECT_NE(slurp(archives.front() / "ReloadTarget_0.log").find(ReloadTarget::startupLine(LINKED)),
            std::string::npos);
}
