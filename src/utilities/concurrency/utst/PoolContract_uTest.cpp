/**
 * @file PoolContract_uTest.cpp
 * @brief What ThreadPool and ThreadPoolLockFree both promise, run against each:
 *        construction, running and refusing work, stopping, in-flight and error
 *        reporting, and the workers' signal mask, configuration and start-up.
 *
 * What only one pool does is in that pool's own test file.
 */

#include "src/utilities/concurrency/inc/ThreadPool.hpp"
#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"
#include "src/utilities/concurrency/utst/support/Crew.hpp"
#include "src/utilities/concurrency/utst/support/PoolTasks.hpp"
#include "src/utilities/concurrency/utst/support/ThreadProbe.hpp"
#include "src/utilities/concurrency/utst/support/ThreadStartFault.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

#include <pthread.h>

using apex::concurrency::PoolConfig;
using apex::concurrency::PoolStatus;
using apex::concurrency::TaskError;
using apex::concurrency::ThreadPool;
using apex::concurrency::ThreadPoolLockFree;
using apex::concurrency::test::countRun;
using apex::concurrency::test::Crew;
using apex::concurrency::test::CrewState;
using apex::concurrency::test::drainHeldPool;
using apex::concurrency::test::DrainOutcome;
using apex::concurrency::test::Gate;
using apex::concurrency::test::NullTaskOutcome;
using apex::concurrency::test::runAroundNullDelegate;
using apex::concurrency::test::settles;
using apex::concurrency::test::ThreadIdentity;
using apex::concurrency::test::threadsAsleepIn;
using apex::concurrency::test::ThreadStartTally;

namespace {

/* ----------------------------- The Pools Under Test ----------------------------- */

/// What differs between the pools for these cases: the name of a pool's first worker and how a
/// pool with a worker configuration is built.
template <class Pool> struct PoolUnderTest;

template <> struct PoolUnderTest<ThreadPool> {
  static constexpr const char* FIRST_WORKER = "pool_w0";
  static ThreadPool configured(std::size_t workers, const PoolConfig& config) {
    return ThreadPool(workers, config);
  }
};

template <> struct PoolUnderTest<ThreadPoolLockFree> {
  static constexpr const char* FIRST_WORKER = "lf_pool_w0";
  static ThreadPoolLockFree configured(std::size_t workers, const PoolConfig& config) {
    return ThreadPoolLockFree(workers, 64, config);
  }
};

/// Names the two instantiations after their pools. GetName is the name gtest calls.
struct PoolNames {
  // NOLINTNEXTLINE(readability-identifier-naming)
  template <class Pool> static std::string GetName(int /*index*/) {
    return std::is_same_v<Pool, ThreadPool> ? "ThreadPool" : "ThreadPoolLockFree";
  }
};

template <class Pool> class PoolContract : public ::testing::Test {};
using Pools = ::testing::Types<ThreadPool, ThreadPoolLockFree>;
TYPED_TEST_SUITE(PoolContract, Pools, PoolNames);

/* ----------------------------- Tasks and Signal Masks ----------------------------- */

std::uint8_t failWith42(void* /*ctx*/) noexcept { return 42; }

/// Whether `sig` is blocked on the calling thread.
bool blockedHere(int sig) {
  sigset_t mask;
  sigemptyset(&mask);
  pthread_sigmask(SIG_BLOCK, nullptr, &mask);
  return sigismember(&mask, sig) == 1;
}

/// Whether any signal of the workers' blocked set, or SIGSEGV, is blocked on the calling thread.
bool workerSetBlockedHere() {
  return blockedHere(SIGINT) || blockedHere(SIGTERM) || blockedHere(SIGPIPE) ||
         blockedHere(SIGSEGV);
}

/// Clear the calling thread's mask, so a case starts from a known state whatever earlier cases
/// in this process left behind.
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

