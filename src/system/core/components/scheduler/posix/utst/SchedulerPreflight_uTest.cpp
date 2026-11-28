/**
 * @file SchedulerPreflight_uTest.cpp
 * @brief Unit tests for the static task-table feasibility analysis.
 *
 * Coverage (pure-function analysis over constructed tables):
 *  - Clean table: all checks PASS.
 *  - Missing pool reference: FAIL with the task counted.
 *  - Dispatch burst over workers: WARN with tick/pool/count evidence.
 *  - Chain dispatch-order hazard (later phase ahead of an earlier one
 *    in the per-tick order, as priority sorting can produce): FAIL --
 *    this is the load-time guard for the pool-parking deadlock.
 *  - Chain shape: waiters >= workers and mixed-rate groups WARN.
 * Plus one integration pass through a real scheduler's
 * runTablePreflight() to prove the wiring populates verdicts.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerPreflight.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SequenceGroup;
using system_core::scheduler::analyzeTaskTable;
using system_core::scheduler::PreflightVerdict;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::TaskConfig;
using system_core::scheduler::TaskEntry;

namespace {

std::uint8_t noopTask(void*) noexcept { return 0; }

TaskEntry makeEntry(std::uint8_t poolId, std::atomic<int>* seqCounter = nullptr, int seqPhase = 0,
                    std::uint16_t freqN = 100, std::uint16_t offset = 0) {
  TaskEntry e{};
  e.config = TaskConfig(freqN, 1, offset, 0, poolId);
  e.seqCounter = seqCounter;
  e.seqPhase = seqPhase;
  e.seqMaxPhase = 3;
  return e;
}

/// An entry at N runs in every D seconds, first run at OFFSET.
TaskEntry makePaced(std::atomic<int>* seqCounter, int seqPhase, std::uint16_t freqN,
                    std::uint16_t freqD, std::uint16_t offset) {
  TaskEntry e = makeEntry(0, seqCounter, seqPhase);
  e.config = TaskConfig(freqN, freqD, offset, 0, 0);
  e.gated = freqD > 1U;
  return e;
}

/// A schedule of SLOTS slots with every entry of ON_ZERO on slot 0, in that order.
std::vector<std::vector<std::size_t>> onSlotZero(std::size_t slots,
                                                 std::vector<std::size_t> onZero) {
  std::vector<std::vector<std::size_t>> schedule(slots);
  schedule[0] = std::move(onZero);
  return schedule;
}

} // namespace

/* ----------------------------- Pure Analysis ----------------------------- */

/** @test A well-formed table passes every check. */
TEST(SchedulerPreflightTest, CleanTablePasses) {
  std::vector<TaskEntry> entries;
  entries.push_back(makeEntry(0));
  entries.push_back(makeEntry(0));
  std::vector<std::vector<std::size_t>> schedule{{0U, 1U}};
  const auto P = analyzeTaskTable(entries, schedule, {4U});

  EXPECT_EQ(P.poolRefs, PreflightVerdict::PASS);
  EXPECT_EQ(P.dispatchBurst, PreflightVerdict::PASS);
  EXPECT_EQ(P.chainOrder, PreflightVerdict::PASS);
  EXPECT_EQ(P.chainShape, PreflightVerdict::PASS);
  EXPECT_EQ(P.overall(), PreflightVerdict::PASS);
}

/** @test Tasks referencing pools that do not exist FAIL the pool check. */
TEST(SchedulerPreflightTest, MissingPoolReferenceFails) {
  std::vector<TaskEntry> entries;
  entries.push_back(makeEntry(0));
  entries.push_back(makeEntry(3)); // Only pool 0 exists.
  std::vector<std::vector<std::size_t>> schedule{{0U, 1U}};
  const auto P = analyzeTaskTable(entries, schedule, {4U});

  EXPECT_EQ(P.poolRefs, PreflightVerdict::FAIL);
  EXPECT_EQ(P.missingPoolTasks, 1U);
}

