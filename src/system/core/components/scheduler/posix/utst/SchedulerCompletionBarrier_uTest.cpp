/**
 * @file SchedulerCompletionBarrier_uTest.cpp
 * @brief The in-flight boundary a tick-thread reader depends on.
 *
 * Dispatch is asynchronous: the cycle entry returns once the tasks are
 * enqueued. A caller that reads task outputs on the tick thread -- the
 * executive's action phase does -- therefore needs a point at which no
 * task owns that data, whichever cycle dispatched it.
 *
 * Coverage:
 *  - A task still running holds the wait, and the value it writes is
 *    visible once the wait returns.
 *  - A held task times the wait out within its bound instead of
 *    stalling the tick thread, and the timeout is counted.
 *  - A task overrunning from an earlier cycle still holds the wait on
 *    later cycles, including one that dispatched nothing at all, until
 *    it finishes: a period violation records the overrun, it does not
 *    end the task's ownership.
 *  - A second invocation admitted behind a running first -- a soft mode
 *    that does not skip queues it -- holds the wait after the first
 *    completes, until it too completes.
 *  - With nothing in flight the wait returns at once.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

namespace {

constexpr std::uint64_t ONE_SECOND_NS = 1000000000ULL;
constexpr std::uint64_t TEN_MS_NS = 10000000ULL;

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_barrier_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// A task that waits for a release flag, then publishes a value. It stands
/// in for a plant step whose output the tick thread goes on to read.
struct HeldTask {
  std::atomic<bool> release{false};
  std::atomic<bool> entered{false};
  int output{0}; ///< Plain int on purpose: the wait is what publishes it.

  static std::uint8_t run(void* self) noexcept {
    auto* t = static_cast<HeldTask*>(self);
    t->entered.store(true, std::memory_order_release);
    while (!t->release.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t->output = 0x5A5A;
    return 0;
  }
};

/// A task whose every invocation holds until released individually: the
/// n-th invocation runs once `released` reaches n. Each one publishes its
/// own number.
struct HeldInvocations {
  std::atomic<std::uint32_t> released{0};
  std::atomic<std::uint32_t> started{0};
  std::atomic<std::uint32_t> finished{0};
  int last_output{0}; ///< Plain int on purpose: the wait is what publishes it.

  static std::uint8_t run(void* self) noexcept {
    auto* t = static_cast<HeldInvocations*>(self);
    const std::uint32_t MINE = t->started.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    while (t->released.load(std::memory_order_acquire) < MINE) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t->last_output = static_cast<int>(MINE);
    t->finished.fetch_add(1U, std::memory_order_acq_rel);
    return 0;
  }
};

bool awaitStarted(const HeldInvocations& t, std::uint32_t n) {
  const auto START = std::chrono::steady_clock::now();
  while (t.started.load(std::memory_order_acquire) < n) {
    if (std::chrono::steady_clock::now() - START > std::chrono::seconds(3)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

/// Wait until the pool worker is actually inside the task body, so the
/// test is not racing dispatch itself.
bool awaitEntry(const HeldTask& task) {
  const auto START = std::chrono::steady_clock::now();
  while (!task.entered.load(std::memory_order_acquire)) {
    if (std::chrono::steady_clock::now() - START > std::chrono::seconds(3)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

} // namespace

/**
 * @test The absolute-deadline wait polls once when its instant is already past
 *       and holds until it when it is ahead, answering the same about what is
 *       in flight: a caller that chose a frame's end keeps it across the call,
 *       whatever held the caller between choosing and calling.
 */
TEST(SchedulerCompletionBarrierTest, AnInstantAlreadyPastPollsOnce) {
  auto sched = std::make_unique<SchedulerMultiThread>(100, testLogDir());
  ASSERT_EQ(sched->init(), 0);

  HeldTask held;
  DelegateU8 del{&HeldTask::run, &held};
  SchedulableTask task(del, "held_plant_step");
  ASSERT_EQ(sched->addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);
  (void)sched->executeTasksOnTickMulti(0);
  ASSERT_TRUE(awaitEntry(held));

  // An instant already past: one poll, back at once, the task still owned.
  const std::uint64_t T0 = apex::helpers::cpu::getMonotonicNs();
  EXPECT_FALSE(sched->waitForInFlightTasksUntil(T0 - ONE_SECOND_NS));
  EXPECT_LT(apex::helpers::cpu::getMonotonicNs() - T0, 5'000'000ULL);
  EXPECT_FALSE(sched->waitForInFlightTasksUntil(0U));

  // An instant ahead: the wait asks until it, then gives the same answer.
  const std::uint64_t T1 = apex::helpers::cpu::getMonotonicNs();
  EXPECT_FALSE(sched->waitForInFlightTasksUntil(T1 + TEN_MS_NS));
  EXPECT_GE(apex::helpers::cpu::getMonotonicNs() - T1, TEN_MS_NS);

  held.release.store(true, std::memory_order_release);
  EXPECT_TRUE(
      sched->waitForInFlightTasksUntil(apex::helpers::cpu::getMonotonicNs() + ONE_SECOND_NS));
  EXPECT_EQ(held.output, 0x5A5A);

  sched->shutdown();
}

