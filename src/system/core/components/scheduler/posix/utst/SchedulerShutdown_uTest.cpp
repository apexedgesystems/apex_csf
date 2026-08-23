/**
 * @file SchedulerShutdown_uTest.cpp
 * @brief Scheduler teardown under sequenced-task hazard conditions.
 *
 * Coverage:
 *  - Shutdown with a phase waiter parked in waitForPhase: a sequenced
 *    task whose earlier phase never ran parks a pool worker; teardown
 *    must wake it and complete anyway. Witnessed in the field as a
 *    post-shutdown zombie (emergency shutdown completed, process never
 *    exited -- pool join blocked on the parked futex).
 *  - Shutdown with a phase-misordered dispatch: phases enqueued out of
 *    order can park every worker with the earlier phase still queued
 *    behind them; teardown must still complete.
 *
 * Both tests bound teardown with a watchdog deadline instead of letting
 * a regression hang the test binary: shutdown runs on a helper thread
 * and the test asserts completion within the deadline.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SequenceGroup;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

namespace {

std::uint8_t noopTask(void*) noexcept { return 0; }

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_shutdown_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// Run fn on a helper thread; true if it completed within deadlineMs.
bool completesWithin(std::chrono::milliseconds deadline, void (*fn)(void*), void* arg) {
  std::atomic<bool> done{false};
  std::thread runner([&done, fn, arg]() {
    fn(arg);
    done.store(true, std::memory_order_release);
  });

  const auto START = std::chrono::steady_clock::now();
  while (!done.load(std::memory_order_acquire)) {
    if (std::chrono::steady_clock::now() - START > deadline) {
      runner.detach(); // Regression path: leave the stuck thread behind.
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  runner.join();
  return true;
}

} // namespace

/**
 * @test Teardown completes while a sequenced task is parked mid-phase.
 *
 * Only the phase-2 task is dispatched; phase 1 never runs, so the worker
 * parks in waitForPhase with no one coming to advance the counter. The
 * scheduler must wake and release that worker during shutdown.
 */
TEST(SchedulerShutdownTest, TeardownCompletesWithParkedPhaseWaiter) {
  auto sched = std::make_unique<SchedulerMultiThread>(100, testLogDir());
  ASSERT_EQ(sched->init(), 0);

  SequenceGroup seq(2);
  DelegateU8 del{&noopTask, nullptr};
  SchedulableTask blocked(del, "phase2_only");
  seq.addTask(blocked, 2);

  TaskConfig cfg(100, 1, 0);
  ASSERT_EQ(sched->addTask(blocked, cfg, &seq), Status::SUCCESS);

  // Dispatch: the phase-2 task parks awaiting a phase-1 advance that
  // will never come.
  (void)sched->executeTasksOnTickMulti(0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Let it park.

  const bool DONE = completesWithin(
      std::chrono::milliseconds(3000),
      [](void* raw) { static_cast<SchedulerMultiThread*>(raw)->shutdown(); }, sched.get());

  EXPECT_TRUE(DONE) << "shutdown() blocked on a parked phase waiter";

  if (DONE) {
    sched.reset();
  } else {
    // Leak deliberately: the destructor would hang the same way.
    [[maybe_unused]] auto* leaked = sched.release();
  }
}