/** @test A tick loading more tasks than workers WARNs with evidence. */
TEST(SchedulerPreflightTest, DispatchBurstOverWorkersWarns) {
  std::vector<TaskEntry> entries;
  entries.reserve(5);
  for (int i = 0; i < 5; ++i) {
    entries.push_back(makeEntry(0));
  }
  std::vector<std::vector<std::size_t>> schedule{{0U, 1U, 2U, 3U, 4U}};
  const auto P = analyzeTaskTable(entries, schedule, {2U});

  EXPECT_EQ(P.dispatchBurst, PreflightVerdict::WARN);
  EXPECT_EQ(P.worstBurstTick, 0U);
  EXPECT_EQ(P.worstBurstTasks, 5U);
  EXPECT_EQ(P.worstBurstWorkers, 2U);
  EXPECT_EQ(P.worstBurstPool, 0U);
}

/**
 * @test A later phase dispatched ahead of an earlier one FAILs the
 *       order check -- the configuration a priority-inverted chain
 *       produces, and the shape that can park every worker with the
 *       earlier phase stuck behind them.
 */
TEST(SchedulerPreflightTest, ChainDispatchOrderHazardFails) {
  std::atomic<int> counter{1};
  std::vector<TaskEntry> entries;
  entries.push_back(makeEntry(0, &counter, 3)); // Tail first in dispatch order.
  entries.push_back(makeEntry(0, &counter, 1));
  std::vector<std::vector<std::size_t>> schedule{{0U, 1U}};
  const auto P = analyzeTaskTable(entries, schedule, {2U});

  EXPECT_EQ(P.chainOrder, PreflightVerdict::FAIL);
  EXPECT_EQ(P.chainOrderViolations, 1U);
  EXPECT_EQ(P.overall(), PreflightVerdict::FAIL);
}

/**
 * @test As many waiting tasks on one tick as the pool has workers WARNs
 *       the shape check; the group's mixed rates are counted beside it.
 */
TEST(SchedulerPreflightTest, ChainShapeHazardsWarn) {
  std::atomic<int> counter{1};
  std::vector<TaskEntry> entries;
  entries.push_back(makeEntry(0, &counter, 1, 100, 0));
  entries.push_back(makeEntry(0, &counter, 2, 100, 0));
  entries.push_back(makeEntry(0, &counter, 3, 50, 10)); // Mixed rate + offset.
  std::vector<std::vector<std::size_t>> schedule{{0U, 1U, 2U}};
  const auto P = analyzeTaskTable(entries, schedule, {2U}); // 2 waiters >= 2 workers.

  EXPECT_EQ(P.chainShape, PreflightVerdict::WARN);
  EXPECT_EQ(P.chainsOverWorkers, 1U);
  EXPECT_EQ(P.mixedRateChains, 1U);
}

/**
 * @test The waiting tasks are counted tick by tick: a group of three whose
 *       tasks never all share a tick keeps one waiter at most, which two
 *       workers carry, and its mixed rates alone do not WARN.
 */
TEST(SchedulerPreflightTest, ChainShapeCountsWaitersOnEachTick) {
  std::atomic<int> counter{1};
  std::vector<TaskEntry> entries;
  entries.push_back(makeEntry(0, &counter, 1, 100, 0)); // Every tick.
  entries.push_back(makeEntry(0, &counter, 2, 50, 0));  // Tick 0.
  entries.push_back(makeEntry(0, &counter, 3, 1, 1));   // Tick 1.
  std::vector<std::vector<std::size_t>> schedule{{0U, 1U}, {0U, 2U}, {0U}};
  const auto P = analyzeTaskTable(entries, schedule, {2U});

  EXPECT_EQ(P.chainShape, PreflightVerdict::PASS);
  EXPECT_EQ(P.chainsOverWorkers, 0U);
  EXPECT_EQ(P.mixedRateChains, 1U);
  EXPECT_EQ(P.chainOrder, PreflightVerdict::PASS);

  // One worker cannot carry even one parked task beside the one it waits for.
  const auto ONE = analyzeTaskTable(entries, schedule, {1U});
  EXPECT_EQ(ONE.chainShape, PreflightVerdict::WARN);
  EXPECT_EQ(ONE.chainsOverWorkers, 1U);
}

