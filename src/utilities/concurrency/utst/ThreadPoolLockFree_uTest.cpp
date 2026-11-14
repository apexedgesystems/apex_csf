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
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

/// The white-box seams the pool's header befriends: the ring and wake states the shutdown
/// protocol has to survive, built directly rather than hoped for from a racing schedule.
struct ThreadPoolLockFreeTestAccess {
  /// A published task the workers were not told about: the state a worker leaves behind when its
  /// look at the ring found a cell ahead still being published.
  static bool pushWithoutSignal(ThreadPoolLockFree& pool, const char* label, DelegateU8 task) {
    return pool.taskQueue_.tryPush(ThreadPoolLockFree::TaskItem{label, task});
  }
  /// A signal with nothing published for it.
  static void signalWithoutTask(ThreadPoolLockFree& pool) { pool.signalWork(); }
  /// The workers parked or about to park.
  static std::uint32_t idleWorkers(const ThreadPoolLockFree& pool) {
    return pool.wake_.value.idle.load(std::memory_order_acquire);
  }
  /// Publications after which the wake counter is back at a value it held.
  static constexpr std::uint32_t COUNTER_TURN = std::uint32_t{1} << 31;
  /// Put the wake counter where that many publications would leave it, without publishing
  /// anything and without waking anyone. Accelerated state: what a worker delayed before its
  /// wait would find after that much real history, which no test executes.
  static void advanceCounterUnsignalled(ThreadPoolLockFree& pool, std::uint32_t publications) {
    pool.wake_.value.generation.fetch_add(publications * ThreadPoolLockFree::ADVANCE,
                                          std::memory_order_seq_cst);
  }
  /// The ring holds fewer tasks than one turn of the wake counter, so the workers cannot all
  /// be a full turn behind it at once.
  static_assert(ThreadPoolLockFree::MAX_QUEUE_CAPACITY < COUNTER_TURN);
  /// One publication moves the counter's lowest bit and leaves the quiescent mark alone.
  static_assert((ThreadPoolLockFree::ADVANCE & ThreadPoolLockFree::QUIESCENT) == 0 &&
                ThreadPoolLockFree::ADVANCE == 2 * ThreadPoolLockFree::QUIESCENT);
  /// A producer past the admission check that has not published yet.
  static void enterAdmission(ThreadPoolLockFree& pool) {
    pool.admission_.value.fetch_add(1, std::memory_order_acq_rel);
  }
  /// That producer leaving, after its push and signal.
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

/// Keep a scenario for the rest of the process. The holder is allocated once and never freed, so
/// static destruction at exit does not reach a pool whose shutdown() is still running or whose
/// worker never woke.
void keepForProcess(std::shared_ptr<void> scenario) {
  static auto* const kept = new std::vector<std::shared_ptr<void>>;
  kept->push_back(std::move(scenario));
}

/// A test body's hold on a scenario: a pool together with everything its workers, its task
/// callbacks and any helper thread touch, on the heap and shared, so none of it is on the test's
/// stack. The scenario is destroyed only when the body reports that the pool's work settled; on
/// any other way out, a failed bound or a fatal assertion, it is kept for the process, because
/// destroying a pool with a worker parked on a lost wake blocks in the destructor.
template <class Scenario> class Held {
public:
  template <class... Args>
  explicit Held(Args&&... args)
      : scenario_(std::make_shared<Scenario>(std::forward<Args>(args)...)) {}
  Held(const Held&) = delete;
  Held& operator=(const Held&) = delete;
  ~Held() {
    if (!settled_) {
      keepForProcess(std::move(scenario_));
    }
  }

  Scenario* operator->() const noexcept { return scenario_.get(); }
  [[nodiscard]] std::shared_ptr<Scenario> share() const { return scenario_; }
  void settled() noexcept { settled_ = true; }

private:
  std::shared_ptr<Scenario> scenario_;
  bool settled_{false};
};

/// A shutdown regression's state. The pool is the last member, so it is destroyed, and its tasks
/// finished, before the counters they write.
struct ShutdownScenario {
  ShutdownScenario(std::size_t workers, std::size_t capacity) : pool(workers, capacity) {}

