/**
 * @file ExecutiveActionGate_dTest.cpp
 * @brief The action phase against a task that overruns its period.
 *
 * Boots a real ApexExecutive with one scheduled task that can be held
 * inside its body for as long as the test likes, and watches what the
 * executive does while the task overruns:
 *
 *  - the clock and the task loop keep their cadence: the in-flight wait
 *    before the action phase is bounded by the frame, so an overrunning
 *    worker never stalls the tick thread; the clock's overrun count rises
 *    only on the held ticks (a worker active at a tick boundary is an
 *    overrun by its definition) and not before or after them;
 *  - the action phase is skipped, and counted, on every cycle the task is
 *    still running -- including the cycles on which the scheduler
 *    dispatches nothing new because the task is still busy;
 *  - once the task completes, the phase resumes and the skip count stops.
 *
 * The schedule comes from a scheduler TPRM the test writes into the
 * executive's bank, since a config-less boot runs an idle scheduler.
 * Wall-clock sensitive: a component-level dev test for an otherwise-idle
 * host. ACTION_GATE_KEEP_FS set in the environment keeps the executive's
 * filesystem and logs after the run.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveState.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using executive::ApexExecutive;
using Clock = std::chrono::steady_clock;

constexpr std::uint16_t CLOCK_HZ = 100;

/**
 * @brief A model whose one task can be held inside its body.
 *
 * `hold` true keeps the worker in step() -- the overrun under test;
 * `steps` counts completed steps so the test can see the task running
 * before and after.
 */
class HeldStepModel final : public system_core::system_component::SwModelBase {
public:
  static constexpr std::uint16_t COMPONENT_ID = 240;
  static constexpr const char* COMPONENT_NAME = "HeldStepModel";
  static constexpr std::uint8_t STEP_TASK_UID = 1;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  std::atomic<bool> hold{false};
  std::atomic<std::uint64_t> steps{0};

  std::uint8_t step() noexcept {
    while (hold.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    steps.fetch_add(1, std::memory_order_acq_rel);
    return 0u;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    registerTask<HeldStepModel, &HeldStepModel::step>(STEP_TASK_UID, this, "step");
    return 0u;
  }
};

/**
 * @brief Executive under test: the held model, scheduled every tick.
 *
 * Lag-tolerant soft RT so the induced overrun warns instead of shutting
 * the system down; cycle-target shutdown so every run is bounded.
 */
class GateExecutive final : public ApexExecutive {
public:
  GateExecutive(const std::filesystem::path& fsRoot, std::uint64_t targetCycles)
      : ApexExecutive("ExecutiveActionGate_dTest",
                      {"--shutdown-mode", "cycle", "--shutdown-cycle", std::to_string(targetCycles),
                       "--rt-mode", "lag-tolerant", "--rt-max-lag", "1000000", "--skip-cleanup",
                       "--startup-delay", "0", "--verbosity", "0"},
                      fsRoot) {}