/* ----------------------------- Tasks paced over several seconds ----------------------------- */

/**
 * @test Two tasks of one group that share a slot and never a cycle are
 *       not counted together: at 10 Hz, one run every two seconds from
 *       tick 0 and one from tick 10 both sit on slot 0 and run a second
 *       apart. No burst, no waiter, and no order hazard whichever is
 *       listed first.
 */
TEST(SchedulerPreflightTest, TasksThatShareASlotAndNeverACycleAreNotCountedTogether) {
  std::atomic<int> counter{1};
  std::vector<TaskEntry> entries;
  entries.push_back(makePaced(&counter, 1, 1, 2, 0));
  entries.push_back(makePaced(&counter, 2, 1, 2, 10));

  const auto P = analyzeTaskTable(entries, onSlotZero(10, {0U, 1U}), {1U});
  EXPECT_EQ(P.dispatchBurst, PreflightVerdict::PASS);
  EXPECT_EQ(P.worstBurstTasks, 1U);
  EXPECT_EQ(P.chainShape, PreflightVerdict::PASS);
  EXPECT_EQ(P.chainsOverWorkers, 0U);
  EXPECT_EQ(P.chainOrder, PreflightVerdict::PASS);

  const auto REVERSED = analyzeTaskTable(entries, onSlotZero(10, {1U, 0U}), {1U});
  EXPECT_EQ(REVERSED.chainOrder, PreflightVerdict::PASS)
      << "an order hazard was reported between tasks that never run together";
  EXPECT_EQ(REVERSED.chainOrderViolations, 0U);
}

/**
 * @test The same two tasks with one first run do meet, every other
 *       second, and are counted: a burst of two, a waiter the one worker
 *       cannot carry, and an order hazard when the later phase is listed
 *       first.
 */
TEST(SchedulerPreflightTest, TasksPacedOverSecondsThatMeetAreCounted) {
  std::atomic<int> counter{1};
  std::vector<TaskEntry> entries;
  entries.push_back(makePaced(&counter, 1, 1, 2, 0));
  entries.push_back(makePaced(&counter, 2, 1, 2, 0));

  const auto P = analyzeTaskTable(entries, onSlotZero(10, {0U, 1U}), {1U});
  EXPECT_EQ(P.dispatchBurst, PreflightVerdict::WARN);
  EXPECT_EQ(P.worstBurstTasks, 2U);
  EXPECT_EQ(P.chainShape, PreflightVerdict::WARN);
  EXPECT_EQ(P.chainOrder, PreflightVerdict::PASS);

  const auto REVERSED = analyzeTaskTable(entries, onSlotZero(10, {1U, 0U}), {1U});
  EXPECT_EQ(REVERSED.chainOrder, PreflightVerdict::FAIL);
}

/**
 * @test A task on every run of the slot is counted with each paced task
 *       in turn and not with both: two paced tasks a second apart and one
 *       every second make a burst of two.
 */
TEST(SchedulerPreflightTest, ATaskOnEveryRunMeetsEachPacedTaskInTurn) {
  std::vector<TaskEntry> entries;
  entries.push_back(makePaced(nullptr, 0, 1, 2, 0));
  entries.push_back(makePaced(nullptr, 0, 1, 2, 10));
  entries.push_back(makePaced(nullptr, 0, 1, 1, 0));

  const auto P = analyzeTaskTable(entries, onSlotZero(10, {0U, 1U, 2U}), {1U});
  EXPECT_EQ(P.worstBurstTasks, 2U);
}

/**
 * @test Paces with no common period inside the budget: the slot is taken
 *       with every task due and counted as bounded, and an order hazard
 *       seen only there warns where an exact one fails.
 */
