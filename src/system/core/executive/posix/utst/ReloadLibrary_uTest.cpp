/**
 * @file ReloadLibrary_uTest.cpp
 * @brief RELOAD_LIBRARY on a running executive: the swapped-in component
 *        gets its log, and the swap log names that log.
 *
 * Boots a real ApexExecutive with one linked ReloadTarget, places a
 * plugin library with the same component id in the inactive library bank,
 * locks the component and issues RELOAD_LIBRARY. The first two cases send
 * the reload the way a sequence does: as a command action the action
 * engine dispatches on the task thread, which the executive hands to its
 * external I/O thread. The test reads the outcome from the files the
 * executive writes. Nothing here depends on timing: every wait is a poll
 * for a state, with a bound that only stops a broken run from hanging.
 *
 * Each case owns what its executive would otherwise share with another
 * run of the same case: a directory created for it alone, and a TCP port
 * for the interface that the kernel reported free, handed over in the
 * interface's parameter file.
 *
 * The route cases send the commands the way ground does: framed, over the
 * interface's TCP port, so the handler runs on the external I/O thread
 * while the task thread dispatches the component's scheduled task. They
 * hold a run of that task inside the old instance across the reload and
 * require the swap to wait for it, the old instance to outlive it, and
 * the replacement to run afterwards. They read the component's output
 * block back through INSPECT, which must show the instance that runs, and
 * they offer replacements the swap must refuse whole.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveState.hpp"
#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveStatus.hpp"
#include "src/system/core/components/action/apex/inc/ActionInterface.hpp"
#include "src/system/core/components/action/apex/inc/DataAction.hpp"
#include "src/system/core/components/interface/apex/inc/ApexInterfaceTunables.hpp"
#include "src/system/core/components/interface/apex/inc/InterfaceBase.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
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
using executive::ExecCommandResult;
using executive::test::ReloadProbe;
using executive::test::ReloadTarget;
using executive::test::RenamedReloadTarget;
namespace aproto = system_core::protocols::aproto;
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
  [[nodiscard]] std::uint32_t targetUid() const noexcept { return target.fullUid(); }
  [[nodiscard]] std::filesystem::path inactiveLibDir() noexcept {
    return fileSystem().inactiveLibDir();
  }

  static constexpr int NOT_RUN = -1;

  /**
   * @brief Run a command whose payload is the target's fullUid on the task
   *        thread, at its next cycle, by calling the executive's handler.
   * @return The handler's status, or NOT_RUN when no cycle took the
   *         command within POLL_LIMIT.
   */
  [[nodiscard]] int runOnTaskThread(Opcode opcode) noexcept { return post(opcode, false); }

  /**
   * @brief Queue a command action for the executive, payload the target's
   *        fullUid, on the task thread at its next cycle. The action
   *        engine dispatches it in that cycle's action phase.
   * @return 0 when the action was queued, 1 when the queue refused it,
   *         NOT_RUN when no cycle took the request within POLL_LIMIT.
   */
  [[nodiscard]] int queueCommandAction(Opcode opcode) noexcept { return post(opcode, true); }

  /// Hash of the task thread's id, as ReloadProbe::thisThread() gives it;
  /// 0 before its first cycle.
  [[nodiscard]] std::size_t taskThread() const noexcept {
    return taskThread_.load(std::memory_order_acquire);
  }

protected:
  [[nodiscard]] bool registerComponents() noexcept override {
    return registerComponent(&target, fileSystem().logDir());
  }

  void onWaitDeadlineSelected(std::uint64_t, std::uint64_t) noexcept override {
    taskThread_.store(ReloadProbe::thisThread(), std::memory_order_release);
    const int OPCODE = pending_.exchange(NOT_RUN, std::memory_order_acq_rel);
    if (OPCODE == NOT_RUN) {
      return;
    }
    const std::uint32_t UID = target.fullUid();
    std::array<std::uint8_t, sizeof(UID)> payload{};
    std::memcpy(payload.data(), &UID, sizeof(UID));

    if (viaAction_.load(std::memory_order_acquire)) {
      system_core::data::DataAction action{};
      system_core::data::initCommandAction(action, 0U, system_core::data::ActionTrigger::IMMEDIATE,
                                           0U, static_cast<std::uint16_t>(OPCODE), payload.data(),
                                           static_cast<std::uint8_t>(payload.size()));
      const auto QUEUED = system_core::data::queueAction(actionComponent().iface(), action);
      result_.store(QUEUED == system_core::data::ActionStatus::SUCCESS ? 0 : 1,
                    std::memory_order_release);
      return;
    }

    std::vector<std::uint8_t> response;
    result_.store(handleCommand(static_cast<std::uint16_t>(OPCODE),
                                apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()},
                                response),
                  std::memory_order_release);
  }