  static std::uint8_t record(void* ctx) noexcept {
    auto* mask = static_cast<WorkerMask*>(ctx);
    mask->sigint = blockedHere(SIGINT);
    mask->sigterm = blockedHere(SIGTERM);
    mask->sigpipe = blockedHere(SIGPIPE);
    mask->sigsegv = blockedHere(SIGSEGV);
    mask->done.store(true, std::memory_order_release);
    return 0;
  }
};

/// An affinity naming a CPU no host has, so applying it fails for every user; a requested RT
/// policy would fail only without CAP_SYS_NICE.
PoolConfig impossibleAffinity() {
  PoolConfig config;
  config.affinity = {255};
  return config;
}

} // namespace

/* ----------------------------- Construction and Execution ----------------------------- */

/** @test A constructed pool is valid, has the requested workers, no task in flight and no error. */
TYPED_TEST(PoolContract, ConstructsValidWithItsWorkersIdle) {
  TypeParam pool(4);
  EXPECT_TRUE(pool.isValid());
  EXPECT_EQ(pool.workerCount(), 4U);
  EXPECT_FALSE(pool.threadsRunning());
  EXPECT_FALSE(pool.hasErrors());
  EXPECT_EQ(pool.workerConfigFailures(), 0U);
}

/** @test A running pool accepts a task and has run it by the time shutdown() returns. */
TYPED_TEST(PoolContract, RunsAnAcceptedTask) {
  std::atomic<std::uint32_t> ran{0};
  TypeParam pool(2);
  EXPECT_EQ(pool.tryEnqueue("count", {&countRun, &ran}), PoolStatus::SUCCESS);
  pool.shutdown();
  EXPECT_EQ(ran.load(), 1U);
}

/** @test A hundred tasks offered to four workers by both enqueue calls each run once. */
TYPED_TEST(PoolContract, RunsEveryTaskOfMany) {
  constexpr std::uint32_t TASKS = 100;
  std::atomic<std::uint32_t> ran{0};
  TypeParam pool(4);
  for (std::uint32_t i = 0; i < TASKS / 2; ++i) {
    EXPECT_EQ(pool.tryEnqueue("count", {&countRun, &ran}), PoolStatus::SUCCESS);
    pool.enqueueTask("count", {&countRun, &ran});
  }
  pool.shutdown();
  EXPECT_EQ(ran.load(), TASKS);
}

/** @test A stopped pool refuses work: tryEnqueue() reports POOL_STOPPED, enqueueTask(), which
 * has no status to return, drops the task, and neither task runs. */
TYPED_TEST(PoolContract, RefusesWorkAfterShutdown) {
  std::atomic<std::uint32_t> ran{0};
  TypeParam pool(2);
  pool.shutdown();
  EXPECT_EQ(pool.tryEnqueue("refused", {&countRun, &ran}), PoolStatus::POOL_STOPPED);
  pool.enqueueTask("dropped", {&countRun, &ran});
  EXPECT_EQ(ran.load(), 0U);
  EXPECT_FALSE(pool.isValid());
}

/* ----------------------------- Stopping ----------------------------- */

/** @test shutdown() runs every task accepted before it, then the pool is stopped for good. Ten
 * tasks wait behind the only worker, which is held until the pool refuses work, so all of them
 * are still queued when the stop begins. After it the pool is not valid, and a second
 * shutdown() returns and leaves it refusing work. */
TYPED_TEST(PoolContract, ShutdownRunsEveryQueuedTaskThenRefusesWork) {
  const DrainOutcome OUT = drainHeldPool<TypeParam>(false, 10U, std::size_t{1});
  ASSERT_TRUE(OUT.workerHeld);
  ASSERT_TRUE(OUT.finished) << "shutdown() did not return";
  EXPECT_EQ(OUT.ranBeforeStop, 0U);
  EXPECT_GE(OUT.accepted, 10U);
  EXPECT_EQ(OUT.ran, OUT.accepted);
  EXPECT_FALSE(OUT.validAfter);
  EXPECT_EQ(OUT.enqueueAfter, PoolStatus::POOL_STOPPED);
}

