/**
 * @file SchedulerSequencedDispatch_uTest.cpp
 * @brief Phase ordering within a tick, for the tasks that run on it.
 *
 * A sequence group orders its tasks by phase. The tasks of one group need
 * not run at one rate: a step may run on every tick and a report once a
 * second. On each tick the scheduler orders the group's tasks dispatched
 * on that tick and no others, so a task waits for exactly the earlier
 * phases that run with it.
 *
 * Coverage:
 *  - A slower task at a later phase never overlaps the step it follows,
 *    on a pool with two workers, whatever numbers name the two phases.
 *  - A later-phase task alone in its group on a tick runs; it does not
 *    wait for a phase that is not there.
 *  - The same holds when the earlier phase sits on the slot and is not
 *    due on that cycle.
 *  - Two tasks at one phase both finish before the phase after them
 *    starts, and the phase numbers need not count tasks.
 *  - The group's counter rests at 1 after every tick.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SequenceGroup;
using system_core::scheduler::PoolSpec;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

namespace {

constexpr std::uint64_t ONE_SECOND_NS = 1000000000ULL;

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_seq_dispatch_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// A 100 Hz scheduler whose one pool has two workers.
std::unique_ptr<SchedulerMultiThread> twoWorkers() {
  PoolSpec spec;
  spec.numThreads = 2;
  auto sched =
      std::make_unique<SchedulerMultiThread>(100, testLogDir(), std::vector<PoolSpec>{spec});
  EXPECT_EQ(sched->init(), 0U);
  return sched;
}

/// What the tasks of one group saw of one another.
struct Shared {
  std::atomic<int> inEarlier{0};    ///< Earlier-phase tasks inside their body now.
  std::atomic<int> earlierRuns{0};  ///< Earlier-phase bodies completed.
  std::atomic<int> laterRuns{0};    ///< Later-phase bodies run.
  std::atomic<int> overlaps{0};     ///< Later-phase runs that found an earlier one inside.
  std::atomic<int> laterTooSoon{0}; ///< Later-phase runs before both earlier ones were done.
  int earlierPerTick{1};            ///< Earlier-phase tasks dispatched with the later one.

  /// An earlier phase: holds its body open long enough to be overlapped.
  static std::uint8_t earlier(void* raw) noexcept {
    auto* s = static_cast<Shared*>(raw);
    s->inEarlier.fetch_add(1, std::memory_order_acq_rel);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    s->inEarlier.fetch_sub(1, std::memory_order_acq_rel);
    s->earlierRuns.fetch_add(1, std::memory_order_acq_rel);
    return 0;
  }

  /// A later phase: records whether an earlier one was still inside.
  static std::uint8_t later(void* raw) noexcept {
    auto* s = static_cast<Shared*>(raw);
    if (s->inEarlier.load(std::memory_order_acquire) != 0) {
      s->overlaps.fetch_add(1, std::memory_order_acq_rel);
    }
    const int RUN = s->laterRuns.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (s->earlierRuns.load(std::memory_order_acquire) < RUN * s->earlierPerTick) {
      s->laterTooSoon.fetch_add(1, std::memory_order_acq_rel);
    }
    return 0;
  }
};

/// Dispatch TICKS ticks from tick 0, each run to rest before the next.
void runTicks(SchedulerMultiThread& sched, std::uint32_t ticks) {
  for (std::uint32_t i = 0; i < ticks; ++i) {
    (void)sched.executeTasksOnCycleMulti(i);
    ASSERT_TRUE(sched.waitForInFlightTasks(ONE_SECOND_NS)) << "cycle " << i << " did not settle";
  }
}

/// Phase numbers for a step and the task that follows it.
struct PhasePair {
  int step;
  int report;
};

class StepAndSlowerTask : public ::testing::TestWithParam<PhasePair> {};

} // namespace

/* ----------------------------- Different rates ----------------------------- */

/**
 * @test A 1 Hz task at the later phase never overlaps the 100 Hz step it
 *       shares a tick with once a second, on two workers.
 */