TEST(SchedulerPreflightTest, ASlotPastTheBudgetIsAnUpperBoundAndItsOrderHazardWarns) {
  std::atomic<int> counter{1};
  std::vector<TaskEntry> entries;
  entries.push_back(makePaced(&counter, 2, 1, 251, 0)); // Later phase first.
  entries.push_back(makePaced(&counter, 1, 1, 257, 0));

  const auto P = analyzeTaskTable(entries, onSlotZero(10, {0U, 1U}), {1U});
  EXPECT_EQ(P.boundedSlots, 1U);
  EXPECT_EQ(P.worstBurstTasks, 2U);
  EXPECT_EQ(P.chainOrder, PreflightVerdict::WARN);
  EXPECT_EQ(P.chainOrderViolations, 0U);
  EXPECT_EQ(P.chainShape, PreflightVerdict::WARN);

  // Within the budget nothing is bounded.
  std::vector<TaskEntry> near;
  near.push_back(makePaced(&counter, 2, 1, 2, 0));
  near.push_back(makePaced(&counter, 1, 1, 3, 0));
  const auto EXACT = analyzeTaskTable(near, onSlotZero(10, {0U, 1U}), {1U});
  EXPECT_EQ(EXACT.boundedSlots, 0U);
  EXPECT_EQ(EXACT.chainOrder, PreflightVerdict::FAIL) << "seconds 0, 6, 12 hold both";
}

/* ----------------------------- Counts at their limit ----------------------------- */

/**
 * @test The hazard counts stop at 255 and the verdicts do not depend on
 *       them: 256 groups with a waiter the one worker cannot carry, each
 *       listed later phase first, still WARN the shape and FAIL the order.
 */
TEST(SchedulerPreflightTest, HazardCountsSaturateAndVerdictsHold) {
  for (const std::size_t GROUPS : {std::size_t{255}, std::size_t{256}, std::size_t{300}}) {
    std::vector<std::atomic<int>> counters(GROUPS);
    std::vector<TaskEntry> entries;
    std::vector<std::size_t> onZero;
    for (std::size_t g = 0; g < GROUPS; ++g) {
      entries.push_back(makeEntry(0, &counters[g], 2, 50, 0)); // Later phase first.
      entries.push_back(makeEntry(0, &counters[g], 1, 100, 0));
      onZero.push_back(2 * g);
      onZero.push_back(2 * g + 1);
    }
    const auto P = analyzeTaskTable(entries, onSlotZero(1, onZero), {1U});

    EXPECT_EQ(P.chainShape, PreflightVerdict::WARN) << GROUPS << " groups";
    EXPECT_EQ(P.chainOrder, PreflightVerdict::FAIL) << GROUPS << " groups";
    EXPECT_EQ(P.chainsOverWorkers, 255U) << GROUPS << " groups";
    EXPECT_EQ(P.chainOrderViolations, 255U) << GROUPS << " groups";
    EXPECT_EQ(P.mixedRateChains, 255U) << GROUPS << " groups";
  }
}

/* ----------------------------- Integration ----------------------------- */

/** @test runTablePreflight populates verdicts on a live scheduler. */
TEST(SchedulerPreflightTest, RunnerPopulatesVerdicts) {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_preflight_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  SequenceGroup seq(2);
  SchedulerMultiThread sched(100, DIR);
  ASSERT_EQ(sched.init(), 0);

  DelegateU8 del{&noopTask, nullptr};
  SchedulableTask a(del, "a");
  SchedulableTask b(del, "b");
  seq.addTask(a, 1);
  seq.addTask(b, 2);
  ASSERT_EQ(sched.addTask(a, TaskConfig(100, 1, 0), &seq), system_core::scheduler::Status::SUCCESS);
  ASSERT_EQ(sched.addTask(b, TaskConfig(100, 1, 0), &seq), system_core::scheduler::Status::SUCCESS);

  sched.runTablePreflight();
  EXPECT_EQ(sched.tablePreflight().chainOrder, PreflightVerdict::PASS);
  EXPECT_EQ(sched.tablePreflight().poolRefs, PreflightVerdict::PASS);
  sched.shutdown();
}