/** @test Destroying a pool runs every task accepted before it: the same held worker and waiting
 * tasks, stopped by the destructor. */
TYPED_TEST(PoolContract, DestructorRunsEveryQueuedTask) {
  const DrainOutcome OUT = drainHeldPool<TypeParam>(true, 10U, std::size_t{1});
  ASSERT_TRUE(OUT.workerHeld);
  ASSERT_TRUE(OUT.finished) << "the destructor did not return";
  EXPECT_EQ(OUT.ranBeforeStop, 0U);
  EXPECT_GE(OUT.accepted, 10U);
  EXPECT_EQ(OUT.ran, OUT.accepted);
}

/** @test shutdown() wakes workers that are parked with nothing to do. Two idle workers are seen
 * asleep inside the pool, where the host shows a thread's blocked system call, and a shutdown()
 * on a thread the case waits for with a bound returns. Elsewhere a watch interval stands in for
 * seeing them asleep. */
TYPED_TEST(PoolContract, ShutdownWakesIdleWorkers) {
  struct Idle : CrewState {
    TypeParam pool{2};
  };
  Crew<Idle> crew;
  const bool PARKED = settles([&]() {
    const int ASLEEP = threadsAsleepIn(&crew->pool, sizeof(crew->pool));
    if (ASLEEP < 0) {
      std::this_thread::sleep_for(apex::concurrency::test::WATCH);
      return true;
    }
    return ASLEEP == 2;
  });
  EXPECT_TRUE(PARKED) << "the idle workers did not park";

  crew.start([](Idle& state) { state.pool.shutdown(); });
  ASSERT_TRUE(crew.finish()) << "shutdown() did not return with two idle workers";
  EXPECT_FALSE(crew->pool.isValid());
}

/* ----------------------------- In-flight and Error Reporting ----------------------------- */

/** @test threadsRunning() is true while a worker is inside a task and false once the task has
 * returned. The task holds its worker at a gate the case opens; the gate also opens when the
 * case leaves early, so the pool's destructor is never left waiting for it. */
TYPED_TEST(PoolContract, ReportsATaskInFlightWhileItRuns) {
  struct Held {
    ~Held() { gate.open.store(true, std::memory_order_release); }
    Gate gate;
    TypeParam pool{1};
  };
  Held held;
  ASSERT_EQ(held.pool.tryEnqueue("hold", {&Gate::hold, &held.gate}), PoolStatus::SUCCESS);
  ASSERT_TRUE(settles([&]() { return held.gate.holding.load() == 1; }))
      << "the worker did not start the task";
  EXPECT_TRUE(held.pool.threadsRunning());

  held.gate.open.store(true, std::memory_order_release);
  EXPECT_TRUE(settles([&]() { return !held.pool.threadsRunning(); }))
      << "a task still counted in flight after it returned";
  held.pool.shutdown();
  EXPECT_FALSE(held.pool.threadsRunning());
}

/** @test A task that returns a non-zero code is recorded with its label, its code and
 * TASK_FAILED. */
TYPED_TEST(PoolContract, CollectsAFailedTasksLabelAndCode) {
  TypeParam pool(2);
  ASSERT_EQ(pool.tryEnqueue("failing-task", {&failWith42, nullptr}), PoolStatus::SUCCESS);
  pool.shutdown();

  auto errors = pool.collectErrors();
  ASSERT_EQ(errors.size(), 1U);
  const TaskError& ERR = errors.front();
  EXPECT_STREQ(ERR.label, "failing-task");
  EXPECT_EQ(ERR.errorCode, 42);
  EXPECT_EQ(ERR.status, PoolStatus::TASK_FAILED);
}

