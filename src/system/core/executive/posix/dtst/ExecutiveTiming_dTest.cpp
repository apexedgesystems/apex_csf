/**
 * @file ExecutiveTiming_dTest.cpp
 * @brief Timing-integrity tests for the executive clock grid and task drain.
 *
 * Boots a real ApexExecutive (config-less, CLI-configured, cycle-target
 * shutdown) and pins the wall-clock contracts of the runtime:
 *
 *  - Absolute deadline grid: clock cycles track wall time by
 *    construction. A stall shorter than the resync bound is repaid by a
 *    catch-up burst, so the span from cycle A to cycle B stays
 *    (B - A) * framePeriod regardless of the stall. A per-iteration
 *    re-anchor to now() would instead leak the whole stall (and every
 *    oversleep) into permanent sim-vs-wall drift.
 *
 *  - Backlog drain: ticks signaled while the task thread is busy or
 *    held may collapse into one condition-variable wake; the wait
 *    predicate's cycle-deficit term guarantees the deficit still drains
 *    to clock/task parity instead of pinning forever.
 *
 *  - Resync policy: a stall beyond one second is not caught up; the
 *    grid re-anchors once, and the dropped ticks are counted in the
 *    health packet's resyncDroppedTicks.
 *
 *  - Pause is suspended time, not owed time: resume re-anchors the grid
 *    on a clean period boundary, so a pause of any length is neither
 *    repaid nor counted as a resync.
 *
 * Stalls are real: the whole process is stopped with SIGSTOP and
 * continued by a forked child after the chosen interval, which is what
 * a host suspend or a debugger does to the clock. These are
 * component-level dev tests (manual execution): they assert wall-clock
 * spans, so run them on an otherwise-idle host.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveState.hpp"

#include <gtest/gtest.h>

#include <csignal>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using executive::ApexExecutive;
using executive::ExecutiveHealthPacket;
using Clock = std::chrono::steady_clock;

/**
 * @brief Executive under test: real ApexExecutive, no components.
 *
 * Config-less boot ("CLI args only" path): default 100 Hz clock,
 * lag-tolerant soft RT so induced stalls warn instead of shutting
 * down, cycle-target shutdown so every run is bounded.
 */
class TimingExecutive : public ApexExecutive {
public:
  TimingExecutive(const std::filesystem::path& fsRoot, std::uint64_t targetCycles)
      : ApexExecutive("ExecutiveTiming_dTest",
                      {"--shutdown-mode", "cycle", "--shutdown-cycle", std::to_string(targetCycles),
                       "--rt-mode", "lag-tolerant", "--rt-max-lag", "1000000", "--skip-cleanup",
                       "--verbosity", "0"},
                      fsRoot) {}
};

/**
 * @brief Fixture managing one executive boot per test.
 */