private:
  [[nodiscard]] int post(Opcode opcode, bool viaAction) noexcept {
    result_.store(NOT_RUN, std::memory_order_release);
    viaAction_.store(viaAction, std::memory_order_release);
    pending_.store(static_cast<int>(opcode), std::memory_order_release);
    const auto DEADLINE = Clock::now() + POLL_LIMIT;
    while (result_.load(std::memory_order_acquire) == NOT_RUN && Clock::now() < DEADLINE) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return result_.load(std::memory_order_acquire);
  }

  std::atomic<int> pending_{NOT_RUN};
  std::atomic<bool> viaAction_{false};
  std::atomic<int> result_{NOT_RUN};
  std::atomic<std::size_t> taskThread_{0};
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

/// Write the parameter file FULL_UID loads from DIR: the prelude, then BODY.
bool writeTprm(const std::filesystem::path& dir, std::uint32_t fullUid, const std::uint8_t* body,
               std::size_t bodyBytes) {
  namespace sc = system_core::system_component;
  sc::TprmPayloadHeader header{};
  header.magic = sc::TPRM_PAYLOAD_MAGIC;
  header.version = sc::TPRM_PAYLOAD_VERSION;
  header.fullUid = fullUid;
  header.payloadSize = bodyBytes;
  header.payloadCrc = sc::tprmCrc32(body, bodyBytes);

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(dir / sc::SystemComponentBase::tprmFilename(fullUid),
                    std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(reinterpret_cast<const char*>(body), static_cast<std::streamsize>(bodyBytes));
  out.flush();
  return out.good();
}

/// Write the interface's parameter file into DIR: the defaults, served on PORT.
bool writeInterfacePort(const std::filesystem::path& dir, std::uint16_t port) {
  system_core::interface::ApexInterfaceTunables tunables{};
  tunables.port = port;
  const std::uint32_t INTERFACE_UID =
      static_cast<std::uint32_t>(system_core::interface::InterfaceBase::COMPONENT_ID) << 8;
  return writeTprm(dir, INTERFACE_UID, reinterpret_cast<const std::uint8_t*>(&tunables),
                   sizeof(tunables));
}

/// Write the scheduler's parameter file into DIR: the target's step task, in its
/// sequence group, on every cycle.
bool writeStepSchedule(const std::filesystem::path& dir) {
  namespace sch = system_core::scheduler;
  constexpr std::uint32_t SCHEDULER_UID = 1U << 8;
  constexpr std::uint32_t TARGET_UID = static_cast<std::uint32_t>(ReloadTarget::COMPONENT_ID) << 8;

  sch::SchedulerTprmHeader header{};
  header.numPools = 1;
  header.workersPerPool = 2;
  header.numTasks = 1;
  sch::SchedulerTaskEntry step{};
  step.fullUid = TARGET_UID;
  step.taskUid = ReloadTarget::STEP_TASK_UID;
  step.poolIndex = 0;
  step.freqN = executive::DEFAULT_CLOCK_FREQUENCY;
  step.freqD = 1;
  step.offset = 0;
  step.priority = 0;
  step.sequenceGroup = ReloadTarget::STEP_GROUP;
  step.sequencePhase = 1;

  std::array<std::uint8_t, sizeof(header) + sizeof(step)> image{};
  std::memcpy(image.data(), &header, sizeof(header));
  std::memcpy(image.data() + sizeof(header), &step, sizeof(step));
  return writeTprm(dir, SCHEDULER_UID, image.data(), image.size());
}

/**
 * @brief A ground-side client: framed commands to the executive over the
 *        interface's TCP port, and their replies.
 */
class CommandClient {
public:
  [[nodiscard]] bool connect(std::uint16_t port) {
    client_ =
        std::make_unique<apex::protocols::tcp::TcpSocketClient>("127.0.0.1", std::to_string(port));
    std::string error;
    return client_->init(2000, error) == apex::protocols::tcp::TCP_CLIENT_SUCCESS;
  }

  /// Send OPCODE to the executive with UID as its payload.
  /// @return the command's sequence, 0 on failure.
  [[nodiscard]] std::uint16_t send(ApexExecutive::Opcode opcode, std::uint32_t uid) {
    std::array<std::uint8_t, sizeof(uid)> payload{};
    std::memcpy(payload.data(), &uid, sizeof(uid));
    return send(opcode, payload.data(), payload.size());
  }

  /// Send OPCODE to the executive with PAYLOAD.
  /// @return the command's sequence, 0 on failure.
  [[nodiscard]] std::uint16_t send(ApexExecutive::Opcode opcode, const std::uint8_t* payload,
                                   std::size_t payloadBytes) {
    constexpr std::size_t PAYLOAD_MAX = 32;
    if (payloadBytes > PAYLOAD_MAX) {
      return 0;
    }
    const std::uint16_t SEQUENCE = ++sequence_;
    const aproto::AprotoHeader HEADER =
        aproto::buildHeader(0U, static_cast<std::uint16_t>(opcode), SEQUENCE,
                            static_cast<std::uint16_t>(payloadBytes), false, true, false);
    std::array<std::uint8_t, aproto::APROTO_HEADER_SIZE + PAYLOAD_MAX> packet{};
    std::size_t packetBytes = 0;
    if (aproto::encodePacket(HEADER, {payload, payloadBytes}, {packet.data(), packet.size()},
                             packetBytes) != aproto::Status::SUCCESS) {
      return 0;
    }
    std::array<std::uint8_t, 2 * sizeof(packet) + 8> frame{};
    const auto ENCODED =
        apex::protocols::slip::encode({packet.data(), packetBytes}, frame.data(), frame.size());
    std::string error;
    const ssize_t SENT = client_->write(
        apex::compat::bytes_span{frame.data(), static_cast<std::size_t>(ENCODED.bytesProduced)},
        2000, error);
    return SENT == static_cast<ssize_t>(ENCODED.bytesProduced) ? SEQUENCE : 0;
  }

  /**
   * @brief Wait for the reply to the command numbered SEQUENCE.
   * @return true with its status when the reply arrived within LIMIT.
   */
  [[nodiscard]] bool reply(std::uint16_t sequence, std::chrono::milliseconds limit,
                           std::uint8_t& status, std::vector<std::uint8_t>* extra = nullptr) {
    const auto DEADLINE = Clock::now() + limit;
    for (;;) {
      if (takeReply(sequence, status, extra)) {
        return true;
      }
      if (Clock::now() >= DEADLINE) {
        return false;
      }
      std::array<std::uint8_t, 1024> chunk{};
      std::string error;
      const ssize_t READ = client_->read(chunk, 10, error);
      if (READ > 0) {
        received_.insert(received_.end(), chunk.begin(), chunk.begin() + READ);
      }
    }
  }

private:
  /// Decode the frames received so far; true when one is the reply to SEQUENCE.
  bool takeReply(std::uint16_t sequence, std::uint8_t& status, std::vector<std::uint8_t>* extra) {
    const apex::protocols::slip::DecodeConfig CONFIG{};
    std::array<std::uint8_t, 2048> decoded{};
    while (!received_.empty()) {
      const auto RESULT = apex::protocols::slip::decodeChunk(
          decode_, CONFIG, apex::compat::bytes_span{received_.data(), received_.size()},
          decoded.data(), decoded.size());
      const std::size_t CONSUMED =
          std::min(static_cast<std::size_t>(RESULT.bytesConsumed), received_.size());
      received_.erase(received_.begin(), received_.begin() + static_cast<std::ptrdiff_t>(CONSUMED));
      if (!RESULT.frameCompleted) {
        if (CONSUMED == 0) {
          break;
        }
        continue;
      }
      decode_ = apex::protocols::slip::DecodeState{};

      aproto::PacketView view{};
      if (aproto::createPacketView(
              apex::compat::rospan<std::uint8_t>{decoded.data(),
                                                 static_cast<std::size_t>(RESULT.bytesProduced)},
              view) != aproto::Status::SUCCESS ||
          !view.isResponse() || view.payload.size() < aproto::APROTO_ACK_PAYLOAD_SIZE) {
        continue;
      }
      aproto::AckPayload ack{};
      std::memcpy(&ack, view.payload.data(), sizeof(ack));
      if (ack.cmdSequence == sequence) {
        status = ack.status;
        if (extra != nullptr) {
          extra->assign(view.payload.begin() + aproto::APROTO_ACK_PAYLOAD_SIZE, view.payload.end());
        }
        return true;
      }
    }
    return false;
  }

  std::unique_ptr<apex::protocols::tcp::TcpSocketClient> client_;
  std::vector<std::uint8_t> received_;
  apex::protocols::slip::DecodeState decode_{};
  std::uint16_t sequence_{0};
};

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

    // Every instance of the component, linked or loaded, reports to this probe.
    probe_.publish();

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
    // A run still held would keep its worker, and the shutdown, waiting.
    probe_.hold.store(false, std::memory_order_release);
    stopExecutive();
    ReloadProbe::withdraw();
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
    if (scheduleStep_) {
      ASSERT_TRUE(writeStepSchedule(exec_->tprmDir())) << exec_->tprmDir();
    }
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
        port_ = PORT;
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
    // The reload as a sequence sends it: a command action, dispatched by
    // the action engine on the task thread. The executive runs it on its
    // external I/O thread, so the swap is over when the swap log says so.
    ASSERT_EQ(exec_->queueCommandAction(ApexExecutive::Opcode::RELOAD_LIBRARY), 0);
    ASSERT_TRUE(waitForLine(swapLog(), "RELOAD_LIBRARY_OK")) << slurp(swapLog());
  }

  /// The archived copies of the linked instance's log, one per archive made.
  [[nodiscard]] std::vector<std::filesystem::path> archivedLogs() const {
    std::vector<std::filesystem::path> logs;
    for (const auto& entry : std::filesystem::directory_iterator(fsRoot_ / "swap_history")) {
      logs.push_back(entry.path() / "ReloadTarget_0.log");
    }
    return logs;
  }

  /// Copy PLUGIN_FILE into the inactive bank under the name the executive looks for.
  void bankPlugin(const char* pluginFile) {
    const auto PLUGIN = std::filesystem::path(APEX_TEST_PLUGIN_DIR) / pluginFile;
    ASSERT_TRUE(std::filesystem::exists(PLUGIN)) << PLUGIN;
    std::filesystem::create_directories(exec_->inactiveLibDir());
    std::filesystem::copy_file(PLUGIN, exec_->inactiveLibDir() / "ReloadTarget_0.so",
                               std::filesystem::copy_options::overwrite_existing);
  }

  /// Poll until CONDITION holds; false when POLL_LIMIT passes first.
  template <typename TCondition> [[nodiscard]] static bool waitFor(TCondition&& condition) {
    const auto DEADLINE = Clock::now() + POLL_LIMIT;
    while (Clock::now() < DEADLINE) {
      if (condition()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
  }

  /// Let COUNT clock cycles pass; false when they do not within POLL_LIMIT.
  [[nodiscard]] bool waitCycles(std::uint64_t count) {
    const std::uint64_t TARGET = exec_->getHealthPacket().clockCycles + count;
    return waitFor([&]() { return exec_->getHealthPacket().clockCycles >= TARGET; });
  }

  bool scheduleStep_{false};       ///< Set before SetUp(): schedule the target's task.
  ReloadProbe probe_;              ///< What the component's instances report.
  std::uint16_t port_{0};          ///< The interface's TCP port.
  std::filesystem::path ownedDir_; ///< Empty until SetUp() has created the directory.
  std::filesystem::path fsRoot_;   ///< The running executive's tree, under ownedDir_.
  std::unique_ptr<ReloadExecutive> exec_;
  std::thread runner_;
};

/// The route cases: the target's task is scheduled on every cycle, and
/// commands go to the executive over its TCP port.
class ReloadRouteTest : public ReloadLibraryTest {
protected:
  ReloadRouteTest() { scheduleStep_ = true; }

  void SetUp() override {
    ReloadLibraryTest::SetUp();
    if (HasFatalFailure()) {
      return;
    }
    ASSERT_TRUE(ground_.connect(port_)) << "no connection to port " << port_;
    // The linked instance is number 1 on the probe; its task is running.
    ASSERT_TRUE(waitFor([&]() { return probe_.finished[1].load() > 0U; }))
        << "the linked instance's task is not being dispatched";
  }

  /// Hold a task run of instance NUMBER inside the task.
  void holdARunOf(std::size_t number) {
    probe_.hold.store(true, std::memory_order_release);
    ASSERT_TRUE(waitFor([&]() { return probe_.inTask(number); }))
        << "no run of instance " << number << " entered the task";
  }

  void release() { probe_.hold.store(false, std::memory_order_release); }

  /// Lock the target over the wire and require the reply.
  void lockTarget() {
    const std::uint16_t SEQUENCE =
        ground_.send(ApexExecutive::Opcode::CMD_LOCK_COMPONENT, exec_->targetUid());
    ASSERT_NE(SEQUENCE, 0u);
    std::uint8_t status = 0xFF;
    ASSERT_TRUE(ground_.reply(SEQUENCE, POLL_LIMIT, status)) << "no reply to the lock";
    ASSERT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS));
  }

  /// Send RELOAD_LIBRARY for the target. @return the command's sequence.
  [[nodiscard]] std::uint16_t sendReload() {
    return ground_.send(ApexExecutive::Opcode::RELOAD_LIBRARY, exec_->targetUid());
  }

  /**
   * @brief Read the target's output block through INSPECT over the wire.
   * @return The instance number the block holds, or 0 when the read failed.
   */
  [[nodiscard]] std::uint32_t inspectedInstance() {
    const std::uint32_t UID = exec_->targetUid();
    std::array<std::uint8_t, 9> payload{}; // uid, category, offset, length (0: the whole block)
    std::memcpy(payload.data(), &UID, sizeof(UID));
    payload[4] = static_cast<std::uint8_t>(system_core::data::DataCategory::OUTPUT);
    const std::uint16_t SEQUENCE =
        ground_.send(ApexExecutive::Opcode::INSPECT, payload.data(), payload.size());
    std::uint8_t status = 0xFF;
    std::vector<std::uint8_t> block;
    ReloadTarget::Output output{};
    if (SEQUENCE == 0 || !ground_.reply(SEQUENCE, POLL_LIMIT, status, &block) ||
        status != static_cast<std::uint8_t>(CommandResult::SUCCESS) ||
        block.size() != sizeof(output)) {
      return 0;
    }
    std::memcpy(&output, block.data(), sizeof(output));
    return output.instance;
  }

  /// How many times TEXT occurs in the swap log.
  [[nodiscard]] std::size_t swapLogCount(const std::string& text) const {
    const std::string LOG = slurp(swapLog());
    std::size_t count = 0;
    for (std::size_t at = LOG.find(text); at != std::string::npos;
         at = LOG.find(text, at + text.size())) {
      ++count;
    }
    return count;
  }

  CommandClient ground_;
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

  // The action's command was handed over: the replacement (instance 2 on
  // the probe) was initialized on a thread other than the task thread.
  EXPECT_NE(SWAP_LOG.find("RELOAD_LIBRARY_DEFERRED: uid=0x00FB00"), std::string::npos) << SWAP_LOG;
  ASSERT_NE(exec_->taskThread(), 0U);
  EXPECT_NE(probe_.initThread[2].load(), 0U);
  EXPECT_NE(probe_.initThread[2].load(), exec_->taskThread())
      << "the replacement was loaded and initialized on the task thread";

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

