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
 *  - A task the scheduler could not hand to its pool is not counted by
 *    the tasks of its group that it did hand over: a later phase does not
 *    wait for it, and the counter rests at 1 so the next tick is ordered.
 *  - A group whose tasks from an earlier tick are still running is not
 *    dispatched again until they have finished: no task of it joins the
 *    unfinished tick, and none is left waiting once it ends.
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

/// A scheduler with one pool per entry of WORKERS, not yet initialized:
/// a test that needs the context pools sized for its table adds its tasks
/// and then calls init().
std::unique_ptr<SchedulerMultiThread> withPools(const std::vector<std::size_t>& workers) {
  std::vector<PoolSpec> specs;
  for (const std::size_t COUNT : workers) {
    PoolSpec spec;
    spec.numThreads = COUNT;
    specs.push_back(spec);
  }
  return std::make_unique<SchedulerMultiThread>(100, testLogDir(), specs);
}

/// Poll PRED until it holds or LIMIT passes.
template <typename Pred> bool waitFor(Pred pred, std::chrono::milliseconds limit) {
  const auto END = std::chrono::steady_clock::now() + limit;
  while (!pred()) {
    if (std::chrono::steady_clock::now() > END) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

/// Tasks that hold contexts, and a two-phase group dispatched once those
/// contexts are nearly or wholly gone.
struct Admission {
  std::atomic<bool> releaseFillers{false}; ///< Fillers return once set.
  std::atomic<bool> releaseFirst{false};   ///< A held first phase returns once set.
  std::atomic<bool> firstInside{false};    ///< The first phase is held in its body.
  std::atomic<int> holdFirstOnRun{0};      ///< Run of the first phase to hold; 0 for none.
  std::atomic<int> firstRuns{0};
  std::atomic<int> besideRuns{0}; ///< Runs of a task at the first phase that is never held.
  std::atomic<int> secondRuns{0};
  std::atomic<int> overlaps{0}; ///< Second-phase runs that found the first inside.

  static std::uint8_t filler(void* raw) noexcept {
    auto* s = static_cast<Admission*>(raw);
    (void)waitFor([s] { return s->releaseFillers.load(std::memory_order_acquire); },
                  std::chrono::seconds(10));
    return 0;
  }

  static std::uint8_t first(void* raw) noexcept {
    auto* s = static_cast<Admission*>(raw);
    const int RUN = s->firstRuns.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (RUN == s->holdFirstOnRun.load(std::memory_order_acquire)) {
      s->firstInside.store(true, std::memory_order_release);
      (void)waitFor([s] { return s->releaseFirst.load(std::memory_order_acquire); },
                    std::chrono::seconds(10));
      s->firstInside.store(false, std::memory_order_release);
    }
    return 0;
  }

  static std::uint8_t beside(void* raw) noexcept {
    static_cast<Admission*>(raw)->besideRuns.fetch_add(1, std::memory_order_acq_rel);
    return 0;
  }

  static std::uint8_t second(void* raw) noexcept {
    auto* s = static_cast<Admission*>(raw);
    if (s->firstInside.load(std::memory_order_acquire)) {
      s->overlaps.fetch_add(1, std::memory_order_acq_rel);
    }
    s->secondRuns.fetch_add(1, std::memory_order_acq_rel);
    return 0;
  }
};

/// Whether tasks still busy are skipped (true) or dispatched again (false).
class GroupStillRunning : public ::testing::TestWithParam<bool> {};

/// Lets every held task go and stops the scheduler when a test ends, on
/// a failed expectation as on a passed one.
struct ReleaseAndStop {
  Admission& state;
  SchedulerMultiThread& sched;
  ~ReleaseAndStop() {
    state.releaseFillers.store(true, std::memory_order_release);
    state.releaseFirst.store(true, std::memory_order_release);
    sched.shutdown();
  }
};

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

/* ----------------------------- A task that is not admitted ----------------------------- */

/**
 * @test The first phase of a group cannot be handed to its pool, whose
 *       contexts are all held; the second phase, in another pool, runs
 *       without waiting for it and the counter rests at 1.
 */
TEST(SchedulerSequencedDispatchTest, ALaterPhaseDoesNotWaitForATaskThatWasNotAdmitted) {
  Admission state;
  SequenceGroup seq(2);
  auto sched = withPools({1, 1});

  // Pool 0 gets five contexts for this table (a burst of three and two for
  // its one worker); five fillers hold them all from ticks 0 and 1.
  std::vector<std::unique_ptr<SchedulableTask>> fillers;
  for (int i = 0; i < 5; ++i) {
    fillers.push_back(
        std::make_unique<SchedulableTask>(DelegateU8{&Admission::filler, &state}, "filler"));
    const std::uint16_t OFFSET = (i < 3) ? 0 : 1;
    ASSERT_EQ(sched->addTask(*fillers.back(), TaskConfig(1, 1, OFFSET, 0, 0)), Status::SUCCESS);
  }
  SchedulableTask first(DelegateU8{&Admission::first, &state}, "first");
  SchedulableTask second(DelegateU8{&Admission::second, &state}, "second");
  seq.addTask(first, 1);
  seq.addTask(second, 2);
  ASSERT_EQ(sched->addTask(first, TaskConfig(1, 1, 2, 127, 0), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(second, TaskConfig(1, 1, 2, -128, 1), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->init(), 0U);
  sched->setContextFallback(false);
  const ReleaseAndStop AT_EXIT{state, *sched};

  (void)sched->executeTasksOnCycleMulti(0);
  (void)sched->executeTasksOnCycleMulti(1);
  ASSERT_EQ(sched->totalDispatchDrops(), 0U);
  (void)sched->executeTasksOnCycleMulti(2);
  ASSERT_EQ(sched->totalDispatchDrops(), 1U) << "the first phase was to find no context";

  EXPECT_TRUE(waitFor([&state] { return state.secondRuns.load() == 1; }, std::chrono::seconds(2)))
      << "the second phase waited for a first phase that was never handed over";

  state.releaseFillers.store(true, std::memory_order_release);
  EXPECT_TRUE(sched->waitForInFlightTasks(5 * ONE_SECOND_NS));
  EXPECT_EQ(state.firstRuns.load(), 0);
  EXPECT_EQ(state.secondRuns.load(), 1);
  EXPECT_EQ(seq.counter()->load(), 1) << "the counter rests at 1 after the tick's tasks";
}

/**
 * @test The second phase of a group finds no context; the first runs
 *       alone and leaves the counter at 1, so on the next tick the second
 *       phase waits for the first as on any other.
 */
TEST(SchedulerSequencedDispatchTest, ATaskThatWasNotAdmittedLeavesTheNextTickInOrder) {
  Admission state;
  SequenceGroup seq(2);
  auto sched = withPools({2});

  // Eight contexts for this table (a burst of four and four for the two
  // workers); seven fillers hold all but one from ticks 0 and 1.
  std::vector<std::unique_ptr<SchedulableTask>> fillers;
  for (int i = 0; i < 7; ++i) {
    fillers.push_back(
        std::make_unique<SchedulableTask>(DelegateU8{&Admission::filler, &state}, "filler"));
    const std::uint16_t OFFSET = (i < 4) ? 0 : 1;
    ASSERT_EQ(sched->addTask(*fillers.back(), TaskConfig(1, 1, OFFSET, 0, 0)), Status::SUCCESS);
  }
  SchedulableTask first(DelegateU8{&Admission::first, &state}, "first");
  SchedulableTask second(DelegateU8{&Admission::second, &state}, "second");
  seq.addTask(first, 1);
  seq.addTask(second, 2);
  ASSERT_EQ(sched->addTask(first, TaskConfig(1, 1, 2, 127, 0), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(second, TaskConfig(1, 1, 2, -128, 0), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->init(), 0U);
  sched->setContextFallback(false);
  const ReleaseAndStop AT_EXIT{state, *sched};

  // First second: the second phase is dropped, the first runs alone.
  (void)sched->executeTasksOnCycleMulti(0);
  (void)sched->executeTasksOnCycleMulti(1);
  (void)sched->executeTasksOnCycleMulti(2);
  ASSERT_EQ(sched->totalDispatchDrops(), 1U) << "the second phase was to find no context";
  state.releaseFillers.store(true, std::memory_order_release);
  ASSERT_TRUE(sched->waitForInFlightTasks(5 * ONE_SECOND_NS));
  EXPECT_EQ(state.firstRuns.load(), 1);
  EXPECT_EQ(state.secondRuns.load(), 0);
  EXPECT_EQ(seq.counter()->load(), 1) << "one task ran, and the counter is back at rest";

  // Next second: both are admitted, and the first is held in its body.
  state.holdFirstOnRun.store(2, std::memory_order_release);
  (void)sched->executeTasksOnCycleMulti(100);
  (void)sched->executeTasksOnCycleMulti(101);
  ASSERT_TRUE(sched->waitForInFlightTasks(5 * ONE_SECOND_NS));
  (void)sched->executeTasksOnCycleMulti(102);
  ASSERT_EQ(sched->totalDispatchDrops(), 1U);
  ASSERT_TRUE(waitFor([&state] { return state.firstInside.load(); }, std::chrono::seconds(2)));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_EQ(state.secondRuns.load(), 0) << "the second phase ran while the first was in its body";

  state.releaseFirst.store(true, std::memory_order_release);
  EXPECT_TRUE(sched->waitForInFlightTasks(5 * ONE_SECOND_NS));
  EXPECT_EQ(state.secondRuns.load(), 1);
  EXPECT_EQ(state.overlaps.load(), 0);
  EXPECT_EQ(seq.counter()->load(), 1);
}

/* ----------------------------- A group still running ----------------------------- */

/**
 * @test The first task of a group is still in its body when the next
 *       tick comes. No task of the group is dispatched on that tick: the
 *       task beside it, which is free, does not join the unfinished tick,
 *       and the second phase runs once, after the first, when it is let
 *       go. The tick after that runs the whole group in order.
 */
TEST_P(GroupStillRunning, IsNotDispatchedAgainUntilItHasComeToRest) {
  Admission state;
  SequenceGroup seq(3);
  auto sched = withPools({2});

  SchedulableTask first(DelegateU8{&Admission::first, &state}, "first");
  SchedulableTask beside(DelegateU8{&Admission::beside, &state}, "beside");
  SchedulableTask second(DelegateU8{&Admission::second, &state}, "second");
  seq.addTask(first, 1);
  seq.addTask(beside, 1);
  seq.addTask(second, 2);
  ASSERT_EQ(sched->addTask(first, TaskConfig(100, 1, 0, 127), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(beside, TaskConfig(100, 1, 0, 126), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(second, TaskConfig(100, 1, 0, -128), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->init(), 0U);
  sched->setSkipOnBusy(GetParam());
  const ReleaseAndStop AT_EXIT{state, *sched};

  // Tick 0: the first task stays in its body; the one beside it finishes;
  // the second phase waits for both.
  state.holdFirstOnRun.store(1, std::memory_order_release);
  (void)sched->executeTasksOnCycleMulti(0);
  ASSERT_TRUE(waitFor([&state] { return state.firstInside.load() && state.besideRuns.load() == 1; },
                      std::chrono::seconds(2)));

  // Tick 1: the group is still running.
  (void)sched->executeTasksOnCycleMulti(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(state.besideRuns.load(), 1) << "a free task joined a tick of its group still running";
  EXPECT_EQ(state.secondRuns.load(), 0) << "the second phase ran with the first in its body";

  state.releaseFirst.store(true, std::memory_order_release);
  ASSERT_TRUE(sched->waitForInFlightTasks(5 * ONE_SECOND_NS))
      << "a task of the group was left waiting";
  EXPECT_EQ(state.firstRuns.load(), 1);
  EXPECT_EQ(state.besideRuns.load(), 1);
  EXPECT_EQ(state.secondRuns.load(), 1);
  EXPECT_EQ(state.overlaps.load(), 0);
  EXPECT_EQ(seq.counter()->load(), 1);
  EXPECT_EQ(sched->totalSkipCount(), 3U) << "the three tasks due on tick 1 were to be held back";

  // Tick 2: the group is at rest and runs whole.
  (void)sched->executeTasksOnCycleMulti(2);
  ASSERT_TRUE(sched->waitForInFlightTasks(5 * ONE_SECOND_NS));
  EXPECT_EQ(state.firstRuns.load(), 2);
  EXPECT_EQ(state.besideRuns.load(), 2);
  EXPECT_EQ(state.secondRuns.load(), 2);
  EXPECT_EQ(seq.counter()->load(), 1);
}

INSTANTIATE_TEST_SUITE_P(SkipOnBusy, GroupStillRunning, ::testing::Values(true, false));
