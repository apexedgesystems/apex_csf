/**
 * @file SchedulerShutdown_uTest.cpp
 * @brief Scheduler teardown under sequenced-task hazard conditions.
 *
 * Coverage:
 *  - Shutdown with a phase waiter parked in waitForPhase: a sequenced
 *    task whose earlier phase has not finished parks a pool worker;
 *    teardown must wake it and complete anyway. Witnessed in the field
 *    as a post-shutdown zombie (emergency shutdown completed, process
 *    never exited -- pool join blocked on the parked futex).
 *
 * Teardown is bounded by a deadline instead of letting a regression hang
 * the test binary: shutdown runs on a helper thread and the test asserts
 * completion within the deadline.
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

std::filesystem::path testLogDir() {
  const auto DIR = std::filesystem::temp_directory_path() /
                   ("scheduler_shutdown_utest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// A phase-1 task that stays in its body until released, and a phase-2
/// task that must never run.
struct HeldChain {
  std::atomic<bool> release{false};
  std::atomic<bool> held{false};
  std::atomic<int> waiterRuns{0};

  static std::uint8_t hold(void* raw) noexcept {
    auto* c = static_cast<HeldChain*>(raw);
    c->held.store(true, std::memory_order_release);
    while (!c->release.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
  }
  static std::uint8_t waiter(void* raw) noexcept {
    static_cast<HeldChain*>(raw)->waiterRuns.fetch_add(1, std::memory_order_acq_rel);
    return 0;
  }
};

} // namespace

/**
 * @test Teardown releases a sequenced task parked behind an earlier phase.
 *
 * Phase 1 is inside its body and stays there, so the phase-2 task parks
 * in waitForPhase with no advance coming. Shutdown must wake that waiter
 * itself: the test sees the waiter leave without having run while phase 1
 * is still held, and only then lets phase 1 go.
 */
TEST(SchedulerShutdownTest, TeardownCompletesWithParkedPhaseWaiter) {
  // The chain, its group and its tasks outlive the scheduler, and phase 1
  // is released on every way out of this test, so a failed expectation
  // ends as a failed test.
  HeldChain chain;
  SequenceGroup seq(2);
  SchedulableTask first(DelegateU8{&HeldChain::hold, &chain}, "phase1_held");
  SchedulableTask blocked(DelegateU8{&HeldChain::waiter, &chain}, "phase2_parked");
  seq.addTask(first, 1);
  seq.addTask(blocked, 2);

  system_core::scheduler::PoolSpec spec;
  spec.numThreads = 2;
  auto sched = std::make_unique<SchedulerMultiThread>(
      100, testLogDir(), std::vector<system_core::scheduler::PoolSpec>{spec});
  ASSERT_EQ(sched->init(), 0);
  struct ReleaseAtExit {
    HeldChain& chain;
    ~ReleaseAtExit() { chain.release.store(true, std::memory_order_release); }
  } releaseAtExit{chain};

  ASSERT_EQ(sched->addTask(first, TaskConfig(100, 1, 0, 127), &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(blocked, TaskConfig(100, 1, 0, -128), &seq), Status::SUCCESS);

  // Dispatch: phase 1 enters its body and stays; phase 2 parks behind it.
  (void)sched->executeTasksOnTickMulti(0);
  while (!chain.held.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Let the waiter park.
  ASSERT_TRUE(sched->entries()[1].stillRunning()) << "phase 2 did not wait behind phase 1";
  ASSERT_EQ(chain.waiterRuns.load(), 0);

  // Shutdown from another thread; it cannot return while phase 1 is held.
  std::atomic<bool> closed{false};
  std::thread closer([&sched, &closed]() {
    sched->shutdown();
    closed.store(true, std::memory_order_release);
  });

  // The waiter leaves on shutdown's wake, phase 1 still inside its body.
  bool waiterLeft = false;
  for (int i = 0; i < 3000 && !waiterLeft; ++i) {
    waiterLeft = !sched->entries()[1].stillRunning();
    if (!waiterLeft) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  EXPECT_TRUE(waiterLeft) << "shutdown did not wake the parked phase waiter";
  EXPECT_EQ(chain.waiterRuns.load(), 0) << "an aborted wait must not run its task";

  chain.release.store(true, std::memory_order_release);
  for (int i = 0; i < 3000 && !closed.load(std::memory_order_acquire); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const bool DONE = closed.load(std::memory_order_acquire);

  EXPECT_TRUE(DONE) << "shutdown() blocked on a parked phase waiter";

  if (DONE) {
    closer.join();
    sched.reset();
  } else {
    // Leak deliberately: the destructor would hang the same way, and the
    // helper is still inside shutdown().
    closer.detach();
    [[maybe_unused]] auto* leaked = sched.release();
  }
}