TEST_P(StepAndSlowerTask, TheSlowerTaskNeverOverlapsTheStep) {
  Shared shared;
  SequenceGroup seq(2);
  auto sched = twoWorkers();

  SchedulableTask step(DelegateU8{&Shared::earlier, &shared}, "step");
  SchedulableTask report(DelegateU8{&Shared::later, &shared}, "report");
  seq.addTask(step, GetParam().step);
  seq.addTask(report, GetParam().report);
  ASSERT_EQ(sched->addTask(step, TaskConfig(100, 1, 0, 127), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(report, TaskConfig(1, 1, 51, -128), &seq), Status::SUCCESS);

  runTicks(*sched, 400);

  EXPECT_EQ(shared.laterRuns.load(), 4);
  EXPECT_EQ(shared.overlaps.load(), 0) << "the report ran while the step was inside its body";
  EXPECT_EQ(seq.counter()->load(), 1) << "the counter rests at 1 between ticks";
  sched->shutdown();
}

INSTANTIATE_TEST_SUITE_P(PhaseNumbers, StepAndSlowerTask,
                         ::testing::Values(PhasePair{1, 2}, PhasePair{0, 1}, PhasePair{3, 7}));

/* ----------------------------- A phase alone on its tick ----------------------------- */

/** @test A later-phase task alone in its group on a tick runs. */
TEST(SchedulerSequencedDispatchTest, ALaterPhaseAloneOnItsTickRuns) {
  Shared shared;
  SequenceGroup seq(2);
  auto sched = twoWorkers();

  // The step runs on even ticks, the report on tick 51: never together.
  SchedulableTask step(DelegateU8{&Shared::earlier, &shared}, "step");
  SchedulableTask report(DelegateU8{&Shared::later, &shared}, "report");
  seq.addTask(step, 1);
  seq.addTask(report, 2);
  ASSERT_EQ(sched->addTask(step, TaskConfig(50, 1, 0, 127), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(report, TaskConfig(1, 1, 51, -128), &seq), Status::SUCCESS);

  // Two seconds: the report's tick comes round twice, after 26 and after
  // 76 runs of the step.
  runTicks(*sched, 200);

  EXPECT_EQ(shared.laterRuns.load(), 2) << "the report waited for a step that is not on its tick";
  EXPECT_EQ(seq.counter()->load(), 1);
  sched->shutdown();
}

/** @test A later phase runs on the cycles where the earlier one is not due. */
TEST(SchedulerSequencedDispatchTest, ALaterPhaseRunsWhenTheEarlierOneIsNotDue) {
  Shared shared;
  SequenceGroup seq(2);
  auto sched = twoWorkers();

  // Both sit on slot 0. The first runs every other second, the second every second.
  SchedulableTask everyOther(DelegateU8{&Shared::earlier, &shared}, "every_other");
  SchedulableTask every(DelegateU8{&Shared::later, &shared}, "every");
  seq.addTask(everyOther, 1);
  seq.addTask(every, 2);
  ASSERT_EQ(sched->addTask(everyOther, TaskConfig(1, 2, 0, 127), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(every, TaskConfig(1, 1, 0, -128), &seq), Status::SUCCESS);

  runTicks(*sched, 400);

  EXPECT_EQ(shared.earlierRuns.load(), 2);
  EXPECT_EQ(shared.laterRuns.load(), 4);
  EXPECT_EQ(shared.overlaps.load(), 0);
  EXPECT_EQ(seq.counter()->load(), 1);
  sched->shutdown();
}

/* ----------------------------- Phases as an order ----------------------------- */

/**
 * @test Two tasks at one phase both finish before the next phase starts,
 *       with phase numbers that do not count tasks.
 */
TEST(SchedulerSequencedDispatchTest, APhaseWaitsForEveryTaskOfTheEarlierPhase) {
  Shared shared;
  shared.earlierPerTick = 2;
  SequenceGroup seq(3);
  auto sched = twoWorkers();

  SchedulableTask first(DelegateU8{&Shared::earlier, &shared}, "first");
  SchedulableTask second(DelegateU8{&Shared::earlier, &shared}, "second");
  SchedulableTask last(DelegateU8{&Shared::later, &shared}, "last");
  seq.addTask(first, 5);
  seq.addTask(second, 5);
  seq.addTask(last, 9);
  ASSERT_EQ(sched->addTask(first, TaskConfig(100, 1, 0, 127), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(second, TaskConfig(100, 1, 0, 126), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(last, TaskConfig(100, 1, 0, -128), &seq), Status::SUCCESS);

  runTicks(*sched, 50);

  EXPECT_EQ(shared.laterRuns.load(), 50);
  EXPECT_EQ(shared.overlaps.load(), 0);
  EXPECT_EQ(shared.laterTooSoon.load(), 0) << "the last phase started before both earlier "
                                              "tasks had finished";
  EXPECT_EQ(seq.counter()->load(), 1);
  sched->shutdown();
}