/**
 * @test A reload sent over the command route while a run of the old
 *       instance's task is in flight waits for that run: nothing is
 *       swapped and nothing destroyed until it returns, then the
 *       replacement runs. The second swap replaces a plugin, which is
 *       destroyed only after its own run has returned.
 */
TEST_F(ReloadRouteTest, SwapWaitsForATaskInFlightAndTheOldInstanceOutlivesIt) {
  constexpr std::size_t LINKED_NO = 1;
  constexpr std::size_t FIRST_PLUGIN_NO = 2;
  constexpr std::size_t SECOND_PLUGIN_NO = 3;
  std::uint8_t status = 0xFF;

  EXPECT_EQ(inspectedInstance(), LINKED_NO);

  // First swap: the linked instance is replaced by a plugin while one of its runs is held.
  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_Reload.so"));
  ASSERT_NO_FATAL_FAILURE(holdARunOf(LINKED_NO));
  ASSERT_NO_FATAL_FAILURE(lockTarget());
  const std::uint16_t FIRST = sendReload();
  ASSERT_NE(FIRST, 0u);

  // The handler has made and initialized the replacement and is waiting to publish it.
  ASSERT_TRUE(waitFor([&]() { return swapLogCount("RELOAD_LIBRARY_INIT_OK") == 1U; }))
      << slurp(swapLog());
  ASSERT_EQ(probe_.constructed.load(), 2U);
  ASSERT_TRUE(waitCycles(10));
  EXPECT_TRUE(probe_.inTask(LINKED_NO));
  EXPECT_FALSE(ground_.reply(FIRST, std::chrono::milliseconds(0), status))
      << "the reload completed with a run of the old instance in flight";
  EXPECT_EQ(swapLogCount("RELOAD_LIBRARY_OK"), 0U);
  EXPECT_EQ(probe_.entered[FIRST_PLUGIN_NO].load(), 0U) << "the replacement ran before the swap";

  release();
  ASSERT_TRUE(ground_.reply(FIRST, POLL_LIMIT, status)) << "no reply to the reload";
  EXPECT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS)) << slurp(swapLog());

  // The replacement runs; the linked instance has finished and runs no more.
  EXPECT_TRUE(waitFor([&]() { return probe_.finished[FIRST_PLUGIN_NO].load() > 0U; }))
      << "the replacement's task is not being dispatched";
  EXPECT_FALSE(probe_.inTask(LINKED_NO));
  const std::uint32_t LINKED_RUNS = probe_.entered[LINKED_NO].load();
  ASSERT_TRUE(waitCycles(10));
  EXPECT_EQ(probe_.entered[LINKED_NO].load(), LINKED_RUNS);
  EXPECT_EQ(inspectedInstance(), FIRST_PLUGIN_NO) << "INSPECT reads another instance's output";

  // Second swap: the first plugin is replaced while one of its runs is held.
  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_Reload.so"));
  ASSERT_NO_FATAL_FAILURE(holdARunOf(FIRST_PLUGIN_NO));
  ASSERT_NO_FATAL_FAILURE(lockTarget());
  const std::uint16_t SECOND = sendReload();
  ASSERT_NE(SECOND, 0u);

  ASSERT_TRUE(waitFor([&]() { return swapLogCount("RELOAD_LIBRARY_INIT_OK") == 2U; }))
      << slurp(swapLog());
  ASSERT_TRUE(waitCycles(10));
  EXPECT_TRUE(probe_.inTask(FIRST_PLUGIN_NO));
  EXPECT_FALSE(probe_.destroyed[FIRST_PLUGIN_NO].load())
      << "the plugin was destroyed with one of its runs in flight";
  EXPECT_FALSE(ground_.reply(SECOND, std::chrono::milliseconds(0), status));

  release();
  ASSERT_TRUE(ground_.reply(SECOND, POLL_LIMIT, status)) << "no reply to the second reload";
  EXPECT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS)) << slurp(swapLog());

  // The replaced plugin is destroyed, after its run returned; the new one runs.
  EXPECT_TRUE(waitFor([&]() { return probe_.destroyed[FIRST_PLUGIN_NO].load(); }))
      << "the replaced plugin was never destroyed";
  EXPECT_FALSE(probe_.destroyedInTask[FIRST_PLUGIN_NO].load());
  EXPECT_TRUE(waitFor([&]() { return probe_.finished[SECOND_PLUGIN_NO].load() > 0U; }));
  EXPECT_FALSE(probe_.destroyed[LINKED_NO].load());
  EXPECT_EQ(swapLogCount("RELOAD_LIBRARY_OK"), 2U);

  // The output block and the sequenced task are the second plugin's: the
  // first is gone, and the task goes on running through its group's counter.
  EXPECT_EQ(inspectedInstance(), SECOND_PLUGIN_NO);
  const std::uint32_t RUNS = probe_.finished[SECOND_PLUGIN_NO].load();
  EXPECT_TRUE(waitFor([&]() { return probe_.finished[SECOND_PLUGIN_NO].load() > RUNS + 5U; }));
}