/** @test hasErrors() follows the error queue: false on a new pool, true once a task has failed,
 * false again after collectErrors() has taken the errors, which a second collection finds gone. */
TYPED_TEST(PoolContract, HasErrorsFollowsFailuresAndCollection) {
  TypeParam pool(1);
  EXPECT_FALSE(pool.hasErrors());

  ASSERT_EQ(pool.tryEnqueue("fail", {&failWith42, nullptr}), PoolStatus::SUCCESS);
  pool.shutdown();
  EXPECT_TRUE(pool.hasErrors());

  EXPECT_EQ(pool.collectErrors().size(), 1U);
  EXPECT_FALSE(pool.hasErrors());
  EXPECT_TRUE(pool.collectErrors().empty());
}

/** @test A null delegate queued between two tasks is a no-op: both tasks run and no error is
 * recorded for it. */
TYPED_TEST(PoolContract, NullDelegateBetweenTasksIsANoOp) {
  const NullTaskOutcome OUT = runAroundNullDelegate<TypeParam>(std::size_t{1});
  ASSERT_TRUE(OUT.finished) << "shutdown() did not return";
  EXPECT_EQ(OUT.accepted, 3U);
  EXPECT_EQ(OUT.ran, 2U);
  EXPECT_FALSE(OUT.hasErrors);
  EXPECT_EQ(OUT.errors, 0U);
}

/* ----------------------------- Signal Masks ----------------------------- */

/** @test Constructing and destroying a pool leaves the constructing thread's signal mask as it
 * was: the workers inherit the blocked set, the caller does not keep it. */
TYPED_TEST(PoolContract, ConstructorRestoresCallerSignalMask) {
  bool blockedWhileAlive = false;
  bool blockedAfter = false;
  std::thread caller([&]() {
    unblockAllHere();
    {
      TypeParam pool(2);
      blockedWhileAlive = workerSetBlockedHere();
    }
    blockedAfter = workerSetBlockedHere();
  });
  caller.join();
  EXPECT_FALSE(blockedWhileAlive) << "the pool's worker set leaked onto the constructing thread";
  EXPECT_FALSE(blockedAfter) << "the constructing thread's mask stayed changed after the pool";
}

/** @test The workers block the process signals (SIGINT, SIGTERM, SIGPIPE) so those stay with the
 * thread that owns them, and do not block SIGSEGV, a synchronous fault whose handler must run. */
TYPED_TEST(PoolContract, WorkersBlockProcessSignalsOnly) {
  WorkerMask mask;
  PoolStatus accepted = PoolStatus::POOL_STOPPED;
  bool recorded = false;
  std::thread caller([&]() {
    unblockAllHere();
    TypeParam pool(1);
    accepted = pool.tryEnqueue("mask", {&WorkerMask::record, &mask});
    recorded = settles([&]() { return mask.done.load(std::memory_order_acquire); });
  });
  caller.join();
  ASSERT_EQ(accepted, PoolStatus::SUCCESS);
  ASSERT_TRUE(recorded) << "the worker did not run the task";
  EXPECT_TRUE(mask.sigint);
  EXPECT_TRUE(mask.sigterm);
  EXPECT_TRUE(mask.sigpipe);
  EXPECT_FALSE(mask.sigsegv) << "a blocked SIGSEGV only bypasses an installed crash handler";
}

/* ----------------------------- Worker Configuration and Start-up ----------------------------- */

/** @test A worker whose configuration cannot be applied is counted, so the owner can tell the
 * pool is not running as configured. */
TYPED_TEST(PoolContract, ReportsWorkerConfigFailure) {
  TypeParam unconfigured = PoolUnderTest<TypeParam>::configured(2, impossibleAffinity());
  EXPECT_EQ(unconfigured.workerCount(), 2U);
  EXPECT_EQ(unconfigured.workerConfigFailures(), 2U);
}

/** @test A worker takes the affinity and the name its pool was configured with: a task finds its
 * thread limited to the one configured CPU and named for the pool. */