class ExecutiveTimingTest : public ::testing::Test {
protected:
  void boot(std::uint64_t targetCycles) {
    fsRoot_ = std::filesystem::path(::testing::TempDir()) /
              ("exec_timing_" +
               std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(fsRoot_);
    std::filesystem::create_directories(fsRoot_);
    exec_ = std::make_unique<TimingExecutive>(fsRoot_, targetCycles);
    ASSERT_EQ(exec_->init(), 0) << "executive init failed";
    runner_ = std::thread([this]() { runResult_ = exec_->run(); });
  }

  void TearDown() override {
    if (runner_.joinable()) {
      exec_->shutdown();
      runner_.join();
    }
    exec_.reset();
    std::filesystem::remove_all(fsRoot_);
  }

  /// Poll the health packet until clock cycles reach `cycles` (or fail).
  /// Returns the wall time at which the crossing was observed.
  Clock::time_point waitForClockCycles(std::uint64_t cycles, std::chrono::milliseconds timeout) {
    const auto DEADLINE = Clock::now() + timeout;
    while (Clock::now() < DEADLINE) {
      if (exec_->getHealthPacket().clockCycles >= cycles) {
        return Clock::now();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ADD_FAILURE() << "timed out waiting for clock cycle " << cycles;
    return Clock::now();
  }

  /// Stop the whole process for `stall`, the way a host suspend or a
  /// debugger does. A forked child sleeps out the interval and sends
  /// SIGCONT; the child runs only async-signal-safe calls, so forking
  /// from this threaded process is sound.
  static void stallProcessFor(std::chrono::milliseconds stall) {
    const pid_t CHILD = ::fork();
    ASSERT_GE(CHILD, 0) << "fork failed";
    if (CHILD == 0) {
      timespec ts{};
      ts.tv_sec = static_cast<time_t>(stall.count() / 1000);
      ts.tv_nsec = static_cast<long>((stall.count() % 1000) * 1'000'000L);
      while (::nanosleep(&ts, &ts) != 0) {
      }
      (void)::kill(::getppid(), SIGCONT);
      ::_exit(0);
    }
    (void)::raise(SIGSTOP);
    int status = 0;
    (void)::waitpid(CHILD, &status, 0);
  }

  /// Wait until the paused flag reports `paused`.
  void waitForPaused(bool paused, std::chrono::milliseconds timeout) {
    const auto DEADLINE = Clock::now() + timeout;
    while (Clock::now() < DEADLINE) {
      if (exec_->getHealthPacket().hasFlag(ExecutiveHealthPacket::FLAG_PAUSED) == paused) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ADD_FAILURE() << "timed out waiting for paused=" << paused;
  }

  std::filesystem::path fsRoot_{};
  std::unique_ptr<TimingExecutive> exec_{};
  std::thread runner_{};
  executive::RunResult runResult_{};
};

/* ----------------------------- Nominal cadence ----------------------------- */

/**
 * The undisturbed baseline: the span between two observed cycle counts
 * matches the grid, no resyncs occur, and the run ends at clock/task
 * parity on the exact cycle target.
 */
TEST_F(ExecutiveTimingTest, NominalRunTracksWallAtParity) {
  constexpr std::uint64_t TARGET = 250;
  boot(TARGET);

  const auto T_A = waitForClockCycles(50, std::chrono::seconds(15));
  const auto T_B = waitForClockCycles(TARGET, std::chrono::seconds(10));
  runner_.join();

  // 200 frames at 10 ms = 2000 ms. Polling quantization and scheduler
  // noise stay well under the tolerance on the serial rail.
  const auto SPAN_MS = std::chrono::duration_cast<std::chrono::milliseconds>(T_B - T_A).count();
  EXPECT_NEAR(static_cast<double>(SPAN_MS), 2000.0, 150.0);

  const auto HP = exec_->getHealthPacket();
  EXPECT_EQ(HP.clockCycles, TARGET);
  // Cycle-target shutdown races the final signaled tick: the task
  // thread's post-wake shutdown check may retire it before executing
  // tick TARGET, so end-state task cycles are TARGET or TARGET - 1.
  // The drain contract under test is that no larger deficit survives.
  EXPECT_GE(HP.taskExecutionCycles, TARGET - 1);
  EXPECT_LE(HP.taskExecutionCycles, TARGET);
  EXPECT_EQ(HP.resyncDroppedTicks, 0u);
}

/* ----------------------------- Grid repayment ----------------------------- */

/**
 * A stall shorter than the resync bound must be repaid, not leaked: the
 * wall span from cycle A to the target is the grid span even though the
 * process was stopped for 600 ms inside it, because the wake after the
 * stall fires the backlogged deadlines as a catch-up burst. The burst
 * also exercises the drain path under collapsed wakes -- the run must
 * still end at parity. A re-anchoring clock would exceed the expected
 * span by the full stall; a boolean-only wake would end short of task
 * parity.
 */
TEST_F(ExecutiveTimingTest, ShortStallIsRepaidByCatchUpBurst) {
  constexpr std::uint64_t TARGET = 300;
  constexpr auto STALL = std::chrono::milliseconds(600);
  boot(TARGET);

  const auto T_A = waitForClockCycles(50, std::chrono::seconds(15));
  stallProcessFor(STALL);
  const auto T_B = waitForClockCycles(TARGET, std::chrono::seconds(10));
  runner_.join();

  // 250 frames at 10 ms = 2500 ms, stall included: the grid owes no
  // extra time. The 600 ms discriminator dwarfs the tolerance.
  const auto SPAN_MS = std::chrono::duration_cast<std::chrono::milliseconds>(T_B - T_A).count();
  EXPECT_NEAR(static_cast<double>(SPAN_MS), 2500.0, 250.0);

  const auto HP = exec_->getHealthPacket();
  EXPECT_EQ(HP.clockCycles, TARGET);
  // Cycle-target shutdown races the final signaled tick: the task
  // thread's post-wake shutdown check may retire it before executing
  // tick TARGET, so end-state task cycles are TARGET or TARGET - 1.
  // The drain contract under test is that no larger deficit survives.
  EXPECT_GE(HP.taskExecutionCycles, TARGET - 1);
  EXPECT_LE(HP.taskExecutionCycles, TARGET);
  EXPECT_EQ(HP.resyncDroppedTicks, 0u);
}

/* ----------------------------- Resync policy ----------------------------- */

/**
 * A stall beyond one second must NOT be caught up: the grid re-anchors
 * once and accounts for the gap in resyncDroppedTicks. The policy is
 * all or nothing, so a 1.6 s stop at 100 Hz drops the 160 deadlines
 * that passed during it, give or take the one the wake handles and the
 * few frames the host takes to run the clock thread again. Dropped
 * ticks are never issued, so the cycle count falls behind wall time by
 * the stall for good: the span from cycle A to the target grows by the
 * whole stall, the signature that distinguishes a drop from a repaid
 * backlog. The run must still finish at parity on the cycle target.
 */
TEST_F(ExecutiveTimingTest, LongStallResyncsWithCountedDrop) {
  constexpr std::uint64_t TARGET = 250;
  constexpr auto STALL = std::chrono::milliseconds(1600);
  boot(TARGET);

  const auto T_A = waitForClockCycles(50, std::chrono::seconds(15));
  stallProcessFor(STALL);
  const auto T_B = waitForClockCycles(TARGET, std::chrono::seconds(10));
  runner_.join();

  const auto HP = exec_->getHealthPacket();
  EXPECT_GE(HP.resyncDroppedTicks, 150u);
  EXPECT_LE(HP.resyncDroppedTicks, 175u);

  // 200 frames at 10 ms = 2000 ms plus the 1600 ms stall in full: the
  // dropped deadlines were never issued as cycles.
  const auto SPAN_MS = std::chrono::duration_cast<std::chrono::milliseconds>(T_B - T_A).count();
  EXPECT_NEAR(static_cast<double>(SPAN_MS), 3600.0, 250.0);

  EXPECT_EQ(HP.clockCycles, TARGET);
  // Cycle-target shutdown races the final signaled tick: the task
  // thread's post-wake shutdown check may retire it before executing
  // tick TARGET, so end-state task cycles are TARGET or TARGET - 1.
  // The drain contract under test is that no larger deficit survives.
  EXPECT_GE(HP.taskExecutionCycles, TARGET - 1);
  EXPECT_LE(HP.taskExecutionCycles, TARGET);
}

/* ----------------------------- Pause policy ----------------------------- */

/**
 * Paused time is suspended, not owed. Resume re-anchors the grid on a
 * clean period boundary, so the wall span from cycle A to the target
 * grows by the whole pause, and a pause longer than the resync bound is
 * still not a resync: no catch-up burst (which would read as period
 * violations, fatal under hard RT) and no dropped-tick count.
 */
TEST_F(ExecutiveTimingTest, PauseIsSuspendedNotOwed) {
  constexpr std::uint64_t TARGET = 250;
  constexpr auto PAUSE = std::chrono::milliseconds(1200);
  boot(TARGET);

  const auto T_A = waitForClockCycles(50, std::chrono::seconds(15));

  exec_->pause();
  waitForPaused(true, std::chrono::seconds(2));
  std::this_thread::sleep_for(PAUSE);
  exec_->resume();
  waitForPaused(false, std::chrono::seconds(2));

  const auto T_B = waitForClockCycles(TARGET, std::chrono::seconds(10));
  runner_.join();

  // 200 frames at 10 ms = 2000 ms, plus the 1200 ms pause in full. The
  // pause takes effect at a frame boundary on either side, so the
  // tolerance carries a few frames.
  const auto SPAN_MS = std::chrono::duration_cast<std::chrono::milliseconds>(T_B - T_A).count();
  EXPECT_NEAR(static_cast<double>(SPAN_MS), 3200.0, 250.0);

  const auto HP = exec_->getHealthPacket();
  EXPECT_EQ(HP.resyncDroppedTicks, 0u);
  EXPECT_EQ(HP.clockCycles, TARGET);
  EXPECT_GE(HP.taskExecutionCycles, TARGET - 1);
  EXPECT_LE(HP.taskExecutionCycles, TARGET);
}

} // namespace
