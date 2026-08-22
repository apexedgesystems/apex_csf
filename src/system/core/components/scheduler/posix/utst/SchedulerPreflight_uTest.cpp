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

/** @test Waiters >= workers and mixed-rate groups WARN the shape check. */
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