/**
 * @test The wait holds for a running task and publishes what it wrote.
 */
TEST(SchedulerCompletionBarrierTest, WaitsForRunningTaskAndPublishesItsWrite) {
  auto sched = std::make_unique<SchedulerMultiThread>(100, testLogDir());
  ASSERT_EQ(sched->init(), 0);

  HeldTask held;
  DelegateU8 del{&HeldTask::run, &held};
  SchedulableTask task(del, "held_plant_step");
  ASSERT_EQ(sched->addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  (void)sched->executeTasksOnTickMulti(0);
  ASSERT_TRUE(awaitEntry(held));

  // Still running: the task's data is not the caller's to read yet.
  EXPECT_FALSE(sched->waitForInFlightTasks(TEN_MS_NS));

  held.release.store(true, std::memory_order_release);
  EXPECT_TRUE(sched->waitForInFlightTasks(ONE_SECOND_NS));
  EXPECT_EQ(held.output, 0x5A5A);

  sched->shutdown();
}

/**
 * @test A held task times the wait out within its bound.
 */
TEST(SchedulerCompletionBarrierTest, HeldTaskTimesOutWithinBound) {
  auto sched = std::make_unique<SchedulerMultiThread>(100, testLogDir());
  ASSERT_EQ(sched->init(), 0);

  HeldTask held;
  DelegateU8 del{&HeldTask::run, &held};
  SchedulableTask task(del, "held_forever");
  ASSERT_EQ(sched->addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  (void)sched->executeTasksOnTickMulti(0);
  ASSERT_TRUE(awaitEntry(held));

  const auto START = std::chrono::steady_clock::now();
  EXPECT_FALSE(sched->waitForInFlightTasks(TEN_MS_NS));
  const auto ELAPSED = std::chrono::steady_clock::now() - START;

  // Bounded by its deadline, not by the task: generous upper bound so a
  // loaded box cannot flake it, while a wait that followed the task
  // would never return here.
  EXPECT_LT(ELAPSED, std::chrono::seconds(1));
  EXPECT_EQ(sched->totalInFlightWaitTimeouts(), 1U);

  held.release.store(true, std::memory_order_release);
  EXPECT_TRUE(sched->waitForInFlightTasks(ONE_SECOND_NS));

  sched->shutdown();
}

/**
 * @test A task overrunning from an earlier cycle holds the wait on every
 *       later cycle until it finishes, including one with no dispatches.
 */
TEST(SchedulerCompletionBarrierTest, OverrunFromAnEarlierCycleStillHoldsTheWait) {
  auto sched = std::make_unique<SchedulerMultiThread>(100, testLogDir());
  ASSERT_EQ(sched->init(), 0);
  sched->setSkipOnBusy(true);

  // A 1 Hz task: only tick 0's slot holds it, so every other tick of the
  // second dispatches nothing at all.
  HeldTask held;
  DelegateU8 del{&HeldTask::run, &held};
  SchedulableTask task(del, "overrunning_task");
  ASSERT_EQ(sched->addTask(task, TaskConfig(1, 1, 0)), Status::SUCCESS);

  (void)sched->executeTasksOnTickMulti(0);
  ASSERT_TRUE(awaitEntry(held));
  EXPECT_FALSE(sched->waitForInFlightTasks(TEN_MS_NS)) << "the cycle that dispatched it";

  // Later cycles. Tick 1 dispatches nothing; the worker is still inside
  // the task, so the data is still its own. The wait must say so.
  (void)sched->executeTasksOnTickMulti(1);
  EXPECT_FALSE(sched->waitForInFlightTasks(TEN_MS_NS)) << "a cycle with no dispatches";
  (void)sched->executeTasksOnTickMulti(2);
  EXPECT_FALSE(sched->waitForInFlightTasks(TEN_MS_NS)) << "another";
  EXPECT_EQ(sched->totalInFlightWaitTimeouts(), 3U);

  // Only the task's completion ends its ownership.
  held.release.store(true, std::memory_order_release);
  EXPECT_TRUE(sched->waitForInFlightTasks(ONE_SECOND_NS));
  EXPECT_EQ(held.output, 0x5A5A);

  sched->shutdown();
}

namespace {

/// A scheduler with exactly `workers` workers in its one pool, so a test
/// decides whether a second invocation queues behind the first or runs
/// beside it.
std::unique_ptr<SchedulerMultiThread> schedulerWithWorkers(std::size_t workers) {
  std::vector<system_core::scheduler::PoolSpec> pools(1);
  pools[0].name = "default";
  pools[0].numThreads = workers;
  return std::make_unique<SchedulerMultiThread>(100, testLogDir(), std::move(pools));
}

/// The successor scenario: invocation 1 holds, invocation 2 is admitted
/// behind it (the default policy admits a task still running at its next
/// dispatch rather than skipping it), the first is released, and the
/// wait must hold until the second completes too.
void successorHoldsTheWait(std::size_t workers, bool successorRunsBeside) {
  auto sched = schedulerWithWorkers(workers);
  ASSERT_EQ(sched->init(), 0);

  HeldInvocations held;
  DelegateU8 del{&HeldInvocations::run, &held};
  SchedulableTask task(del, "held_twice");
  ASSERT_EQ(sched->addTask(task, TaskConfig(100, 1, 0)), Status::SUCCESS);

  (void)sched->executeTasksOnTickMulti(0); // Invocation 1 runs and holds.
  ASSERT_TRUE(awaitStarted(held, 1U));
  (void)sched->executeTasksOnTickMulti(1); // Invocation 2 is admitted.
  EXPECT_TRUE(sched->hasPeriodViolation());
  EXPECT_EQ(sched->entries()[0].inFlightCount(), 2U);
  if (successorRunsBeside) {
    ASSERT_TRUE(awaitStarted(held, 2U)); // A second worker runs it at once.
  }
  EXPECT_FALSE(sched->waitForInFlightTasks(TEN_MS_NS)) << "both invocations outstanding";

  // Release the first. It completes; the second is still outstanding --
  // queued behind it on one worker, or running beside it on two. The
  // entry is still in flight: its second invocation owns the data.
  held.released.store(1U, std::memory_order_release);
  ASSERT_TRUE(awaitStarted(held, 2U));
  const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (held.finished.load(std::memory_order_acquire) < 1U &&
         std::chrono::steady_clock::now() < DEADLINE) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  EXPECT_EQ(held.finished.load(std::memory_order_acquire), 1U);
  EXPECT_FALSE(sched->waitForInFlightTasks(TEN_MS_NS)) << "the second invocation is outstanding";
  EXPECT_EQ(sched->entries()[0].inFlightCount(), 1U);

  // Only the second's completion ends the entry's ownership, and its
  // write is what the wait then publishes.
  held.released.store(2U, std::memory_order_release);
  EXPECT_TRUE(sched->waitForInFlightTasks(ONE_SECOND_NS));
  EXPECT_EQ(held.finished.load(std::memory_order_acquire), 2U);
  EXPECT_EQ(held.last_output, 2);
  EXPECT_EQ(sched->entries()[0].inFlightCount(), 0U);

  sched->shutdown();
}

} // namespace

/**
 * @test A second invocation queued behind a running first, on one worker,
 *       holds the wait after the first completes, until it completes too.
 */
TEST(SchedulerCompletionBarrierTest, QueuedSuccessorHoldsTheWaitAfterTheFirstCompletes) {
  successorHoldsTheWait(1U, false);
}

/**
 * @test A second invocation running beside the first, on two workers,
 *       holds the wait after the first completes, until it completes too.
 */
TEST(SchedulerCompletionBarrierTest, RunningSuccessorHoldsTheWaitAfterTheFirstCompletes) {
  successorHoldsTheWait(2U, true);
}

/**
 * @test With nothing in flight the wait returns at once.
 */
TEST(SchedulerCompletionBarrierTest, NothingInFlightCompletesImmediately) {
  auto sched = std::make_unique<SchedulerMultiThread>(100, testLogDir());
  ASSERT_EQ(sched->init(), 0);

  (void)sched->executeTasksOnTickMulti(0);
  EXPECT_TRUE(sched->waitForInFlightTasks(0U));
  EXPECT_EQ(sched->totalInFlightWaitTimeouts(), 0U);

  sched->shutdown();
}
