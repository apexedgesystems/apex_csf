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
#include <csignal>
#include <thread>

#include <pthread.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

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

/* ----------------------------- Lifecycle Contract Tests ----------------------------- */

namespace {

/// Whether `sig` is blocked on the calling thread.
bool blockedHere(int sig) {
  sigset_t mask;
  sigemptyset(&mask);
  pthread_sigmask(SIG_BLOCK, nullptr, &mask);
  return sigismember(&mask, sig) == 1;
}

/// Clear the calling thread's mask, so the test starts from a known state whatever
/// earlier tests in this process left behind.
void unblockAllHere() {
  sigset_t none;
  sigemptyset(&none);
  pthread_sigmask(SIG_SETMASK, &none, nullptr);
}

} // namespace

/** @test Constructing and destroying a pool leaves the constructing thread's signal mask as it
 * was: the workers inherit the blocked set, the caller does not keep it. */
TEST_F(ThreadPoolTest, ConstructorRestoresCallerSignalMask) {
  bool blockedWhileAlive = false;
  bool blockedAfter = false;
  std::thread caller([&]() {
    unblockAllHere();
    {
      ThreadPool pool(2);
      blockedWhileAlive = blockedHere(SIGINT) || blockedHere(SIGTERM) || blockedHere(SIGPIPE) ||
                          blockedHere(SIGSEGV);
    }
    blockedAfter =
        blockedHere(SIGINT) || blockedHere(SIGTERM) || blockedHere(SIGPIPE) || blockedHere(SIGSEGV);
  });
  caller.join();
  EXPECT_FALSE(blockedWhileAlive) << "the pool's worker set leaked onto the constructing thread";
  EXPECT_FALSE(blockedAfter) << "the constructing thread's mask stayed changed after the pool";
}

namespace {

/// An affinity naming a CPU no host has, so applying it fails for every user; a requested RT
/// policy would fail only without CAP_SYS_NICE.
apex::concurrency::PoolConfig impossibleAffinity() {
  apex::concurrency::PoolConfig config;
  config.affinity = {255};
  return config;
}

} // namespace

/** @test A worker whose configuration cannot be applied is counted, so the owner can tell the
 * pool is not running as configured. */
TEST_F(ThreadPoolTest, ReportsWorkerConfigFailure) {
  ThreadPool configured(2);
  EXPECT_EQ(configured.workerConfigFailures(), 0U);
  ThreadPool unconfigured(2, impossibleAffinity());
  EXPECT_EQ(unconfigured.workerConfigFailures(), unconfigured.workerCount());
  EXPECT_EQ(unconfigured.workerCount(), 2U);
}

/** @test A worker that cannot start leaves an invalid pool and the caller's mask untouched: the
 * child lowers its thread limit to one so thread creation fails (the limit is not enforced for
 * root, where the case is skipped). */
TEST_F(ThreadPoolTest, WorkerStartFailureLeavesPoolInvalid) {
  if (::geteuid() == 0) {
    GTEST_SKIP() << "RLIMIT_NPROC is not enforced for root";
  }
  const pid_t CHILD = ::fork();
  ASSERT_NE(CHILD, -1);
  if (CHILD == 0) {
    const rlimit ONE{1, 1};
    if (::setrlimit(RLIMIT_NPROC, &ONE) != 0) {
      ::_exit(40);
    }
    unblockAllHere();
    int code = 0;
    try {
      ThreadPool pool(2);
      if (pool.isValid() || pool.workerCount() != 0) {
        code = 10; // the pool claims workers that could not start
      }
    } catch (...) {
      code = 20; // the constructor threw instead of reporting an invalid pool
    }
    if (blockedHere(SIGINT) || blockedHere(SIGTERM) || blockedHere(SIGPIPE)) {
      code += 1; // the caller's mask was left blocked
    }
    ::_exit(code);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(CHILD, &status, 0), CHILD);
  ASSERT_TRUE(WIFEXITED(status));
  if (WEXITSTATUS(status) == 40) {
    GTEST_SKIP() << "the thread limit could not be lowered";
  }
  EXPECT_EQ(WEXITSTATUS(status), 0)
      << "10: pool valid, 20: constructor threw, +1: mask left blocked";
}
