/**
 * @file ReloadLibrary_uTest.cpp
 * @brief RELOAD_LIBRARY on a running executive: the swapped-in component
 *        gets its log, and the swap log names that log.
 *
 * Boots a real ApexExecutive with one linked ReloadTarget, places a
 * plugin library with the same component id in the inactive library bank,
 * locks the component and issues RELOAD_LIBRARY. The commands run on the
 * executive's task thread, at the seam that thread offers once per cycle,
 * so the handler shares no state with another running thread; the test
 * reads the outcome from the files the executive writes. Nothing here
 * depends on timing: every wait is a poll for a state, with a bound that
 * only stops a broken run from hanging.
 *
 * Each case owns what its executive would otherwise share with another
 * run of the same case: a directory created for it alone, and a TCP port
 * for the interface that the kernel reported free, handed over in the
 * interface's parameter file.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveState.hpp"
#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"
#include "src/system/core/components/interface/apex/inc/ApexInterfaceTunables.hpp"
#include "src/system/core/components/interface/apex/inc/InterfaceBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using executive::ApexExecutive;
using executive::test::ReloadTarget;
using executive::test::RenamedReloadTarget;
using system_core::system_component::CommandResult;
using Clock = std::chrono::steady_clock;

/// Bound on each poll; reached only when the state never arrives.
constexpr std::chrono::seconds POLL_LIMIT{20};

/// Starts tried before a case gives up; each takes a new port.
constexpr int START_ATTEMPTS = 5;

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
  [[nodiscard]] std::filesystem::path tprmDir() noexcept { return fileSystem().tprmDir(); }
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

/// A TCP port on the loopback address that no socket held a moment ago; 0 when none was found.
std::uint16_t pickFreePort() {
  const int FD = ::socket(AF_INET, SOCK_STREAM, 0);
  if (FD < 0) {
    return 0;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t len = sizeof(addr);
  std::uint16_t port = 0;
  if (::bind(FD, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0 &&
      ::getsockname(FD, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
    port = ntohs(addr.sin_port);
  }
  ::close(FD);
  return port;
}

/// Write the interface's parameter file into DIR: the defaults, served on PORT.
bool writeInterfacePort(const std::filesystem::path& dir, std::uint16_t port) {
  namespace sc = system_core::system_component;
  system_core::interface::ApexInterfaceTunables tunables{};
  tunables.port = port;

  sc::TprmPayloadHeader header{};
  header.magic = sc::TPRM_PAYLOAD_MAGIC;
  header.version = sc::TPRM_PAYLOAD_VERSION;
  header.fullUid = static_cast<std::uint32_t>(system_core::interface::InterfaceBase::COMPONENT_ID)
                   << 8;
  header.payloadSize = sizeof(tunables);
  header.payloadCrc =
      sc::tprmCrc32(reinterpret_cast<const std::uint8_t*>(&tunables), sizeof(tunables));

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(dir / sc::SystemComponentBase::tprmFilename(header.fullUid),
                    std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(reinterpret_cast<const char*>(&tunables), sizeof(tunables));
  out.flush();
  return out.good();
}

class ReloadLibraryTest : public ::testing::Test {
protected:
  void SetUp() override {
    // A directory only this run owns: mkdtemp() picks a free name and
    // creates it (mode 0700) in one step, so a second run of this case
    // gets another directory.
    std::string pattern =
        (std::filesystem::path(::testing::TempDir()) / "exec_reload_XXXXXX").string();
    const char* MADE = ::mkdtemp(pattern.data());
    ASSERT_NE(MADE, nullptr) << "no private directory under " << ::testing::TempDir();
    ownedDir_ = MADE;

    // The port was free when the kernel named it, and another process can
    // take it before the interface binds. That start is held at cycle 0;
    // it is stopped and the next one takes a new port and a new tree.
    for (int attempt = 0; attempt < START_ATTEMPTS; ++attempt) {
      fsRoot_ = ownedDir_ / ("boot_" + std::to_string(attempt));
      bool running = false;
      ASSERT_NO_FATAL_FAILURE(startExecutive(running));
      if (running) {
        return;
      }
      stopExecutive();
    }
    FAIL() << "the executive did not start in " << START_ATTEMPTS << " attempts; last system log:\n"
           << slurp(fsRoot_ / "system.log");
  }

  void TearDown() override {
    stopExecutive();
    // Only the directory SetUp() created, and only now the executive is gone.
    if (!ownedDir_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(ownedDir_, ec);
    }
  }

  /// Boot an executive in fsRoot_ on a port of its own. RUNNING is false when it is held.
  void startExecutive(bool& running) {
    const std::uint16_t PORT = pickFreePort();
    ASSERT_NE(PORT, 0u) << "no free TCP port on the loopback address";
    std::filesystem::create_directories(fsRoot_);
    exec_ = std::make_unique<ReloadExecutive>(fsRoot_);
    ASSERT_TRUE(writeInterfacePort(exec_->tprmDir(), PORT)) << exec_->tprmDir();
    ASSERT_EQ(exec_->init(), 0) << "executive init failed";
    runner_ = std::thread([this]() { (void)exec_->run(); });

    // Running: the clock has ticked, so registration is behind us. Held:
    // the clock is paused before its first cycle.
    const auto DEADLINE = Clock::now() + POLL_LIMIT;
    while (Clock::now() < DEADLINE) {
      const auto HEALTH = exec_->getHealthPacket();
      if (HEALTH.clockCycles >= 2u) {
        // The interface serves the port this case chose, not the default.
        const auto INTERFACE_LOG = exec_->logDir() / "core" / "interface.log";
        ASSERT_TRUE(waitForLine(INTERFACE_LOG, "port=" + std::to_string(PORT) + "\n"))
            << "the interface did not take port " << PORT << ":\n"
            << slurp(INTERFACE_LOG);
        running = true;
        return;
      }
      if ((HEALTH.flags & executive::ExecutiveHealthPacket::FLAG_PAUSED) != 0u) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void stopExecutive() {
    if (runner_.joinable()) {
      exec_->shutdown();
      runner_.join();
    }
    exec_.reset();
  }

  /// The log file of the model named NAME, instance 0.
  [[nodiscard]] std::filesystem::path modelLog(const char* name) const {
    return exec_->logDir() / "models" / (std::string(name) + "_0.log");
  }

  [[nodiscard]] std::filesystem::path swapLog() const {
    return exec_->logDir() / "core" / "swap.log";
  }

  /**
   * @brief Swap PLUGIN_FILE in for the linked instance: wait for that
   *        instance's start-up line, bank the library under the name the
   *        executive looks for, lock the component and reload it.
   */
  void swapIn(const char* pluginFile) {
    const auto LINKED_LOG = modelLog(ReloadTarget::COMPONENT_NAME);
    ASSERT_TRUE(waitForLine(LINKED_LOG, ReloadTarget::startupLine(LINKED)))
        << "the linked instance's start-up line is not in " << LINKED_LOG;

    const auto PLUGIN = std::filesystem::path(APEX_TEST_PLUGIN_DIR) / pluginFile;
    ASSERT_TRUE(std::filesystem::exists(PLUGIN)) << PLUGIN;
    std::filesystem::create_directories(exec_->inactiveLibDir());
    std::filesystem::copy_file(PLUGIN, exec_->inactiveLibDir() / "ReloadTarget_0.so");

    ASSERT_EQ(exec_->runOnTaskThread(ApexExecutive::Opcode::CMD_LOCK_COMPONENT),
              static_cast<int>(CommandResult::SUCCESS));
    ASSERT_EQ(exec_->runOnTaskThread(ApexExecutive::Opcode::RELOAD_LIBRARY),
              static_cast<int>(CommandResult::SUCCESS))
        << slurp(swapLog());
  }

  /// The archived copies of the linked instance's log, one per archive made.
  [[nodiscard]] std::vector<std::filesystem::path> archivedLogs() const {
    std::vector<std::filesystem::path> logs;
    for (const auto& entry : std::filesystem::directory_iterator(fsRoot_ / "swap_history")) {
      logs.push_back(entry.path() / "ReloadTarget_0.log");
    }
    return logs;
  }

  std::filesystem::path ownedDir_; ///< Empty until SetUp() has created the directory.
  std::filesystem::path fsRoot_;   ///< The running executive's tree, under ownedDir_.
  std::unique_ptr<ReloadExecutive> exec_;
  std::thread runner_;
};

} // namespace