  HeldStepModel model;

protected:
  [[nodiscard]] bool registerComponents() noexcept override {
    return registerComponent(&model, fileSystem().logDir());
  }
};

/// Write the scheduler's TPRM into the bank: one pool, one worker, the
/// model's step task at the fundamental.
void writeScheduleTprm(const std::filesystem::path& fsRoot) {
  using system_core::scheduler::NO_SEQUENCE_GROUP;
  using system_core::scheduler::SchedulerTaskEntry;
  using system_core::scheduler::SchedulerTprmHeader;
  using system_core::system_component::TPRM_PAYLOAD_MAGIC;
  using system_core::system_component::TPRM_PAYLOAD_VERSION;
  using system_core::system_component::tprmCrc32;
  using system_core::system_component::TprmPayloadHeader;

  const SchedulerTprmHeader HEADER{1u, 1u, 1u};
  const SchedulerTaskEntry ENTRY{static_cast<std::uint32_t>(HeldStepModel::COMPONENT_ID) << 8,
                                 HeldStepModel::STEP_TASK_UID,
                                 0u,
                                 CLOCK_HZ,
                                 1u,
                                 0u,
                                 0,
                                 NO_SEQUENCE_GROUP,
                                 0u};
  std::vector<std::uint8_t> body(sizeof(HEADER) + sizeof(ENTRY));
  std::memcpy(body.data(), &HEADER, sizeof(HEADER));
  std::memcpy(body.data() + sizeof(HEADER), &ENTRY, sizeof(ENTRY));

  constexpr std::uint32_t SCHEDULER_FULL_UID = 0x000100u;
  TprmPayloadHeader h{};
  h.magic = TPRM_PAYLOAD_MAGIC;
  h.version = TPRM_PAYLOAD_VERSION;
  h.payloadSize = body.size();
  h.fullUid = SCHEDULER_FULL_UID;
  h.layoutHash = 0u; // The scheduler reads its image without a layout check.
  h.payloadCrc = tprmCrc32(body.data(), body.size());

  const auto DIR = fsRoot / "bank_a" / "tprm";
  std::filesystem::create_directories(DIR);
  std::ofstream f(DIR / "000100.tprm", std::ios::binary);
  f.write(reinterpret_cast<const char*>(&h), sizeof(h));
  f.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
}

class ExecutiveActionGateTest : public ::testing::Test {
protected:
  void boot(std::uint64_t targetCycles) {
    fsRoot_ = std::filesystem::path(::testing::TempDir()) /
              ("exec_action_gate_" +
               std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(fsRoot_);
    std::filesystem::create_directories(fsRoot_);
    writeScheduleTprm(fsRoot_);
    exec_ = std::make_unique<GateExecutive>(fsRoot_, targetCycles);
    ASSERT_EQ(exec_->init(), 0) << "executive init failed";
    runner_ = std::thread([this]() { runResult_ = exec_->run(); });
  }

  void TearDown() override {
    if (exec_) {
      exec_->model.hold.store(false, std::memory_order_release);
    }
    if (runner_.joinable()) {
      exec_->shutdown();
      runner_.join();
    }
    exec_.reset();
    // ACTION_GATE_KEEP_FS set: leave the executive's filesystem (and its
    // logs) behind for inspection.
    if (std::getenv("ACTION_GATE_KEEP_FS") == nullptr) {
      std::filesystem::remove_all(fsRoot_);
    }
  }

  /// Poll until the task has completed `steps` steps, or fail.
  bool waitForSteps(std::uint64_t steps, std::chrono::milliseconds timeout) {
    const auto DEADLINE = Clock::now() + timeout;
    while (Clock::now() < DEADLINE) {
      if (exec_->model.steps.load(std::memory_order_acquire) >= steps) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  std::filesystem::path fsRoot_;
  std::unique_ptr<GateExecutive> exec_;
  std::thread runner_;
  executive::RunResult runResult_{};
};

} // namespace

/**
 * @test An overrunning task keeps the action phase out, cycle after
 *       cycle, without stalling the executive, until it completes.
 */
TEST_F(ExecutiveActionGateTest, OverrunSkipsThePhaseAndKeepsCadence) {
  boot(2000); // 20 s ceiling; the test ends the run itself.

  // Running normally: the task completes every cycle and the phase is
  // never held off. The executive takes a few seconds to come up on a
  // fresh filesystem; the ceiling matches the timing test's.
  ASSERT_TRUE(waitForSteps(50, std::chrono::seconds(15)));
  EXPECT_EQ(exec_->actionPhaseSkips(), 0u);
  const auto BEFORE = exec_->getHealthPacket();
  EXPECT_EQ(BEFORE.frameOverrunCount, 0u) << "a cycle that completes is not an overrun";

  // Hold the task for about thirty periods.
  exec_->model.hold.store(true, std::memory_order_release);
  const std::uint64_t STEPS_AT_HOLD = exec_->model.steps.load(std::memory_order_acquire);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto DURING = exec_->getHealthPacket();
  const std::size_t SKIPS_DURING = exec_->actionPhaseSkips();

  // The task itself did not complete while held; the executive kept
  // ticking anyway (clock and task loop both advanced through the hold,
  // within the lag the soft mode tolerates), and the phase was skipped on
  // the held cycles -- the scheduler dispatched nothing new on most of
  // them, and those cycles were held off too.
  EXPECT_LE(exec_->model.steps.load(std::memory_order_acquire), STEPS_AT_HOLD + 1u);
  EXPECT_GE(DURING.clockCycles, BEFORE.clockCycles + 25u);
  EXPECT_GE(DURING.taskExecutionCycles, BEFORE.taskExecutionCycles + 25u);
  EXPECT_LE(DURING.clockCycles - DURING.taskExecutionCycles, 2u) << "the tick thread stalled";
  EXPECT_GE(SKIPS_DURING, 20u);
  // The clock counts a worker active at a tick boundary as an overrun;
  // the held ticks are overruns by that definition, and no more than them.
  EXPECT_LE(DURING.frameOverrunCount, (DURING.clockCycles - BEFORE.clockCycles) + 1u);

  // Release. The phase resumes: the skip count stops, the task runs.
  exec_->model.hold.store(false, std::memory_order_release);
  ASSERT_TRUE(waitForSteps(STEPS_AT_HOLD + 30u, std::chrono::milliseconds(3000)));
  const std::size_t SKIPS_AFTER_RELEASE = exec_->actionPhaseSkips();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_EQ(exec_->actionPhaseSkips(), SKIPS_AFTER_RELEASE);

  // Nothing overruns once the worker is done: the wait finds nothing in
  // flight and the loop ends well inside its frame.
  const auto SETTLED = exec_->getHealthPacket();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto AFTER = exec_->getHealthPacket();
  EXPECT_LE(AFTER.clockCycles - AFTER.taskExecutionCycles, 2u);
  EXPECT_EQ(AFTER.frameOverrunCount, SETTLED.frameOverrunCount)
      << "the wait kept overrunning after the worker finished";
}
