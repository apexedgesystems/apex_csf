/**
 * @file ExecutiveTiming_dTest.cpp
 * @brief Timing-integrity tests for the executive clock grid and task drain.
 *
 * Boots a real ApexExecutive (config-less, CLI-configured, cycle-target
 * shutdown) and pins the two wall-clock contracts of the runtime:
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
 * The pause/resume path induces the stalls: pause halts the clock
 * mid-grid, so resume presents exactly the stale-deadline backlog the
 * policies exist for. These are component-level dev tests (manual
 * execution): they assert wall-clock spans, so run them on an
 * otherwise-idle host.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveState.hpp"

#include <gtest/gtest.h>

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

  const auto& HP = exec_->getHealthPacket();
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
 * wall span from cycle A to the target is the grid span even though a
 * 600 ms pause sits inside it, because resume fires the backlogged
 * deadlines as a catch-up burst. The burst also exercises the drain
 * path under collapsed wakes — the run must still end at parity. A
 * re-anchoring clock would exceed the expected span by the full pause;
 * a boolean-only wake would end short of task parity.
 */
TEST_F(ExecutiveTimingTest, ShortStallIsRepaidByCatchUpBurst) {
  constexpr std::uint64_t TARGET = 300;
  constexpr auto PAUSE = std::chrono::milliseconds(600);
  boot(TARGET);

  const auto T_A = waitForClockCycles(50, std::chrono::seconds(15));

  exec_->pause();
  waitForPaused(true, std::chrono::seconds(2));
  std::this_thread::sleep_for(PAUSE);
  exec_->resume();
  waitForPaused(false, std::chrono::seconds(2));

  const auto T_B = waitForClockCycles(TARGET, std::chrono::seconds(10));
  runner_.join();

  // 250 frames at 10 ms = 2500 ms, pause included: the grid owes no
  // extra time. The 600 ms discriminator dwarfs the tolerance.
  const auto SPAN_MS = std::chrono::duration_cast<std::chrono::milliseconds>(T_B - T_A).count();
  EXPECT_NEAR(static_cast<double>(SPAN_MS), 2500.0, 250.0);

  const auto& HP = exec_->getHealthPacket();
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
 * once and accounts for the gap in resyncDroppedTicks. A 1.6 s pause
 * at 100 Hz drops on the order of 160 ticks (the pause takes effect at
 * a frame boundary, so the count carries a few frames of slop). The
 * run must still finish at parity on the cycle target.
 */
TEST_F(ExecutiveTimingTest, LongStallResyncsWithCountedDrop) {
  constexpr std::uint64_t TARGET = 250;
  constexpr auto PAUSE = std::chrono::milliseconds(1600);
  boot(TARGET);

  waitForClockCycles(50, std::chrono::seconds(15));

  exec_->pause();
  waitForPaused(true, std::chrono::seconds(2));
  std::this_thread::sleep_for(PAUSE);
  exec_->resume();
  waitForPaused(false, std::chrono::seconds(2));

  waitForClockCycles(TARGET, std::chrono::seconds(10));
  runner_.join();

  const auto& HP = exec_->getHealthPacket();
  EXPECT_GE(HP.resyncDroppedTicks, 100u);
  EXPECT_LE(HP.resyncDroppedTicks, 220u);
  EXPECT_EQ(HP.clockCycles, TARGET);
  // Cycle-target shutdown races the final signaled tick: the task
  // thread's post-wake shutdown check may retire it before executing
  // tick TARGET, so end-state task cycles are TARGET or TARGET - 1.
  // The drain contract under test is that no larger deficit survives.
  EXPECT_GE(HP.taskExecutionCycles, TARGET - 1);
  EXPECT_LE(HP.taskExecutionCycles, TARGET);
}

} // namespace
