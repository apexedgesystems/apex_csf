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
 *  - once the task completes, the phase resumes and the skip count stops;
 *  - after the whole process is stalled (SIGSTOP, as a host suspend or a
 *    debugger does it), the clock's catch-up burst leaves the task loop
 *    behind by many cycles: the in-flight wait is reduced to a poll on
 *    every one of those cycles, counted -- and skips the phase when that
 *    poll finds the just-dispatched task in flight -- and the backlog
 *    drains to parity instead of each old cycle being granted the latest
 *    tick's frame.
 *
 * The schedule comes from a scheduler TPRM the test writes into the
 * executive's bank, since a config-less boot runs an idle scheduler.
 * Wall-clock sensitive: a component-level dev test for an otherwise-idle
 * host. ACTION_GATE_KEEP_FS set in the environment keeps the executive's
 * filesystem and logs after the run.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveState.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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

/// The host can hold the task thread now and then, with nothing in
/// flight: past a tick, the cycle that follows polls while behind, skips
/// the phase and counts a lag overrun; short of a tick but past the
/// wait's bound within its frame, the cycle skips without being behind.
/// Each window that claims the loop keeps pace tolerates this many of
/// those, against the twenty-odd the hold and the stall produce in
/// thirty cycles.
constexpr std::uint64_t HOST_HOLDS_TOLERATED = 2;

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

  /// Armed, the next cycle that selects a wait of over 5 ms is held for
  /// 20 ms at the seam between selecting its deadline and waiting on
  /// it: a preemption there, two ticks long, on demand.
  std::atomic<bool> holdAtSelection{false};
  std::atomic<unsigned> heldAtSelection{0};

protected:
  [[nodiscard]] bool registerComponents() noexcept override {
    return registerComponent(&model, fileSystem().logDir());
  }

  void onWaitDeadlineSelected(std::uint64_t, std::uint64_t waitEndNs) noexcept override {
    if (holdAtSelection.load(std::memory_order_acquire) &&
        waitEndNs > apex::helpers::cpu::getMonotonicNs() + 5'000'000ULL) {
      holdAtSelection.store(false, std::memory_order_release);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      heldAtSelection.fetch_add(1u, std::memory_order_acq_rel);
    }
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

/// Stop the whole process for `stall`, the way a host suspend or a
/// debugger does: a forked child sends SIGSTOP, sleeps, then SIGCONT; the
/// child runs only async-signal-safe calls. Returns how long the caller
/// was away, which is the stall only if the child delivered it: a fork
/// or a signal that fails leaves the process running and the caller
/// back at once, and the test checks the return rather than assume.
std::chrono::milliseconds stallProcessFor(std::chrono::milliseconds stall) {
  const auto T0 = Clock::now();
  const pid_t CHILD = ::fork();
  if (CHILD == 0) {
    const pid_t PARENT = ::getppid();
    timespec ts{};
    ts.tv_sec = static_cast<time_t>(stall.count() / 1000);
    ts.tv_nsec = static_cast<long>((stall.count() % 1000) * 1'000'000L);
    ::kill(PARENT, SIGSTOP);
    ::nanosleep(&ts, nullptr);
    ::kill(PARENT, SIGCONT);
    ::_exit(0);
  }
  int status = 0;
  ::waitpid(CHILD, &status, 0);
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - T0);
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

  // Running normally: the task completes every cycle and the wait never
  // holds the phase off; only a host hold registers, within tolerance.
  // The executive takes a few seconds to come up on a fresh filesystem;
  // the ceiling matches the timing test's.
  ASSERT_TRUE(waitForSteps(50, std::chrono::seconds(15)));
  const std::size_t SKIPS_BEFORE = exec_->actionPhaseSkips();
  EXPECT_LE(SKIPS_BEFORE, HOST_HOLDS_TOLERATED) << "the phase was held off at pace";
  const auto BEFORE = exec_->getHealthPacket();
  EXPECT_LE(BEFORE.frameOverrunCount, HOST_HOLDS_TOLERATED)
      << "a cycle that completes is not an overrun";

  // Hold the task for about thirty periods.
  exec_->model.hold.store(true, std::memory_order_release);
  const std::uint64_t STEPS_AT_HOLD = exec_->model.steps.load(std::memory_order_acquire);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto DURING = exec_->getHealthPacket();
  const std::size_t SKIPS_DURING = exec_->actionPhaseSkips() - SKIPS_BEFORE;

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
  EXPECT_LE(DURING.frameOverrunCount - BEFORE.frameOverrunCount,
            (DURING.clockCycles - BEFORE.clockCycles) + 1u);

  // Release. The phase resumes: the skip count stops, the task runs.
  exec_->model.hold.store(false, std::memory_order_release);
  ASSERT_TRUE(waitForSteps(STEPS_AT_HOLD + 30u, std::chrono::milliseconds(3000)));
  const std::size_t SKIPS_AFTER_RELEASE = exec_->actionPhaseSkips();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_LE(exec_->actionPhaseSkips() - SKIPS_AFTER_RELEASE, HOST_HOLDS_TOLERATED);

  // Nothing overruns once the worker is done: the wait finds nothing in
  // flight and the loop ends well inside its frame.
  const auto SETTLED = exec_->getHealthPacket();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto AFTER = exec_->getHealthPacket();
  EXPECT_LE(AFTER.clockCycles - AFTER.taskExecutionCycles, 2u);
  EXPECT_LE(AFTER.frameOverrunCount - SETTLED.frameOverrunCount, HOST_HOLDS_TOLERATED)
      << "the wait kept overrunning after the worker finished";
}