/** @test The component RELOAD_LIBRARY swaps in writes to the component's log file. */
TEST_F(ReloadLibraryTest, SwappedInComponentHasItsLog) {
  ASSERT_NO_FATAL_FAILURE(swapIn("ExecutiveTestPlugin_Reload.so"));

  const auto LOG_FILE = modelLog(ReloadTarget::COMPONENT_NAME);
  EXPECT_TRUE(waitForLine(LOG_FILE, ReloadTarget::startupLine(LOADED)))
      << "the swapped-in instance wrote no start-up line to " << LOG_FILE << ":\n"
      << slurp(LOG_FILE);

  // The swap log names the log the new instance was given and the archive.
  const std::string SWAP_LOG = slurp(swapLog());
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_LOG_ATTACHED: uid=0x00FB00 log=" + LOG_FILE.string()),
            std::string::npos)
      << SWAP_LOG;
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_LOG_ARCHIVED: uid=0x00FB00"), std::string::npos);
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_OK: uid=0x00FB00"), std::string::npos);

  // One archive of the component's log, holding the linked instance's line.
  const auto ARCHIVES = archivedLogs();
  ASSERT_EQ(ARCHIVES.size(), 1u);
  EXPECT_NE(slurp(ARCHIVES.front()).find(ReloadTarget::startupLine(LINKED)), std::string::npos);
}

/**
 * @test A replacement with the same id and another name writes to the log
 *       its own name selects, and the swap log names that file.
 */
TEST_F(ReloadLibraryTest, RenamedReplacementIsReportedWithItsOwnLog) {
  ASSERT_NO_FATAL_FAILURE(swapIn("ExecutiveTestPlugin_RenamedReload.so"));

  const auto OLD_LOG_FILE = modelLog(ReloadTarget::COMPONENT_NAME);
  const auto NEW_LOG_FILE = modelLog(RenamedReloadTarget::COMPONENT_NAME);
  EXPECT_TRUE(waitForLine(NEW_LOG_FILE, ReloadTarget::startupLine(LOADED)))
      << "the swapped-in instance wrote no start-up line to " << NEW_LOG_FILE;

  const std::string SWAP_LOG = slurp(swapLog());
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_LOG_ATTACHED: uid=0x00FB00 log=" + NEW_LOG_FILE.string()),
            std::string::npos)
      << SWAP_LOG;
  EXPECT_EQ(SWAP_LOG.find("log=" + OLD_LOG_FILE.string()), std::string::npos) << SWAP_LOG;

  // The archive is of the file the linked instance wrote.
  const auto ARCHIVES = archivedLogs();
  ASSERT_EQ(ARCHIVES.size(), 1u);
  EXPECT_NE(slurp(ARCHIVES.front()).find(ReloadTarget::startupLine(LINKED)), std::string::npos);
}