/**
 * @test A reload whose wait for the tasks runs out is refused whole: the
 *       replacement is destroyed, the old instance is unlocked and runs
 *       on, and a later reload succeeds.
 */
TEST_F(ReloadRouteTest, ReloadIsRefusedWhenTheTaskDoesNotReturnInTime) {
  constexpr std::size_t LINKED_NO = 1;
  constexpr std::size_t REFUSED_NO = 2;
  constexpr std::size_t ACCEPTED_NO = 3;
  std::uint8_t status = 0xFF;

  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_Reload.so"));
  ASSERT_NO_FATAL_FAILURE(holdARunOf(LINKED_NO));
  ASSERT_NO_FATAL_FAILURE(lockTarget());
  const std::uint16_t HELD = sendReload();
  ASSERT_NE(HELD, 0u);

  // The run stays in the task past the handler's wait: the reply is the refusal.
  ASSERT_TRUE(ground_.reply(HELD, POLL_LIMIT, status)) << "no reply to the reload";
  EXPECT_EQ(status, static_cast<std::uint8_t>(ExecCommandResult::NOT_QUIESCENT));
  EXPECT_TRUE(probe_.inTask(LINKED_NO));
  EXPECT_TRUE(waitFor([&]() { return swapLogCount("reason=not_quiescent") == 1U; }))
      << slurp(swapLog());
  EXPECT_TRUE(probe_.destroyed[REFUSED_NO].load()) << "the refused replacement is still alive";
  EXPECT_EQ(probe_.entered[REFUSED_NO].load(), 0U);

  // Unlocked by the refusal: once released, the old instance is dispatched again.
  release();
  const std::uint32_t RUNS = probe_.entered[LINKED_NO].load();
  EXPECT_TRUE(waitFor([&]() { return probe_.entered[LINKED_NO].load() > RUNS + 5U; }))
      << "the old instance is not dispatched after the refusal";

  // The state is whole: the same reload, with nothing held, goes through.
  ASSERT_NO_FATAL_FAILURE(lockTarget());
  const std::uint16_t AGAIN = sendReload();
  ASSERT_NE(AGAIN, 0u);
  ASSERT_TRUE(ground_.reply(AGAIN, POLL_LIMIT, status)) << "no reply to the second reload";
  EXPECT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS)) << slurp(swapLog());
  EXPECT_TRUE(waitFor([&]() { return probe_.finished[ACCEPTED_NO].load() > 0U; }));
}

