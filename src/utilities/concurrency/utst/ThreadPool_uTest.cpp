/**
 * @file ThreadPool_uTest.cpp
 * @brief Unit tests for concurrency::ThreadPool.
 *
 * Notes:
 *  - Tests verify lifecycle, task execution, and error handling.
 *  - Thread timing tests use reasonable tolerances for CI stability.
 */

#include "src/utilities/concurrency/inc/ThreadPool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using apex::concurrency::DelegateU8;
using apex::concurrency::PoolStatus;
using apex::concurrency::TaskError;
using apex::concurrency::ThreadPool;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default TaskError has null label and zero error code */
TEST(TaskErrorDefaultTest, DefaultIsZero) {
  const TaskError DEFAULT{};
  EXPECT_EQ(DEFAULT.label, nullptr);
  EXPECT_EQ(DEFAULT.errorCode, 0);
  EXPECT_EQ(DEFAULT.status, PoolStatus::SUCCESS);
}

/* ----------------------------- ThreadPool Lifecycle Tests ----------------------------- */

class ThreadPoolTest : public ::testing::Test {
protected:
  static constexpr std::size_t THREAD_COUNT = 4;
};

/** @test ThreadPool can be constructed with specified thread count */
TEST_F(ThreadPoolTest, ConstructsWithThreadCount) {
  ThreadPool pool(THREAD_COUNT);
  // Pool should be alive after construction
  EXPECT_FALSE(pool.threadsRunning());
}

/** @test ThreadPool isValid returns true after successful construction */
TEST_F(ThreadPoolTest, IsValidAfterConstruction) {
  ThreadPool pool(THREAD_COUNT);
  EXPECT_TRUE(pool.isValid());
  EXPECT_EQ(pool.workerCount(), THREAD_COUNT);
}

/** @test ThreadPool tryEnqueue returns SUCCESS when pool is running */
TEST_F(ThreadPoolTest, TryEnqueueReturnsSuccess) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  const PoolStatus STATUS = pool.tryEnqueue("task", task);
  EXPECT_EQ(STATUS, PoolStatus::SUCCESS);
  pool.shutdown();
  EXPECT_EQ(counter.load(), 1);
}

/** @test ThreadPool tryEnqueue returns POOL_STOPPED after shutdown */
TEST_F(ThreadPoolTest, TryEnqueueReturnsPoolStopped) {
  ThreadPool pool(2);
  pool.shutdown();

  auto task = DelegateU8{[](void*) noexcept -> std::uint8_t { return 0; }, nullptr};
  const PoolStatus STATUS = pool.tryEnqueue("task", task);
  EXPECT_EQ(STATUS, PoolStatus::POOL_STOPPED);
}

/** @test ThreadPool destructor calls shutdown automatically */
TEST_F(ThreadPoolTest, DestructorCallsShutdown) {
  {
    ThreadPool pool(2);
    // Pool goes out of scope, destructor should not hang
  }
  SUCCEED();
}

/** @test ThreadPool shutdown is idempotent */
TEST_F(ThreadPoolTest, ShutdownIsIdempotent) {
  ThreadPool pool(2);
  pool.shutdown();
  pool.shutdown(); // Second call should not hang or crash
  SUCCEED();
}

/* ----------------------------- Task Execution Tests ----------------------------- */

/** @test ThreadPool executes a single task successfully */
TEST_F(ThreadPoolTest, ExecutesSingleTask) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  pool.enqueueTask("increment", task);
  pool.shutdown();

  EXPECT_EQ(counter.load(), 1);
}

/** @test ThreadPool executes multiple tasks */
TEST_F(ThreadPoolTest, ExecutesMultipleTasks) {
  ThreadPool pool(THREAD_COUNT);
  std::atomic<int> counter{0};
  constexpr int TASK_COUNT = 100;

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  for (int i = 0; i < TASK_COUNT; ++i) {
    pool.enqueueTask("increment", task);
  }
  pool.shutdown();

  EXPECT_EQ(counter.load(), TASK_COUNT);
}

/** @test ThreadPool reports threads running during task execution */
TEST_F(ThreadPoolTest, ThreadsRunningDuringExecution) {
  ThreadPool pool(1);

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

  pool.enqueueTask("blocking", task);

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

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test ThreadPool collects errors from failed tasks */
TEST_F(ThreadPoolTest, CollectsTaskErrors) {
  ThreadPool pool(2);

  auto failingTask = DelegateU8{[](void*) noexcept -> std::uint8_t { return 42; }, nullptr};

  pool.enqueueTask("failing-task", failingTask);
  pool.shutdown();

  auto errors = pool.collectErrors();
  ASSERT_EQ(errors.size(), 1U);

  const TaskError& ERR = errors.front();
  EXPECT_STREQ(ERR.label, "failing-task");
  EXPECT_EQ(ERR.errorCode, 42);
  EXPECT_EQ(ERR.status, PoolStatus::TASK_FAILED);
}

/** @test ThreadPool collectErrors clears the error queue */
TEST_F(ThreadPoolTest, CollectErrorsClearsQueue) {
  ThreadPool pool(1);

  auto failingTask = DelegateU8{[](void*) noexcept -> std::uint8_t { return 1; }, nullptr};

  pool.enqueueTask("fail", failingTask);
  pool.shutdown();

  auto errors1 = pool.collectErrors();
  EXPECT_EQ(errors1.size(), 1U);

  auto errors2 = pool.collectErrors();
  EXPECT_TRUE(errors2.empty());
}

/** @test ThreadPool ignores null delegate tasks */
TEST_F(ThreadPoolTest, IgnoresNullDelegate) {
  ThreadPool pool(1);

  DelegateU8 nullTask{nullptr, nullptr};
  pool.enqueueTask("null-task", nullTask);
  pool.shutdown();

  auto errors = pool.collectErrors();
  EXPECT_TRUE(errors.empty());
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test ThreadPool threadsRunning returns consistent results */
TEST(ThreadPoolDeterminismTest, ThreadsRunningConsistent) {
  ThreadPool pool(2);
  const bool FIRST = pool.threadsRunning();
  const bool SECOND = pool.threadsRunning();
  EXPECT_EQ(FIRST, SECOND);
}
