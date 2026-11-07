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
#include <csignal>
#include <cstdint>
#include <thread>

#include <pthread.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

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

/* ----------------------------- Lifecycle Contract Tests ----------------------------- */

namespace {

/// Whether `sig` is blocked on the calling thread.
bool blockedHere(int sig) {
  sigset_t mask;
  sigemptyset(&mask);
  pthread_sigmask(SIG_BLOCK, nullptr, &mask);
  return sigismember(&mask, sig) == 1;
}

/// Clear the calling thread's mask, so the test starts from a known state.
void unblockAllHere() {
  sigset_t none;
  sigemptyset(&none);
  pthread_sigmask(SIG_SETMASK, &none, nullptr);
}

/// A worker's view of its own mask, written by the task that runs there.
struct WorkerMask {
  std::atomic<bool> done{false};
  bool sigint{false};
  bool sigterm{false};
  bool sigpipe{false};
  bool sigsegv{false};
};

std::uint8_t recordWorkerMask(void* ctx) noexcept {
  auto* m = static_cast<WorkerMask*>(ctx);
  m->sigint = blockedHere(SIGINT);
  m->sigterm = blockedHere(SIGTERM);
  m->sigpipe = blockedHere(SIGPIPE);
  m->sigsegv = blockedHere(SIGSEGV);
  m->done.store(true, std::memory_order_release);
  return 0;
}

std::uint8_t countRun(void* ctx) noexcept {
  static_cast<std::atomic<std::uint32_t>*>(ctx)->fetch_add(1, std::memory_order_acq_rel);
  return 0;
}

} // namespace

/** @test Constructing and destroying a pool leaves the constructing thread's signal mask as it
 * was: the workers inherit the blocked set, the caller does not keep it. */
TEST_F(ThreadPoolLockFreeTest, ConstructorRestoresCallerSignalMask) {
  bool blockedWhileAlive = false;
  bool blockedAfter = false;
  std::thread caller([&]() {
    unblockAllHere();
    {
      ThreadPoolLockFree pool(2, 32);
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

/** @test The workers block the process signals (SIGINT, SIGTERM, SIGPIPE) so those stay with the
 * thread that owns them, and do not block SIGSEGV, a synchronous fault whose handler must run. */
TEST_F(ThreadPoolLockFreeTest, WorkersBlockProcessSignalsOnly) {
  WorkerMask mask;
  std::thread caller([&]() {
    unblockAllHere();
    ThreadPoolLockFree pool(1, 8);
    ASSERT_EQ(pool.tryEnqueue("mask", {&recordWorkerMask, &mask}), PoolStatus::SUCCESS);
    while (!mask.done.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });
  caller.join();
  EXPECT_TRUE(mask.sigint);
  EXPECT_TRUE(mask.sigterm);
  EXPECT_TRUE(mask.sigpipe);
  EXPECT_FALSE(mask.sigsegv) << "a blocked SIGSEGV only bypasses an installed crash handler";
}

/** @test Every enqueue reported as SUCCESS runs, including one racing shutdown(): a producer that
 * passed the stop check must not have its task stranded in the ring by the workers leaving. */
TEST_F(ThreadPoolLockFreeTest, EverySuccessfulEnqueueRunsAcrossShutdown) {
  constexpr int LIFETIMES = 1000;
  for (int lifetime = 0; lifetime < LIFETIMES; ++lifetime) {
    std::atomic<std::uint32_t> executed{0};
    std::uint32_t accepted = 0;
    ThreadPoolLockFree pool(2, 64);
    std::thread producer([&]() {
      for (;;) {
        const PoolStatus STATUS = pool.tryEnqueue("count", {&countRun, &executed});
        if (STATUS == PoolStatus::POOL_STOPPED) {
          return;
        }
        if (STATUS == PoolStatus::SUCCESS) {
          ++accepted;
        }
      }
    });
    std::this_thread::yield();
    pool.shutdown();
    producer.join();
    ASSERT_EQ(executed.load(std::memory_order_acquire), accepted) << "lifetime " << lifetime;
  }
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
TEST_F(ThreadPoolLockFreeTest, ReportsWorkerConfigFailure) {
  ThreadPoolLockFree configured(2, 8);
  EXPECT_EQ(configured.workerConfigFailures(), 0U);
  ThreadPoolLockFree unconfigured(2, 8, impossibleAffinity());
  EXPECT_EQ(unconfigured.workerConfigFailures(), unconfigured.workerCount());
  EXPECT_EQ(unconfigured.workerCount(), 2U);
}

/** @test A worker that cannot start leaves an invalid pool and the caller's mask untouched: the
 * child lowers its thread limit to one so thread creation fails (the limit is not enforced for
 * root, where the case is skipped). */
TEST_F(ThreadPoolLockFreeTest, WorkerStartFailureLeavesPoolInvalid) {
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
      ThreadPoolLockFree pool(2, 8);
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