/**
 * @test A reload sent while the system is paused is published by the
 *       parked task thread, and the replacement runs after the resume.
 */
TEST_F(ReloadRouteTest, SwapIsPublishedWhileTheSystemIsPaused) {
  constexpr std::size_t PLUGIN_NO = 2;
  std::uint8_t status = 0xFF;
  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_Reload.so"));

  const std::uint16_t PAUSE = ground_.send(ApexExecutive::Opcode::CMD_PAUSE, 0U);
  ASSERT_NE(PAUSE, 0u);
  ASSERT_TRUE(ground_.reply(PAUSE, POLL_LIMIT, status));
  ASSERT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS));
  ASSERT_TRUE(waitFor([&]() {
    return (exec_->getHealthPacket().flags & executive::ExecutiveHealthPacket::FLAG_PAUSED) != 0u;
  })) << "the system did not pause";
  const std::uint64_t PAUSED_AT = exec_->getHealthPacket().clockCycles;

  ASSERT_NO_FATAL_FAILURE(lockTarget());
  const std::uint16_t RELOAD = sendReload();
  ASSERT_NE(RELOAD, 0u);
  ASSERT_TRUE(ground_.reply(RELOAD, POLL_LIMIT, status)) << "no reply to the reload";
  EXPECT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS)) << slurp(swapLog());

  // Still paused: the clock has not moved and the replacement has not run.
  EXPECT_EQ(exec_->getHealthPacket().clockCycles, PAUSED_AT);
  EXPECT_EQ(probe_.entered[PLUGIN_NO].load(), 0U);

  const std::uint16_t RESUME = ground_.send(ApexExecutive::Opcode::CMD_RESUME, 0U);
  ASSERT_NE(RESUME, 0u);
  ASSERT_TRUE(ground_.reply(RESUME, POLL_LIMIT, status));
  ASSERT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS));
  EXPECT_TRUE(waitFor([&]() { return probe_.finished[PLUGIN_NO].load() > 0U; }))
      << "the replacement's task is not dispatched after the resume";
}