/**
 * @test A stalled process drains its backlog while its task is still
 *       held: every behind cycle is a poll, never the latest tick's frame.
 *
 * The task is held before the stall, so afterwards the loop cannot catch
 * the clock up by finishing work: the only way back to parity is a zero
 * wait on every cycle processed while behind. A wait that handed an old
 * cycle the latest tick's frame would spend most of a period on each of
 * the thirty replayed ticks and miss the bound by an order of magnitude,
 * whatever it counted.
 */
TEST_F(ExecutiveActionGateTest, StallDrainsTheBacklogWithoutGrantingOldCyclesAFrame) {
  boot(2000);
  ASSERT_TRUE(waitForSteps(50, std::chrono::seconds(15)));

  // Hold the task. From here every cycle waits out its frame and skips
  // the phase, and the loop keeps within a cycle of the clock.
  exec_->model.hold.store(true, std::memory_order_release);
  const std::size_t SKIPS_AT_HOLD = exec_->actionPhaseSkips();
  {
    const auto DEADLINE = Clock::now() + std::chrono::milliseconds(500);
    while (Clock::now() < DEADLINE && exec_->actionPhaseSkips() < SKIPS_AT_HOLD + 2u) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  ASSERT_GE(exec_->actionPhaseSkips(), SKIPS_AT_HOLD + 2u) << "the hold did not take effect";
  const std::size_t POLLS_BEFORE = exec_->actionWaitBacklogPolls();
  const std::uint64_t BEHIND_WAIT_BEFORE_NS = exec_->actionWaitBehindMaxNs();
  const auto BEFORE = exec_->getHealthPacket();
  const auto T_BEFORE = Clock::now();

  // Thirty periods of stall. The clock keeps its grid through it and on
  // resume issues the missed ticks as fast as it can.
  const auto STALLED = stallProcessFor(std::chrono::milliseconds(300));
  ASSERT_GE(STALLED.count(), 290) << "the stall did not take effect";
  const auto T_RESUME = Clock::now();

  // Parity: the clock back at wall time (as many ticks since BEFORE as
  // periods have elapsed, within one) and the loop within a cycle of it,
  // with the task still held. Thirty zero-budget polls take milliseconds;
  // thirty frames would take most of the stall over again.
  constexpr auto PERIOD = std::chrono::milliseconds(1000 / CLOCK_HZ);
  constexpr auto DRAIN_BOUND = std::chrono::milliseconds(100);
  bool parity = false;
  while (Clock::now() < T_RESUME + DRAIN_BOUND) {
    const auto H = exec_->getHealthPacket();
    const std::uint64_t TICKS_DUE =
        BEFORE.clockCycles + static_cast<std::uint64_t>((Clock::now() - T_BEFORE) / PERIOD);
    if (H.clockCycles + 1u >= TICKS_DUE && H.clockCycles >= BEFORE.clockCycles + 25u &&
        H.clockCycles - H.taskExecutionCycles <= 1u) {
      parity = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto DRAIN_MS =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - T_RESUME).count();
  EXPECT_TRUE(parity) << "the loop did not catch the clock up within " << DRAIN_BOUND.count()
                      << " ms with the task held: a behind cycle was given a frame";
  RecordProperty("drain_ms", static_cast<int>(DRAIN_MS));
  // The direct measure: every cycle that started behind got a poll. With
  // the task held the poll finds it in flight and returns at once, so the
  // longest such wait is the poll's own cost; a budget taken from the
  // latest tick instead would hold the first behind cycle for most of a
  // frame, whatever the replay's pace made of the others.
  constexpr std::uint64_t POLL_BOUND_NS = 2'000'000U;
  const std::uint64_t BEHIND_WAIT_NS = exec_->actionWaitBehindMaxNs();
  EXPECT_GT(BEHIND_WAIT_NS, BEHIND_WAIT_BEFORE_NS) << "no cycle was processed behind";
  EXPECT_LT(BEHIND_WAIT_NS, POLL_BOUND_NS)
      << "a cycle that started behind was granted " << BEHIND_WAIT_NS / 1000U << " us";
  RecordProperty("behind_wait_us", static_cast<int>(BEHIND_WAIT_NS / 1000U));
  RecordProperty("backlog_polls", static_cast<int>(exec_->actionWaitBacklogPolls() - POLLS_BEFORE));

  // Release. The task runs again, the phase resumes, and at pace no
  // further cycle is behind and nothing skips, beyond what a host hold
  // would add.
  exec_->model.hold.store(false, std::memory_order_release);
  const std::uint64_t STEPS_AT_RELEASE = exec_->model.steps.load(std::memory_order_acquire);
  ASSERT_TRUE(waitForSteps(STEPS_AT_RELEASE + 10u, std::chrono::milliseconds(3000)));
  const auto POLLS_AT_PACE = exec_->actionWaitBacklogPolls();
  const auto SKIPS_AT_PACE = exec_->actionPhaseSkips();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_LE(exec_->actionWaitBacklogPolls() - POLLS_AT_PACE, HOST_HOLDS_TOLERATED);
  EXPECT_LE(exec_->actionPhaseSkips() - SKIPS_AT_PACE, HOST_HOLDS_TOLERATED);
  const auto AFTER = exec_->getHealthPacket();
  EXPECT_LE(AFTER.clockCycles - AFTER.taskExecutionCycles, 2u);
}

/**
 * @test A preemption between selecting a cycle's wait deadline and waiting
 *       on it does not renew the frame: the deadline is an instant, so a
 *       cycle held past its frame's end polls, whatever budget it chose.
 */
TEST_F(ExecutiveActionGateTest, PreemptionAfterSelectingTheDeadlineDoesNotRenewTheWait) {
  boot(2000);
  ASSERT_TRUE(waitForSteps(50, std::chrono::seconds(15)));

  // Hold the task so every cycle selects most of a frame as its wait.
  exec_->model.hold.store(true, std::memory_order_release);
  const std::size_t SKIPS_AT_HOLD = exec_->actionPhaseSkips();
  {
    const auto DEADLINE = Clock::now() + std::chrono::milliseconds(500);
    while (Clock::now() < DEADLINE && exec_->actionPhaseSkips() < SKIPS_AT_HOLD + 2u) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  ASSERT_GE(exec_->actionPhaseSkips(), SKIPS_AT_HOLD + 2u) << "the hold did not take effect";
  const std::uint64_t BEHIND_WAIT_BEFORE_NS = exec_->actionWaitBehindMaxNs();

  // One cycle is held 20 ms at the seam: by the time it waits, two ticks
  // have passed and its frame is over.
  exec_->holdAtSelection.store(true, std::memory_order_release);
  {
    const auto DEADLINE = Clock::now() + std::chrono::seconds(1);
    while (Clock::now() < DEADLINE &&
           exec_->heldAtSelection.load(std::memory_order_acquire) == 0u) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  ASSERT_EQ(exec_->heldAtSelection.load(std::memory_order_acquire), 1u) << "no cycle was held";
  // Its wait has returned and been recorded within a frame of the hold's end.
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const std::uint64_t BEHIND_WAIT_NS = exec_->actionWaitBehindMaxNs();
  EXPECT_GT(BEHIND_WAIT_NS, BEHIND_WAIT_BEFORE_NS) << "the held cycle was not behind";
  EXPECT_LT(BEHIND_WAIT_NS, 2'000'000ULL)
      << "the held cycle was granted " << BEHIND_WAIT_NS / 1000U << " us past its frame";
  RecordProperty("behind_wait_us", static_cast<int>(BEHIND_WAIT_NS / 1000U));

  exec_->model.hold.store(false, std::memory_order_release);
  const std::uint64_t STEPS_AT_RELEASE = exec_->model.steps.load(std::memory_order_acquire);
  ASSERT_TRUE(waitForSteps(STEPS_AT_RELEASE + 10u, std::chrono::milliseconds(3000)));
}
