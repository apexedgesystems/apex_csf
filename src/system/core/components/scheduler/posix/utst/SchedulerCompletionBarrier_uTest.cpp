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
 *  - With nothing in flight the wait returns at once.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
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