TYPED_TEST(PoolContract, WorkerRunsWithItsConfiguredAffinityAndName) {
  const std::optional<std::uint8_t> CPU = apex::concurrency::test::nameableAllowedCpu();
  PoolConfig config;
  if (CPU) {
    config.affinity = {*CPU};
  }
  // Declared before the pool: the pool is destroyed first, and its destructor waits for the task
  // that writes here, on every way out of this case.
  ThreadIdentity worker;
  TypeParam pool = PoolUnderTest<TypeParam>::configured(1, config);
  EXPECT_EQ(pool.workerConfigFailures(), 0U);

  ASSERT_EQ(pool.tryEnqueue("identity", {&ThreadIdentity::record, &worker}), PoolStatus::SUCCESS);
  ASSERT_TRUE(settles([&]() { return worker.done.load(); }));
  EXPECT_STREQ(worker.name.data(), PoolUnderTest<TypeParam>::FIRST_WORKER);
  if (!CPU) {
    GTEST_SKIP() << "no allowed CPU below 256: the affinity half did not run";
  }
  EXPECT_EQ(worker.allowedCpus, 1);
  EXPECT_EQ(worker.onlyCpu, int{*CPU});
}

/** @test A worker that cannot start, the first or one after others, leaves an invalid pool: the
 * workers that did start ran to their end and were joined before the constructor returned, the
 * pool has no workers, and the caller's own mask is as it was. The pool is built and destroyed
 * on a thread the case waits for with a bound, so a constructor that never returns fails the
 * case. The seam is pthread_create, interposed by the test binary; a sanitizer runtime that
 * owns that symbol skips the case. */
TYPED_TEST(PoolContract, WorkerStartFailureLeavesPoolInvalid) {
  using namespace apex::concurrency::test;
  if (!threadStartFaultAvailable()) {
    GTEST_SKIP() << "pthread_create belongs to the sanitizer runtime in this build";
  }
  struct Attempt : CrewState {
    explicit Attempt(unsigned at) : failAt(at) {}
    unsigned failAt;
    bool valid{true};
    std::size_t workers{99};
    ThreadStartTally tally;
    bool ownMaskKept{false};
    bool workerSetLeaked{true};
  };
  for (const unsigned FAIL_AT : {1U, 2U, 3U}) {
    SCOPED_TRACE("creation " + std::to_string(FAIL_AT) + " of 3 fails");
    Crew<Attempt> crew(FAIL_AT);
    crew.start([](Attempt& attempt) {
      unblockAllHere();
      blockHere(SIGUSR1); // the caller's own mask, to find intact afterwards
      armThreadStartFailure(attempt.failAt);
      {
        TypeParam pool(3);
        attempt.valid = pool.isValid();
        attempt.workers = pool.workerCount();
      }
      disarmThreadStartFailure();
      attempt.tally = threadStartTally();
      attempt.ownMaskKept = blockedHere(SIGUSR1);
      attempt.workerSetLeaked = blockedHere(SIGINT) || blockedHere(SIGTERM) || blockedHere(SIGPIPE);
    });
    if (!crew.finish()) {
      disarmThreadStartFailure();
      FAIL() << "construction or destruction did not return";
    }
    EXPECT_FALSE(crew->valid);
    EXPECT_EQ(crew->workers, 0U);
    EXPECT_EQ(crew->tally.attempted, FAIL_AT) << "creation went on past the failure";
    EXPECT_EQ(crew->tally.started, FAIL_AT - 1);
    EXPECT_EQ(crew->tally.finished, FAIL_AT - 1)
        << "a started worker had not run to its end when the constructor returned";
    EXPECT_TRUE(crew->ownMaskKept) << "the caller's own mask was not restored";
    EXPECT_FALSE(crew->workerSetLeaked) << "the worker set leaked onto the caller";
  }
}
