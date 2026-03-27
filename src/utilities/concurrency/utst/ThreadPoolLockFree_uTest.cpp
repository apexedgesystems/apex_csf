/**
 * @file ThreadPoolLockFree_uTest.cpp
 * @brief Unit tests for concurrency::ThreadPoolLockFree.
 *
 * Notes:
 *  - Tests verify lifecycle, task execution, bounded queue behavior, and error handling.
 *  - Thread timing tests use reasonable tolerances for CI stability.
 */

#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using apex::concurrency::DelegateU8;
using apex::concurrency::PoolStatus;
using apex::concurrency::TaskError;
using apex::concurrency::ThreadPoolLockFree;

/* ----------------------------- ThreadPoolLockFree Lifecycle Tests ----------------------------- */

class ThreadPoolLockFreeTest : public ::testing::Test {
protected:
  static constexpr std::size_t THREAD_COUNT = 4;
  static constexpr std::size_t QUEUE_CAPACITY = 64;
};

/** @test ThreadPoolLockFree can be constructed with specified thread count and capacity */
TEST_F(ThreadPoolLockFreeTest, ConstructsWithThreadCountAndCapacity) {
  ThreadPoolLockFree pool(THREAD_COUNT, QUEUE_CAPACITY);
  EXPECT_TRUE(pool.isValid());
  EXPECT_EQ(pool.workerCount(), THREAD_COUNT);
  EXPECT_GE(pool.capacity(), QUEUE_CAPACITY); // May be rounded to power-of-two
  EXPECT_FALSE(pool.threadsRunning());
}

/** @test ThreadPoolLockFree destructor calls shutdown automatically */
TEST_F(ThreadPoolLockFreeTest, DestructorCallsShutdown) {
  {
    ThreadPoolLockFree pool(2, 32);
    // Pool goes out of scope, destructor should not hang
  }
  SUCCEED();
}

/** @test ThreadPoolLockFree shutdown is idempotent */
TEST_F(ThreadPoolLockFreeTest, ShutdownIsIdempotent) {
  ThreadPoolLockFree pool(2, 32);
  pool.shutdown();
  pool.shutdown(); // Second call should not hang or crash
  SUCCEED();
}

/* ----------------------------- Task Execution Tests ----------------------------- */

/** @test ThreadPoolLockFree executes a single task successfully */
TEST_F(ThreadPoolLockFreeTest, ExecutesSingleTask) {
  ThreadPoolLockFree pool(2, 32);
  std::atomic<int> counter{0};

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  const PoolStatus STATUS = pool.tryEnqueue("increment", task);
  EXPECT_EQ(STATUS, PoolStatus::SUCCESS);
  pool.shutdown();

  EXPECT_EQ(counter.load(), 1);
}

/** @test ThreadPoolLockFree executes multiple tasks */
TEST_F(ThreadPoolLockFreeTest, ExecutesMultipleTasks) {
  ThreadPoolLockFree pool(THREAD_COUNT, 256);
  std::atomic<int> counter{0};
  constexpr int TASK_COUNT = 100;

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  for (int i = 0; i < TASK_COUNT; ++i) {
    const PoolStatus STATUS = pool.tryEnqueue("increment", task);
    EXPECT_EQ(STATUS, PoolStatus::SUCCESS);
  }
  pool.shutdown();

  EXPECT_EQ(counter.load(), TASK_COUNT);
}

/** @test ThreadPoolLockFree reports threads running during task execution */
TEST_F(ThreadPoolLockFreeTest, ThreadsRunningDuringExecution) {
  ThreadPoolLockFree pool(1, 32);

  struct Context {
    std::atomic<bool> taskStarted{false};
    std::atomic<bool> canFinish{false};
  };
  Context ctx{};

  auto task = DelegateU8{[](void* c) noexcept -> std::uint8_t {
                           auto* x = static_cast<Context*>(c);
                           x->taskStarted.store(true, std::memory_order_release);
                           while (!x->canFinish.load(std::memory_order_acquire)) {
                             std::this_thread::yield();
                           }
                           return 0;
                         },
                         &ctx};

  (void)pool.tryEnqueue("blocking", task);

  // Wait for task to start
  while (!ctx.taskStarted.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }

  EXPECT_TRUE(pool.threadsRunning());

  // Allow task to finish
  ctx.canFinish.store(true, std::memory_order_release);
  pool.shutdown();

  EXPECT_FALSE(pool.threadsRunning());
}

/* ----------------------------- Bounded Queue Tests ----------------------------- */