  /// Whether every worker is parked or about to park.
  [[nodiscard]] bool workersIdle() const { return Access::idleWorkers(pool) == pool.workerCount(); }

  std::atomic<std::uint32_t> executed{0};
  std::atomic<std::uint32_t> probes{0};
  std::atomic<bool> returned{false};
  ThreadPoolLockFree pool;
};

/// shutdown() on its own thread, which holds its own reference to the scenario. The destructor
/// joins a stopper whose shutdown() returned and detaches one that is still inside it, so a body
/// can return at any assertion without a joinable thread and without waiting on a hung pool.
class Stopper {
public:
  explicit Stopper(std::shared_ptr<ShutdownScenario> scenario)
      : scenario_(std::move(scenario)), thread_([held = scenario_]() {
          held->pool.shutdown();
          held->returned.store(true, std::memory_order_release);
        }) {}
  Stopper(const Stopper&) = delete;
  Stopper& operator=(const Stopper&) = delete;
  ~Stopper() {
    if (returned()) {
      thread_.join();
    } else {
      thread_.detach();
    }
  }

  [[nodiscard]] bool returned() const noexcept {
    return scenario_->returned.load(std::memory_order_acquire);
  }
  /// Whether shutdown() returned within the bound.
  [[nodiscard]] bool returnsWithin(std::chrono::milliseconds limit) const {
    return settles([this]() { return returned(); }, limit);
  }

private:
  std::shared_ptr<ShutdownScenario> scenario_;
  std::thread thread_;
};

} // namespace

/** @test A task the parked worker was not told about runs on the next signal, together with the
 * task that signal announces: a woken worker drains the ring rather than taking one task per
 * wake, so a look at the ring that found an unpublished cell ahead strands nothing. */
TEST_F(ThreadPoolLockFreeTest, TaskBehindAMissedSignalRunsOnTheNextWake) {
  Held<ShutdownScenario> scenario(1U, 8U);
  ASSERT_TRUE(settles([&]() { return scenario->workersIdle(); }, BOUND));
  ASSERT_TRUE(
      Access::pushWithoutSignal(scenario->pool, "stranded", {&countRun, &scenario->executed}));
  ASSERT_EQ(scenario->pool.tryEnqueue("next", {&countRun, &scenario->executed}),
            PoolStatus::SUCCESS);
  const bool RAN =
      settles([&]() { return scenario->executed.load(std::memory_order_acquire) == 2; }, BOUND);
  EXPECT_TRUE(RAN) << "ran " << scenario->executed.load()
                   << " of 2: a task waits for a signal that never comes";
  if (RAN) {
    scenario.settled();
  }
}

/** @test shutdown() runs a task the parked worker was not told about, and returns: its own signal
 * wakes every worker, and a worker's exit needs the ring seen empty after the quiescent mark. */
TEST_F(ThreadPoolLockFreeTest, ShutdownRunsTheTaskBehindAMissedSignal) {
  Held<ShutdownScenario> scenario(1U, 8U);
  ASSERT_TRUE(settles([&]() { return scenario->workersIdle(); }, BOUND));
  ASSERT_TRUE(
      Access::pushWithoutSignal(scenario->pool, "stranded", {&countRun, &scenario->executed}));
  Stopper stopper(scenario.share());
  ASSERT_TRUE(stopper.returnsWithin(BOUND))
      << "shutdown() did not return: a worker is parked for a signal that never comes";
  scenario.settled();
  EXPECT_EQ(scenario->executed.load(std::memory_order_acquire), 1U)
      << "the accepted task did not run";
  EXPECT_EQ(scenario->pool.workerCount(), 0U);
}

