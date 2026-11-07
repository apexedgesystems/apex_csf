/**
 * @file ThreadPoolLockFree_uTest.cpp
 * @brief Unit tests for concurrency::ThreadPoolLockFree.
 *
 * Notes:
 *  - Tests verify lifecycle, task execution, bounded queue behavior, and error handling.
 *  - Thread timing tests use reasonable tolerances for CI stability.
 */

#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"
#include "src/utilities/concurrency/utst/support/ThreadStartFault.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <pthread.h>

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

/// Block `sig` on the calling thread.
void blockHere(int sig) {
  sigset_t one;
  sigemptyset(&one);
  sigaddset(&one, sig);
  pthread_sigmask(SIG_BLOCK, &one, nullptr);
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

/** @test Every enqueue reported as SUCCESS runs, including those racing shutdown(): two producers
 * that passed the admission check must not have a task stranded in the ring by the workers
 * leaving, whichever of them publishes last. */
TEST_F(ThreadPoolLockFreeTest, EverySuccessfulEnqueueRunsAcrossShutdown) {
  constexpr int LIFETIMES = 1000;
  for (int lifetime = 0; lifetime < LIFETIMES; ++lifetime) {
    std::atomic<std::uint32_t> executed{0};
    std::atomic<std::uint32_t> accepted{0};
    ThreadPoolLockFree pool(2, 64);
    const auto PRODUCE = [&]() {
      for (;;) {
        const PoolStatus STATUS = pool.tryEnqueue("count", {&countRun, &executed});
        if (STATUS == PoolStatus::POOL_STOPPED) {
          return;
        }
        if (STATUS == PoolStatus::SUCCESS) {
          accepted.fetch_add(1, std::memory_order_acq_rel);
        }
      }
    };
    std::thread producerA(PRODUCE);
    std::thread producerB(PRODUCE);
    std::this_thread::yield();
    pool.shutdown();
    producerA.join();
    producerB.join();
    ASSERT_EQ(executed.load(std::memory_order_acquire), accepted.load(std::memory_order_acquire))
        << "lifetime " << lifetime;
  }
}

namespace apex {
namespace concurrency {

/// The white-box seams the pool's header befriends: the ring and permit states the shutdown
/// protocol has to survive, built directly rather than hoped for from a racing schedule.
struct ThreadPoolLockFreeTestAccess {
  /// A published task with no permit for it: the state left when a worker consumed the permit
  /// on a pop that found a cell ahead still being published.
  static bool pushWithoutPermit(ThreadPoolLockFree& pool, const char* label, DelegateU8 task) {
    return pool.taskQueue_.tryPush(ThreadPoolLockFree::TaskItem{label, task});
  }
  /// A permit with nothing published for it.
  static void releasePermit(ThreadPoolLockFree& pool) { pool.wakeupSem_.release(); }
  /// A producer past the admission check that has not published yet.
  static void enterAdmission(ThreadPoolLockFree& pool) {
    pool.admission_.value.fetch_add(1, std::memory_order_acq_rel);
  }
  /// That producer leaving, after its push and permit.
  static void leaveAdmission(ThreadPoolLockFree& pool) { pool.leaveAdmission(); }
};

} // namespace concurrency
} // namespace apex

namespace {

using Access = apex::concurrency::ThreadPoolLockFreeTestAccess;

/// Poll `done` until it holds or `limit` passes.
template <class Pred> bool settles(Pred done, std::chrono::milliseconds limit) {
  const auto DEADLINE = std::chrono::steady_clock::now() + limit;
  while (!done()) {
    if (std::chrono::steady_clock::now() > DEADLINE) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

constexpr std::chrono::milliseconds BOUND{5000};

} // namespace

/** @test A task left in the ring with its permit consumed runs on the next permit, together with
 * the task that permit announces: a woken worker drains the ring rather than taking one task per
 * permit, so a permit spent on a pop that found an unpublished cell ahead strands nothing. */
TEST_F(ThreadPoolLockFreeTest, TaskBehindAConsumedPermitRunsOnTheNextWake) {
  std::atomic<std::uint32_t> executed{0};
  ThreadPoolLockFree pool(1, 8);
  ASSERT_TRUE(Access::pushWithoutPermit(pool, "stranded", {&countRun, &executed}));
  ASSERT_EQ(pool.tryEnqueue("next", {&countRun, &executed}), PoolStatus::SUCCESS);
  EXPECT_TRUE(settles([&]() { return executed.load(std::memory_order_acquire) == 2; }, BOUND))
      << "ran " << executed.load()
      << " of 2: the stranded task waits for a permit that never comes";
}

/** @test shutdown() runs a task whose permit was consumed, and returns: a worker's exit needs the
 * ring seen empty after the quiescent mark, not a permit per task, so the permit shutdown()
 * releases per worker is not spent on the task. */
TEST_F(ThreadPoolLockFreeTest, ShutdownRunsTheTaskBehindAConsumedPermit) {
  std::atomic<std::uint32_t> executed{0};
  auto pool = std::make_unique<ThreadPoolLockFree>(1, 8);
  ASSERT_TRUE(Access::pushWithoutPermit(*pool, "stranded", {&countRun, &executed}));
  std::atomic<bool> returned{false};
  std::thread stopper([&]() {
    pool->shutdown();
    returned.store(true, std::memory_order_release);
  });
  const bool BOUNDED = settles([&]() { return returned.load(std::memory_order_acquire); }, BOUND);
  EXPECT_TRUE(BOUNDED)
      << "shutdown() did not return: a worker is parked for a permit that never comes";
  if (!BOUNDED) {
    stopper.detach();
    (void)pool.release(); // a hung shutdown keeps its pool; the suite goes on
    return;
  }
  stopper.join();
  EXPECT_EQ(executed.load(std::memory_order_acquire), 1U) << "the accepted task did not run";
  EXPECT_EQ(pool->workerCount(), 0U);
}

/** @test With admission closed while a producer is still inside tryEnqueue(), a worker woken to an
 * empty ring parks again rather than exiting, and shutdown() parks until that producer has left;
 * the task the producer then publishes runs before shutdown() returns. */
TEST_F(ThreadPoolLockFreeTest, ShutdownWaitsForAnAdmittedProducerAndRunsItsTask) {
  std::atomic<std::uint32_t> executed{0};
  std::atomic<std::uint32_t> probes{0};
  auto pool = std::make_unique<ThreadPoolLockFree>(1, 8);
  Access::enterAdmission(*pool); // a producer past the admission check, not yet published
  std::atomic<bool> returned{false};
  std::thread stopper([&]() {
    pool->shutdown();
    returned.store(true, std::memory_order_release);
  });
  // Admission is closed once an enqueue is refused; until then the probes are ordinary tasks.
  ASSERT_TRUE(settles(
      [&]() { return pool->tryEnqueue("probe", {&countRun, &probes}) == PoolStatus::POOL_STOPPED; },
      BOUND));
  // A permit with nothing behind it: the worker wakes to an empty ring with admission closed
  // and a producer outstanding, which is the state a pop at an unpublished cell leaves.
  Access::releasePermit(*pool);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(returned.load(std::memory_order_acquire))
      << "shutdown() returned with a producer still admitted";
  // The producer publishes, releases its permit and leaves.
  ASSERT_TRUE(Access::pushWithoutPermit(*pool, "late", {&countRun, &executed}));
  Access::releasePermit(*pool);
  Access::leaveAdmission(*pool);
  const bool BOUNDED = settles([&]() { return returned.load(std::memory_order_acquire); }, BOUND);
  EXPECT_TRUE(BOUNDED) << "shutdown() did not return after the producer left";
  if (!BOUNDED) {
    stopper.detach();
    (void)pool.release();
    return;
  }
  stopper.join();
  EXPECT_EQ(executed.load(std::memory_order_acquire), 1U)
      << "the admitted producer's task did not run: a worker exited on the close alone";
  EXPECT_EQ(pool->workerCount(), 0U);
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

/** @test A worker that cannot start, the first or one after others, leaves an invalid pool: the
 * workers that did start ran to their end and were joined before the constructor returned, the
 * pool has no workers, construction completes, and the caller's own mask is as it was. The seam is
 * pthread_create, interposed by the test binary; a sanitizer runtime that owns that symbol skips
 * the case. */
TEST_F(ThreadPoolLockFreeTest, WorkerStartFailureLeavesPoolInvalid) {
  using namespace apex::concurrency::test;
  if (!threadStartFaultAvailable()) {
    GTEST_SKIP() << "pthread_create belongs to the sanitizer runtime in this build";
  }
  for (const unsigned FAIL_AT : {1U, 2U, 3U}) {
    SCOPED_TRACE("creation " + std::to_string(FAIL_AT) + " of 3 fails");
    unblockAllHere();
    blockHere(SIGUSR1); // the caller's own mask, to find intact afterwards
    armThreadStartFailure(FAIL_AT);
    const auto START = std::chrono::steady_clock::now();
    bool valid = true;
    std::size_t count = 99;
    {
      ThreadPoolLockFree pool(3, 8);
      valid = pool.isValid();
      count = pool.workerCount();
    }
    const auto ELAPSED = std::chrono::steady_clock::now() - START;
    disarmThreadStartFailure();
    const ThreadStartTally TALLY = threadStartTally();
    EXPECT_FALSE(valid);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(TALLY.attempted, FAIL_AT) << "creation went on past the failure";
    EXPECT_EQ(TALLY.started, FAIL_AT - 1);
    EXPECT_EQ(TALLY.finished, FAIL_AT - 1)
        << "a started worker had not run to its end when the constructor returned";
    EXPECT_TRUE(blockedHere(SIGUSR1)) << "the caller's own mask was not restored";
    EXPECT_FALSE(blockedHere(SIGINT) || blockedHere(SIGTERM) || blockedHere(SIGPIPE))
        << "the worker set leaked onto the caller";
    EXPECT_LT(ELAPSED, std::chrono::seconds(10)) << "construction did not complete promptly";
    unblockAllHere();
  }
}