/** @test ThreadPoolLockFree returns QUEUE_FULL when queue is full */
TEST_F(ThreadPoolLockFreeTest, ReturnsQueueFullWhenFull) {
  // Small capacity, single slow worker
  ThreadPoolLockFree pool(1, 4);

  struct Context {
    std::atomic<bool> canFinish{false};
  };
  Context ctx{};

  // Task that blocks until signaled
  auto blockingTask = DelegateU8{[](void* c) noexcept -> std::uint8_t {
                                   auto* x = static_cast<Context*>(c);
                                   while (!x->canFinish.load(std::memory_order_acquire)) {
                                     std::this_thread::yield();
                                   }
                                   return 0;
                                 },
                                 &ctx};

  // Fill the queue (capacity is rounded to power-of-two, so may be 4 or 8)
  const std::size_t CAP = pool.capacity();
  std::size_t enqueued = 0;
  for (std::size_t i = 0; i < CAP + 10; ++i) {
    PoolStatus status = pool.tryEnqueue("blocking", blockingTask);
    if (status == PoolStatus::SUCCESS) {
      ++enqueued;
    } else if (status == PoolStatus::QUEUE_FULL) {
      // Expected once queue is full
      break;
    }
  }

  // Should have hit QUEUE_FULL
  EXPECT_LE(enqueued, CAP + 1); // +1 for task being executed

  // Release blocked tasks
  ctx.canFinish.store(true, std::memory_order_release);
  pool.shutdown();
}

/** @test ThreadPoolLockFree returns POOL_STOPPED after shutdown */
TEST_F(ThreadPoolLockFreeTest, ReturnsPoolStoppedAfterShutdown) {
  ThreadPoolLockFree pool(2, 32);
  pool.shutdown();

  auto task = DelegateU8{[](void*) noexcept -> std::uint8_t { return 0; }, nullptr};
  const PoolStatus STATUS = pool.tryEnqueue("task", task);
  EXPECT_EQ(STATUS, PoolStatus::POOL_STOPPED);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test ThreadPoolLockFree collects errors from failed tasks */
TEST_F(ThreadPoolLockFreeTest, CollectsTaskErrors) {
  ThreadPoolLockFree pool(2, 32);

  auto failingTask = DelegateU8{[](void*) noexcept -> std::uint8_t { return 42; }, nullptr};

  (void)pool.tryEnqueue("failing-task", failingTask);
  pool.shutdown();

  EXPECT_TRUE(pool.hasErrors());
  auto errors = pool.collectErrors();
  ASSERT_EQ(errors.size(), 1U);

  const TaskError& ERR = errors.front();
  EXPECT_STREQ(ERR.label, "failing-task");
  EXPECT_EQ(ERR.errorCode, 42);
  EXPECT_EQ(ERR.status, PoolStatus::TASK_FAILED);
}

/** @test ThreadPoolLockFree collectErrors clears the error queue */
TEST_F(ThreadPoolLockFreeTest, CollectErrorsClearsQueue) {
  ThreadPoolLockFree pool(1, 32);

  auto failingTask = DelegateU8{[](void*) noexcept -> std::uint8_t { return 1; }, nullptr};

  (void)pool.tryEnqueue("fail", failingTask);
  pool.shutdown();

  auto errors1 = pool.collectErrors();
  EXPECT_EQ(errors1.size(), 1U);

  auto errors2 = pool.collectErrors();
  EXPECT_TRUE(errors2.empty());
  EXPECT_FALSE(pool.hasErrors());
}

/** @test ThreadPoolLockFree ignores null delegate tasks */
TEST_F(ThreadPoolLockFreeTest, IgnoresNullDelegate) {
  ThreadPoolLockFree pool(1, 32);

  DelegateU8 nullTask{nullptr, nullptr};
  (void)pool.tryEnqueue("null-task", nullTask);
  pool.shutdown();

  auto errors = pool.collectErrors();
  EXPECT_TRUE(errors.empty());
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test ThreadPoolLockFree threadsRunning returns consistent results */
TEST(ThreadPoolLockFreeDeterminismTest, ThreadsRunningConsistent) {
  ThreadPoolLockFree pool(2, 32);
  const bool FIRST = pool.threadsRunning();
  const bool SECOND = pool.threadsRunning();
  EXPECT_EQ(FIRST, SECOND);
}

/** @test ThreadPoolLockFree hasErrors returns consistent results when no errors */
TEST(ThreadPoolLockFreeDeterminismTest, HasErrorsConsistent) {
  ThreadPoolLockFree pool(2, 32);
  const bool FIRST = pool.hasErrors();
  const bool SECOND = pool.hasErrors();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_FALSE(FIRST);
}