/** @test With admission closed while a producer is still inside tryEnqueue(), a worker woken to an
 * empty ring parks again rather than exiting, and shutdown() parks until that producer has left;
 * the task the producer then publishes runs before shutdown() returns. */
TEST_F(ThreadPoolLockFreeTest, ShutdownWaitsForAnAdmittedProducerAndRunsItsTask) {
  Held<ShutdownScenario> scenario(1U, 8U);
  Access::enterAdmission(scenario->pool); // a producer past the admission check, not yet published
  Stopper stopper(scenario.share());
  // Admission is closed once an enqueue is refused; until then the probes are ordinary tasks.
  ASSERT_TRUE(settles(
      [&]() {
        return scenario->pool.tryEnqueue("probe", {&countRun, &scenario->probes}) ==
               PoolStatus::POOL_STOPPED;
      },
      BOUND));
  // A signal with nothing behind it: the worker wakes to an empty ring with admission closed
  // and a producer outstanding, which is the state a pop at an unpublished cell leaves.
  Access::signalWithoutTask(scenario->pool);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(stopper.returned()) << "shutdown() returned with a producer still admitted";
  // The producer publishes, signals and leaves.
  ASSERT_TRUE(Access::pushWithoutSignal(scenario->pool, "late", {&countRun, &scenario->executed}));
  Access::signalWithoutTask(scenario->pool);
  Access::leaveAdmission(scenario->pool);
  ASSERT_TRUE(stopper.returnsWithin(BOUND)) << "shutdown() did not return after the producer left";
  scenario.settled();
  EXPECT_EQ(scenario->executed.load(std::memory_order_acquire), 1U)
      << "the admitted producer's task did not run: a worker exited on the close alone";
  EXPECT_EQ(scenario->pool.workerCount(), 0U);
}

/** @test shutdown() reaches workers whose saved wake word the publication counter has come back
 * to. Accelerated state, not executed history: both workers are parked, then the counter is moved
 * directly, with no publication and no wake, by one publication short of a full turn, by a full
 * turn, and by one past it. Those are the words a worker delayed just before its wait would
 * compare against after that many real publications. shutdown() returns in each, because its mark
 * is a bit the counter never touches: no turn of the counter makes the word equal a saved one. */
TEST_F(ThreadPoolLockFreeTest, ShutdownReachesWorkersAcrossATurnOfTheWakeCounter) {
  for (const std::uint32_t PUBLICATIONS :
       {Access::COUNTER_TURN - 1, Access::COUNTER_TURN, Access::COUNTER_TURN + 1}) {
    SCOPED_TRACE("counter moved by " + std::to_string(PUBLICATIONS) + " publications");
    Held<ShutdownScenario> scenario(2U, 2U);
    ASSERT_TRUE(settles([&]() { return scenario->workersIdle(); }, BOUND));
    // Idle is announced before the last look at the ring; give both workers time to be asleep
    // on the word they read, so the moved counter is what their wake compares against.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Access::advanceCounterUnsignalled(scenario->pool, PUBLICATIONS);
    Stopper stopper(scenario.share());
    ASSERT_TRUE(stopper.returnsWithin(BOUND))
        << "shutdown() did not return: its final wake word equals one a worker saved";
    scenario.settled();
    EXPECT_EQ(scenario->pool.workerCount(), 0U);
  }
}

/** @test A task enqueued while the worker is on its way to parking is run: each round enqueues one
 * task the moment the previous one has run, which is when the worker reads the generation, looks
 * at the ring and parks. Either that look finds the task or the producer's signal stops the park;
 * a round that waits out the bound is a wake lost between the two. */