/**
 * @test A swapped-in instance is attached to the internal bus as a
 *       registered one is: it is told the bus is ready once, with the bus
 *       in hand and before its task first runs, and its task runs find
 *       the bus.
 */
TEST_F(ReloadRouteTest, SwappedInComponentIsOnTheInternalBus) {
  constexpr std::size_t LINKED_NO = 1;
  constexpr std::size_t PLUGIN_NO = 2;
  // The linked instance, attached at start-up, is the reference.
  EXPECT_EQ(probe_.busReadyCalls[LINKED_NO].load(), 1U);
  EXPECT_TRUE(probe_.busAtReady[LINKED_NO].load());
  EXPECT_EQ(probe_.runsBeforeBusReady[LINKED_NO].load(), 0U);
  EXPECT_TRUE(waitFor([&]() { return probe_.busInTask[LINKED_NO].load(); }));

  std::uint8_t status = 0xFF;
  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_Reload.so"));
  ASSERT_NO_FATAL_FAILURE(lockTarget());
  const std::uint16_t RELOAD = sendReload();
  ASSERT_NE(RELOAD, 0u);
  ASSERT_TRUE(ground_.reply(RELOAD, POLL_LIMIT, status)) << "no reply to the reload";
  ASSERT_EQ(status, static_cast<std::uint8_t>(CommandResult::SUCCESS)) << slurp(swapLog());

  ASSERT_TRUE(waitFor([&]() { return probe_.finished[PLUGIN_NO].load() > 1U; }))
      << "the replacement's task is not dispatched";
  EXPECT_EQ(probe_.busReadyCalls[PLUGIN_NO].load(), 1U);
  EXPECT_TRUE(probe_.busAtReady[PLUGIN_NO].load());
  EXPECT_EQ(probe_.runsBeforeBusReady[PLUGIN_NO].load(), 0U);
  EXPECT_TRUE(probe_.busInTask[PLUGIN_NO].load());
}