TEST_F(ThreadPoolLockFreeTest, EveryEnqueueReachesAWorkerThatIsParking) {
  constexpr std::uint32_t ROUNDS = 20'000;
  Held<ShutdownScenario> scenario(1U, 8U);
  for (std::uint32_t round = 0; round < ROUNDS; ++round) {
    ASSERT_EQ(scenario->pool.tryEnqueue("round", {&countRun, &scenario->executed}),
              PoolStatus::SUCCESS);
    const auto DEADLINE = std::chrono::steady_clock::now() + BOUND;
    while (scenario->executed.load(std::memory_order_acquire) != round + 1) {
      ASSERT_LT(std::chrono::steady_clock::now(), DEADLINE)
          << "round " << round << ": the task was accepted and no worker came for it";
      std::this_thread::yield();
    }
  }
  scenario.settled();
}

namespace {

std::int64_t cpuNanoseconds(clockid_t clock) {
  timespec spent{};
  clock_gettime(clock, &spent);
  return static_cast<std::int64_t>(spent.tv_sec) * 1'000'000'000 + spent.tv_nsec;
}

/// A chain of tasks on one worker: each task enqueues its successor before it returns, so the
/// ring never holds more than one task and every enqueue finds the worker awake. The last task
/// records the worker's CPU clock and how much of it was spent when the chain ended.
struct ChainScenario {
  ChainScenario(std::uint32_t links, std::size_t capacity) : remaining(links), pool(1, capacity) {}

  static std::uint8_t link(void* ctx) noexcept {
    auto* chain = static_cast<ChainScenario*>(ctx);
    chain->ran.fetch_add(1, std::memory_order_relaxed);
    if (--chain->remaining > 0) {
      if (chain->pool.tryEnqueue("link", {&ChainScenario::link, chain}) != PoolStatus::SUCCESS) {
        chain->refused.store(true, std::memory_order_release);
      }
      return 0;
    }
    pthread_getcpuclockid(pthread_self(), &chain->workerClock);
    chain->cpuAtEnd = cpuNanoseconds(chain->workerClock);
    chain->ended.store(true, std::memory_order_release);
    return 0;
  }

  std::uint32_t remaining; ///< Links still to run; the worker's alone once the chain starts.
  std::atomic<std::uint32_t> ran{0};
  std::atomic<bool> refused{false};
  std::atomic<bool> ended{false};
  clockid_t workerClock{};  ///< Published by `ended`.
  std::int64_t cpuAtEnd{0}; ///< Published by `ended`.
  ThreadPoolLockFree pool;
};

} // namespace

/** @test What a worker does after a drain does not grow with the drain. One worker runs 500,000
 * chained tasks through a two-cell ring, every enqueue accepted; from the end of the last task
 * to its park it spends a bounded amount of CPU, a few failed pops, whatever it delivered before.
 * A signal says the ring changed; it is not a count kept per task for the worker to work off. */
TEST_F(ThreadPoolLockFreeTest, WorkAfterASustainedDrainIsBounded) {
  constexpr std::uint32_t LINKS = 500'000;
  constexpr std::int64_t TAIL_LIMIT_NS = 1'000'000;
  Held<ChainScenario> chain(LINKS, 2U);
  ASSERT_EQ(chain->pool.tryEnqueue("link", {&ChainScenario::link, chain.share().get()}),
            PoolStatus::SUCCESS);
  ASSERT_TRUE(settles([&]() { return chain->ended.load(std::memory_order_acquire); },
                      std::chrono::seconds(30)))
      << "ran " << chain->ran.load() << " of " << LINKS;
  EXPECT_FALSE(chain->refused.load(std::memory_order_acquire))
      << "an enqueue into the two-cell ring was refused";
  EXPECT_EQ(chain->ran.load(std::memory_order_acquire), LINKS);
  // Wall time for whatever follows the last task; the worker's CPU clock says what it cost.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  const std::int64_t TAIL_NS = cpuNanoseconds(chain->workerClock) - chain->cpuAtEnd;
  EXPECT_LT(TAIL_NS, TAIL_LIMIT_NS)
      << "the worker spent " << TAIL_NS << " ns of CPU after its last of " << LINKS << " tasks";
  chain.settled();
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