/**
 * @test A replacement that cannot take over what is registered and
 *       scheduled for the component is refused whole: a shorter output
 *       block, then a task the schedule sequences left unsequenced. The
 *       old instance stays bound, unlocked and running each time.
 */
TEST_F(ReloadRouteTest, IncompatibleReplacementIsRefusedWhole) {
  constexpr std::size_t LINKED_NO = 1;
  struct Refusal {
    const char* plugin;
    ExecCommandResult status;
    const char* reason;
  };
  const std::array<Refusal, 2> REFUSALS{{
      {"ExecutiveTestPlugin_ShorterOutputReload.so", ExecCommandResult::REGISTRY_FAILED,
       "reason=registry_mismatch"},
      {"ExecutiveTestPlugin_UnsequencedReload.so", ExecCommandResult::TASK_MISMATCH,
       "reason=task_mismatch"},
  }};

  std::size_t refusedNo = LINKED_NO;
  for (const Refusal& refusal : REFUSALS) {
    ++refusedNo;
    ASSERT_NO_FATAL_FAILURE(bankPlugin(refusal.plugin));
    ASSERT_NO_FATAL_FAILURE(lockTarget());
    const std::uint16_t RELOAD = sendReload();
    ASSERT_NE(RELOAD, 0u);
    std::uint8_t status = 0xFF;
    ASSERT_TRUE(ground_.reply(RELOAD, POLL_LIMIT, status)) << "no reply to the reload";
    EXPECT_EQ(status, static_cast<std::uint8_t>(refusal.status)) << refusal.plugin;
    EXPECT_TRUE(waitFor([&]() { return swapLogCount(refusal.reason) == 1U; })) << slurp(swapLog());

    // Nothing was bound to the refused instance, and it is gone.
    EXPECT_TRUE(waitFor([&]() { return probe_.destroyed[refusedNo].load(); })) << refusal.plugin;
    EXPECT_EQ(probe_.entered[refusedNo].load(), 0U);
    EXPECT_EQ(inspectedInstance(), LINKED_NO);
    const std::uint32_t RUNS = probe_.entered[LINKED_NO].load();
    EXPECT_TRUE(waitFor([&]() { return probe_.entered[LINKED_NO].load() > RUNS + 5U; }))
        << "the old instance is not dispatched after the refusal of " << refusal.plugin;
  }
  EXPECT_EQ(swapLogCount("RELOAD_LIBRARY_OK"), 0U);
}

/**
 * @test A replacement that is a hardware model is refused before it is
 *       initialized: its transport link could not take the place of one
 *       whose far end a driver holds. The old instance stays bound,
 *       unlocked and running.
 */
TEST_F(ReloadRouteTest, HardwareModelReplacementIsRefused) {
  constexpr std::size_t LINKED_NO = 1;
  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_HardwareReload.so"));
  ASSERT_NO_FATAL_FAILURE(lockTarget());
  const std::uint16_t RELOAD = sendReload();
  ASSERT_NE(RELOAD, 0u);
  std::uint8_t status = 0xFF;
  ASSERT_TRUE(ground_.reply(RELOAD, POLL_LIMIT, status)) << "no reply to the reload";
  EXPECT_EQ(status, static_cast<std::uint8_t>(ExecCommandResult::NOT_SWAPPABLE));
  EXPECT_EQ(swapLogCount("reason=hw_model_transport"), 1U) << slurp(swapLog());
  EXPECT_EQ(swapLogCount("RELOAD_LIBRARY_INIT_OK"), 0U);
  EXPECT_EQ(swapLogCount("RELOAD_LIBRARY_OK"), 0U);

  EXPECT_EQ(inspectedInstance(), LINKED_NO);
  const std::uint32_t RUNS = probe_.entered[LINKED_NO].load();
  EXPECT_TRUE(waitFor([&]() { return probe_.entered[LINKED_NO].load() > RUNS + 5U; }))
      << "the old instance is not dispatched after the refusal";
}

/**
 * @test A reload an action sends while the component's task is scheduled
 *       is run by the external I/O thread: the replacement is loaded and
 *       initialized off the task thread, then published and dispatched.
 */
TEST_F(ReloadRouteTest, ReloadFromAnActionIsPreparedOffTheTaskThread) {
  constexpr std::size_t PLUGIN_NO = 2;
  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_Reload.so"));
  ASSERT_NO_FATAL_FAILURE(lockTarget());

  ASSERT_EQ(exec_->queueCommandAction(ApexExecutive::Opcode::RELOAD_LIBRARY), 0);
  ASSERT_TRUE(waitFor([&]() { return swapLogCount("RELOAD_LIBRARY_OK") == 1U; }))
      << slurp(swapLog());
  EXPECT_EQ(swapLogCount("RELOAD_LIBRARY_DEFERRED"), 1U);

  ASSERT_NE(exec_->taskThread(), 0U);
  EXPECT_NE(probe_.initThread[PLUGIN_NO].load(), 0U);
  EXPECT_NE(probe_.initThread[PLUGIN_NO].load(), exec_->taskThread())
      << "the replacement was loaded and initialized on the task thread";

  EXPECT_TRUE(waitFor([&]() { return probe_.finished[PLUGIN_NO].load() > 0U; }))
      << "the replacement's task is not being dispatched";
  EXPECT_EQ(inspectedInstance(), PLUGIN_NO);
}

/**
 * @test A shutdown that arrives while a reload waits for publication ends
 *       the wait: the reload is refused whole, and the scheduler is
 *       stopped only after the task thread, which publishes, has ended.
 */
TEST_F(ReloadRouteTest, ShutdownWhileASwapWaitsRefusesItWhole) {
  constexpr std::size_t LINKED_NO = 1;
  constexpr std::size_t REFUSED_NO = 2;
  ASSERT_NO_FATAL_FAILURE(bankPlugin("ExecutiveTestPlugin_Reload.so"));
  ASSERT_NO_FATAL_FAILURE(holdARunOf(LINKED_NO));
  ASSERT_NO_FATAL_FAILURE(lockTarget());
  ASSERT_NE(sendReload(), 0u);
  ASSERT_TRUE(waitFor([&]() { return swapLogCount("RELOAD_LIBRARY_INIT_OK") == 1U; }))
      << slurp(swapLog());

  // With the run still held, no settled point comes; the shutdown ends
  // the task thread, and with it the swap's chance of being published.
  exec_->shutdown();
  EXPECT_TRUE(waitFor([&]() { return swapLogCount("reason=not_quiescent") == 1U; }))
      << slurp(swapLog());
  EXPECT_TRUE(waitFor([&]() { return probe_.destroyed[REFUSED_NO].load(); }))
      << "the refused replacement is still alive";
  EXPECT_EQ(probe_.entered[REFUSED_NO].load(), 0U);

  release();
  runner_.join();
  EXPECT_EQ(swapLogCount("RELOAD_LIBRARY_OK"), 0U);
  EXPECT_FALSE(probe_.destroyed[LINKED_NO].load());
  EXPECT_FALSE(probe_.destroyedInTask[REFUSED_NO].load());

  // The scheduler's stop reads the entries a publication writes, so it
  // comes after the thread that publishes.
  const std::string SYSTEM_LOG = slurp(fsRoot_ / "system.log");
  const auto TASKS_ENDED = SYSTEM_LOG.find("Task execution stopped");
  const auto SCHEDULER_STOPPED = SYSTEM_LOG.find("Scheduler stopped");
  ASSERT_NE(TASKS_ENDED, std::string::npos) << SYSTEM_LOG;
  ASSERT_NE(SCHEDULER_STOPPED, std::string::npos) << SYSTEM_LOG;
  EXPECT_LT(TASKS_ENDED, SCHEDULER_STOPPED);
}
