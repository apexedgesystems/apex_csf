/**
 * @file Concurrency_pTest.cpp
 * @brief Performance tests for concurrency primitives.
 *
 * Measures:
 *  - LockFreeQueue push/pop (single-thread and MPMC contention)
 *  - SPSCQueue push/pop (single-thread and producer-consumer)
 *  - RingBuffer push/pop (single-thread, no atomics)
 *  - SpinLock lock/unlock (uncontended and contended, vs std::mutex)
 *  - Semaphore acquire/release (uncontended and contended)
 *  - Latch round of four threads and tryWait on a triggered latch
 *  - Barrier phase of four threads and generation query
 *  - ThreadPool / ThreadPoolLockFree enqueue cost with busy workers, time
 *    from an enqueue to the task starting on a sleeping worker, drain time
 *    per queued task (one and four workers), rejection, and an enqueue
 *    diagnostic with live workers whose regime is not fixed
 *  - ThreadPoolLockFree threadsRunning() with parked and with working workers
 *
 * Every measuring case writes one CSV row. Per-operation times in the summary
 * lines come from the measured call rate. The contention cases run
 * `--threads N` threads, four when the flag is not given.
 *
 * Usage:
 *   ./Concurrency_PTEST --csv results.csv
 *   ./Concurrency_PTEST --quick
 *   ./Concurrency_PTEST --profile perf --gtest_filter="*SpinLock*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/concurrency/inc/Barrier.hpp"
#include "src/utilities/concurrency/inc/Cache.hpp"
#include "src/utilities/concurrency/inc/Latch.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/inc/RingBuffer.hpp"
#include "src/utilities/concurrency/inc/SPSCQueue.hpp"
#include "src/utilities/concurrency/inc/Semaphore.hpp"
#include "src/utilities/concurrency/inc/SpinLock.hpp"
#include "src/utilities/concurrency/inc/ThreadPool.hpp"
#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace ub = vernier::bench;

using apex::concurrency::Barrier;
using apex::concurrency::DelegateU8;
using apex::concurrency::Latch;
using apex::concurrency::LockFreeQueue;
using apex::concurrency::PoolStatus;
using apex::concurrency::RingBuffer;
using apex::concurrency::Semaphore;
using apex::concurrency::SpinLock;
using apex::concurrency::SPSCQueue;
using apex::concurrency::ThreadPool;
using apex::concurrency::ThreadPoolLockFree;

namespace {

/// This binary's default for `--threads`: a contention case with one thread
/// has nobody to contend with.
constexpr int DEFAULT_THREADS = 4;

/// Failed tries in a row before a spinning thread offers its core. On cores
/// of their own two threads never get there; it keeps a case moving when
/// threads outnumber cores.
constexpr std::size_t RETRIES_BEFORE_YIELD = 4096;

/// Nanoseconds per operation from a result's call rate. A call that performs
/// several operations passes how many.
double nsPerOp(const ub::PerfResult& result, double opsPerCall = 1.0) {
  const double OPS_PER_SECOND = result.callsPerSecond * opsPerCall;
  return (OPS_PER_SECOND > 0.0) ? 1e9 / OPS_PER_SECOND : 0.0;
}

/// Waits for `ready()` without sleeping on it. The core is offered between
/// looks, so the wait also ends when threads outnumber cores.
template <class Ready> void spinUntil(Ready ready) {
  while (!ready()) {
    std::this_thread::yield();
  }
}

/* ----------------------------- Pool drain ----------------------------- */

/// One round's gate. Each worker runs one gate task and stays in it until the
/// round opens.
struct DrainGate {
  std::atomic<bool> open{false};
  std::atomic<int> held{0};                  ///< Workers inside this gate.
  const std::atomic<bool>* abandon{nullptr}; ///< Releases every gate at teardown.
};

std::uint8_t holdAtGate(void* context) noexcept {
  auto* gate = static_cast<DrainGate*>(context);
  gate->held.fetch_add(1, std::memory_order_release);
  spinUntil([gate] {
    return gate->open.load(std::memory_order_acquire) ||
           gate->abandon->load(std::memory_order_acquire);
  });
  return 0;
}

std::uint8_t doNothing(void* /*context*/) noexcept { return 0; }

/// Measured rounds plus one warmup round.
std::size_t drainRounds(const ub::PerfCase& perf) {
  return static_cast<std::size_t>(perf.repeats()) + 1;
}

/// Tasks a drain case queues before it measures: every round's work and one
/// gate per worker before each round and after the last.
std::size_t drainTasks(const ub::PerfCase& perf, int workers) {
  const std::size_t ROUNDS = drainRounds(perf);
  return ROUNDS * static_cast<std::size_t>(perf.cycles()) +
         (ROUNDS + 1) * static_cast<std::size_t>(workers);
}

/**
 * @brief Wall time per task for the workers to drain work already queued.
 *
 * Every round's tasks are queued before the measurement, each round behind
 * one gate task per worker. Opening a round's gates lets all workers go at
 * once, and the round ends when every worker is inside the next round's
 * gate, which a worker reaches only after the round's last task returned. A
 * round therefore times the workers taking and running `cycles()` tasks that
 * do nothing, and none of the producer's work. Workers and the measuring
 * thread spin at the gates, so no wake is in the figure.
 */
template <class Pool> void measureDrain(ub::PerfCase& perf, Pool& pool, int workers) {
  ASSERT_TRUE(pool.isValid());

  const std::size_t ROUNDS = drainRounds(perf);
  std::atomic<bool> abandon{false};
  std::deque<DrainGate> gates(ROUNDS + 1);
  for (DrainGate& gate : gates) {
    gate.abandon = &abandon;
  }

  bool queued = true;
  const auto QUEUE = [&](const char* label, DelegateU8 task) {
    queued = (pool.tryEnqueue(label, task) == PoolStatus::SUCCESS) && queued;
  };
  const auto QUEUE_GATE = [&](DrainGate& gate) {
    for (int w = 0; w < workers; ++w) {
      QUEUE("gate", DelegateU8{&holdAtGate, &gate});
    }
  };

  QUEUE_GATE(gates[0]);
  for (std::size_t round = 0; round < ROUNDS; ++round) {
    for (int i = 0; i < perf.cycles(); ++i) {
      QUEUE("task", DelegateU8{&doNothing, nullptr});
    }
    QUEUE_GATE(gates[round + 1]);
  }
  if (!queued) {
    abandon.store(true, std::memory_order_release);
    pool.shutdown();
    FAIL() << "the pool refused part of the drain workload";
  }
  spinUntil([&] { return gates[0].held.load(std::memory_order_acquire) == workers; });

  std::size_t round = 0;
  const auto DRAIN_ROUND = [&] {
    gates[round].open.store(true, std::memory_order_release);
    ++round;
    spinUntil([&] { return gates[round].held.load(std::memory_order_acquire) == workers; });
  };

  DRAIN_ROUND(); // warmup
  const ub::PerfResult RESULT = perf.measured(DRAIN_ROUND, "drain");

  abandon.store(true, std::memory_order_release);
  pool.shutdown();

  std::printf("\n%s drain (%d worker%s): %.0f tasks/s, %.1f ns of wall time per task\n",
              perf.testName().c_str(), workers, (workers == 1) ? "" : "s", RESULT.callsPerSecond,
              nsPerOp(RESULT));
}

/* ----------------------------- Pool enqueue ----------------------------- */

/**
 * @brief Producer cost of an enqueue while every worker is busy.
 *
 * The workers are held at a gate for the whole measurement, so a pass times
 * `cycles()` enqueues and nothing beside them: no worker takes a task or
 * goes idle while the producer runs. The queued tasks run when the gate
 * opens at the end. The lock-free pool's ring is sized for all of them, so
 * each cell is written once and not reused.
 */
template <class Pool> void measureEnqueueToBusyPool(ub::PerfCase& perf, Pool& pool, int workers) {
  ASSERT_TRUE(pool.isValid());

  std::atomic<bool> abandon{false};
  DrainGate gate;
  gate.abandon = &abandon;
  bool held = true;
  for (int w = 0; w < workers; ++w) {
    held = (pool.tryEnqueue("gate", DelegateU8{&holdAtGate, &gate}) == PoolStatus::SUCCESS) && held;
  }
  if (!held) {
    abandon.store(true, std::memory_order_release);
    pool.shutdown();
    FAIL() << "the pool refused the gate tasks";
  }
  spinUntil([&] { return gate.held.load(std::memory_order_acquire) == workers; });

  std::size_t refused = 0;
  const auto PASS = [&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      if (pool.tryEnqueue("task", DelegateU8{&doNothing, nullptr}) != PoolStatus::SUCCESS) {
        ++refused;
      }
    }
  };

  PASS(); // warmup
  const ub::PerfResult RESULT = perf.measured(PASS, "enqueue");

  gate.open.store(true, std::memory_order_release);
  pool.shutdown();

  EXPECT_EQ(refused, 0U) << "the pool refused part of the workload";
  std::printf("\n%s (%d busy workers): %.0f enqueues/s (%.1f ns per enqueue)\n",
              perf.testName().c_str(), workers, RESULT.callsPerSecond, nsPerOp(RESULT));
}

/// The workers' count of finished tasks, on a cache line of its own. Next to
/// a variable the producer writes, every task would move that line between
/// the two threads and the case would time that exchange instead of the pool.
struct alignas(apex::concurrency::cache::CACHE_LINE_SIZE) CompletionCount {
  std::atomic<std::size_t> value{0};

  static std::uint8_t add(void* context) noexcept {
    static_cast<CompletionCount*>(context)->value.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }
};

/**
 * @brief Producer cost of an enqueue attempt while the workers take the tasks (diagnostic).
 *
 * One producer submits tasks that only count themselves to live workers. The
 * regime this settles in is not fixed. Workers that keep up leave the queue
 * empty and look for work between tasks, on the memory the producer writes
 * next; workers that fall behind leave a backlog and stay off it; a bounded
 * ring that fills refuses attempts. The figure follows the relative speed of
 * the two sides, so a change to either moves it without the cost of an
 * enqueue having changed. Read it with its accepted share and as a
 * description of this one workload; judge a change by EnqueueToBusyPool,
 * WakeLatency and the drain cases, whose regimes are fixed.
 */
template <class Pool>
void measureEnqueueToLiveWorkers(ub::PerfCase& perf, Pool& pool, int workers) {
  ASSERT_TRUE(pool.isValid());

  CompletionCount completed;
  const DelegateU8 TASK{&CompletionCount::add, &completed};
  std::size_t accepted = 0;
  const auto ATTEMPT = [&] {
    if (pool.tryEnqueue("task", TASK) == PoolStatus::SUCCESS) {
      ++accepted;
    }
  };
  const auto WAIT_FOR_ACCEPTED = [&] {
    spinUntil([&] { return completed.value.load(std::memory_order_acquire) == accepted; });
  };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      ATTEMPT();
    }
    WAIT_FOR_ACCEPTED();
  });
  const std::size_t WARMUP = accepted;

  const ub::PerfResult RESULT = perf.throughputLoop(ATTEMPT, "enqueue");
  WAIT_FOR_ACCEPTED();
  pool.shutdown();

  const std::size_t ATTEMPTS =
      static_cast<std::size_t>(perf.cycles()) * static_cast<std::size_t>(perf.repeats());
  std::printf("\n%s (%d live workers): %.1f ns per attempt, %zu of %zu attempts accepted, "
              "%zu tasks completed\n",
              perf.testName().c_str(), workers, nsPerOp(RESULT), accepted - WARMUP, ATTEMPTS,
              completed.value.load(std::memory_order_acquire) - WARMUP);
}

/* ----------------------------- Pool in-flight query ----------------------------- */

/**
 * @brief threadsRunning() on a pool whose workers are parked.
 *
 * Nothing writes the pool's task counts while the caller reads them, so the
 * figure is the query's own work: it grows with the worker count where the
 * pool keeps one count per worker.
 */
template <class Pool> void measureInFlightQueryIdle(ub::PerfCase& perf, Pool& pool, int workers) {
  ASSERT_TRUE(pool.isValid());
  constexpr auto PARK_SETTLE = std::chrono::milliseconds(20);
  std::this_thread::sleep_for(PARK_SETTLE);

  std::size_t busy = 0;
  const auto QUERY = [&] { busy += pool.threadsRunning() ? 1U : 0U; };
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      QUERY();
    }
  });
  const ub::PerfResult RESULT = perf.throughputLoop(QUERY, "query");
  pool.shutdown();

  EXPECT_EQ(busy, 0U) << "an idle pool reported a task in flight";
  std::printf("\n%s (%d parked worker%s): %.1f ns per query\n", perf.testName().c_str(), workers,
              (workers == 1) ? "" : "s", nsPerOp(RESULT));
}

/// A task that puts itself back on its pool until told to stop, so every
/// worker starts and finishes tasks for as long as a case needs.
template <class Pool> struct Recirculating {
  Pool* pool{nullptr};
  std::atomic<bool> stop{false};

  static std::uint8_t run(void* context) noexcept {
    auto* self = static_cast<Recirculating*>(context);
    if (!self->stop.load(std::memory_order_acquire)) {
      (void)self->pool->tryEnqueue("again", DelegateU8{&Recirculating::run, self});
    }
    return 0;
  }
};

/**
 * @brief threadsRunning() while every worker starts and finishes tasks.
 *
 * One task per worker keeps putting itself back on the pool, so the workers
 * write the pool's task counts as fast as they can while the caller reads
 * them. The figure is the query with the cache traffic that costs it. The
 * answer may be either value: between two tasks a pool has none in flight.
 */
template <class Pool> void measureInFlightQueryBusy(ub::PerfCase& perf, Pool& pool, int workers) {
  ASSERT_TRUE(pool.isValid());

  Recirculating<Pool> task;
  task.pool = &pool;
  bool queued = true;
  for (int w = 0; w < workers; ++w) {
    queued = (pool.tryEnqueue("again", DelegateU8{&Recirculating<Pool>::run, &task}) ==
              PoolStatus::SUCCESS) &&
             queued;
  }

  std::size_t busy = 0;
  const auto QUERY = [&] { busy += pool.threadsRunning() ? 1U : 0U; };
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      QUERY();
    }
  });
  busy = 0;
  const ub::PerfResult RESULT = perf.throughputLoop(QUERY, "query");

  task.stop.store(true, std::memory_order_release);
  pool.shutdown();

  EXPECT_TRUE(queued) << "the pool refused the recirculating tasks";
  const std::size_t QUERIES =
      static_cast<std::size_t>(perf.cycles()) * static_cast<std::size_t>(perf.repeats());
  std::printf("\n%s (%d working workers): %.1f ns per query, %zu of %zu saw a task in flight\n",
              perf.testName().c_str(), workers, nsPerOp(RESULT), busy, QUERIES);
}

/// When a wake-latency sample's task started, written by the worker.
struct WakeSample {
  std::atomic<bool> started{false};
  std::chrono::steady_clock::time_point at;

  static std::uint8_t stamp(void* context) noexcept {
    auto* sample = static_cast<WakeSample*>(context);
    sample->at = std::chrono::steady_clock::now();
    sample->started.store(true, std::memory_order_release);
    return 0;
  }
};

/**
 * @brief Time from an enqueue to the task starting on a worker that was asleep.
 *
 * Each sample enqueues one task to a one-worker pool that has had nothing
 * to do for WAKE_SETTLE, long enough for the worker to be parked, and takes
 * the time from just before the enqueue to the task's first instruction.
 * The pause between samples is not measured, which the harness's own loops
 * cannot leave out, so the case times itself: a repeat is the median of its
 * samples, and the row is built from the repeats like any other.
 */
template <class Pool> void measureWakeLatency(ub::PerfCase& perf, Pool& pool) {
  ASSERT_TRUE(pool.isValid());
  constexpr auto WAKE_SETTLE = std::chrono::microseconds(200);
  const int SAMPLES = std::min(perf.cycles(), 1000);

  std::vector<double> repeatMediansUs;
  repeatMediansUs.reserve(static_cast<std::size_t>(perf.repeats()));
  std::vector<double> samplesUs(static_cast<std::size_t>(SAMPLES));
  WakeSample sample;

  for (int repeat = -1; repeat < perf.repeats(); ++repeat) { // repeat -1 is the warmup
    for (double& latencyUs : samplesUs) {
      std::this_thread::sleep_for(WAKE_SETTLE);
      sample.started.store(false, std::memory_order_release);
      const auto BEFORE = std::chrono::steady_clock::now();
      if (pool.tryEnqueue("wake", DelegateU8{&WakeSample::stamp, &sample}) != PoolStatus::SUCCESS) {
        pool.shutdown();
        FAIL() << "the pool refused a task";
      }
      spinUntil([&] { return sample.started.load(std::memory_order_acquire); });
      latencyUs = std::chrono::duration<double, std::micro>(sample.at - BEFORE).count();
    }
    if (repeat >= 0) {
      std::sort(samplesUs.begin(), samplesUs.end());
      repeatMediansUs.push_back(samplesUs[samplesUs.size() / 2]);
    }
  }
  pool.shutdown();

  const ub::Stats STATS = ub::summarize(repeatMediansUs);
  const double PER_SECOND = (STATS.median > 0.0) ? 1e6 / STATS.median : 0.0;
  ub::PerfRegistry::instance().set(
      ub::buildPerfRow(perf.testName(), perf.config(), 1, 1, STATS, PER_SECOND));

  std::printf("\n%s: %.2f us from enqueue to task start (median of %d repeats of %d samples; "
              "repeats from %.2f to %.2f us)\n",
              perf.testName().c_str(), STATS.median, perf.repeats(), SAMPLES, STATS.min, STATS.max);
}

} // namespace

/* ----------------------------- LockFreeQueue Tests ----------------------------- */

/**
 * @brief Single-threaded LockFreeQueue throughput.
 *
 * Measures push/pop cycle performance without contention.
 * This establishes the baseline lock-free overhead.
 */
PERF_TEST(LockFreeQueue, SingleThreadThroughput) {
  UB_PERF_GUARD(perf);

  LockFreeQueue<int> queue(1024);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)queue.tryPush(i);
      int out = 0;
      (void)queue.tryPop(out);
    }
  });

  volatile int sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        (void)queue.tryPush(42);
        int out = 0;
        (void)queue.tryPop(out);
        sink = out;
      },
      "push_pop");

  std::printf("\nLockFreeQueue single-thread: %.0f push+pop pairs/s (%.1f ns per pair)\n",
              result.callsPerSecond, nsPerOp(result));
}

/**
 * @brief Multi-producer multi-consumer LockFreeQueue throughput.
 *
 * Tests contention behavior with multiple threads competing for access.
 */
PERF_TEST(LockFreeQueue, MPMCContention) {
  UB_PERF_GUARD(perf);

  LockFreeQueue<int> queue(4096);
  std::atomic<long long> totalOps{0};

  auto result = perf.contentionRun(
      [&] {
        if (queue.tryPush(42)) {
          int out = 0;
          if (queue.tryPop(out)) {
            totalOps.fetch_add(1, std::memory_order_relaxed);
          }
        }
      },
      "mpmc");

  std::printf("\nLockFreeQueue MPMC (%d threads): %.0f ops/s\n", perf.threads(),
              result.callsPerSecond);
}

/* ----------------------------- SPSCQueue Tests ----------------------------- */

/**
 * @brief Single-threaded SPSCQueue throughput.
 *
 * Measures the fast path where producer and consumer are the same thread.
 */
PERF_TEST(SPSCQueue, SingleThreadThroughput) {
  UB_PERF_GUARD(perf);

  SPSCQueue<int> queue(1024);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)queue.tryPush(i);
      int out = 0;
      (void)queue.tryPop(out);
    }
  });

  volatile int sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        (void)queue.tryPush(42);
        int out = 0;
        (void)queue.tryPop(out);
        sink = out;
      },
      "push_pop");

  std::printf("\nSPSCQueue single-thread: %.0f push+pop pairs/s (%.1f ns per pair)\n",
              result.callsPerSecond, nsPerOp(result));
}

/**
 * @brief SPSCQueue transfer between two threads: the intended use.
 *
 * A consumer thread pops for the whole case. One pass pushes `cycles()`
 * items and ends when the consumer has taken the last of them, so the
 * figure is the time per item moved from one thread to the other. Neither
 * side sleeps or enters the kernel while the other makes progress: the
 * producer retries a full queue and the consumer an empty one. The consumer
 * publishes its count with a plain store to a line of its own.
 */
PERF_TEST(SPSCQueue, ProducerConsumer) {
  UB_PERF_GUARD(perf);

  SPSCQueue<int> queue(4096);
  std::atomic<bool> done{false};
  apex::concurrency::cache::AlignCl<std::atomic<std::size_t>> consumed{0};

  std::thread consumer([&] {
    int value = 0;
    std::size_t taken = 0;
    std::size_t misses = 0;
    while (!done.load(std::memory_order_relaxed)) {
      if (queue.tryPop(value)) {
        consumed.value.store(++taken, std::memory_order_release);
        misses = 0;
      } else if (++misses == RETRIES_BEFORE_YIELD) {
        std::this_thread::yield();
        misses = 0;
      }
    }
  });

  std::size_t pushed = 0;
  const auto PASS = [&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      std::size_t misses = 0;
      while (!queue.tryPush(i)) {
        if (++misses == RETRIES_BEFORE_YIELD) {
          std::this_thread::yield();
          misses = 0;
        }
      }
    }
    pushed += static_cast<std::size_t>(perf.cycles());
    spinUntil([&] { return consumed.value.load(std::memory_order_acquire) == pushed; });
  };

  perf.warmup(PASS);
  auto result = perf.measured(PASS, "transfer");

  done.store(true);
  consumer.join();

  std::printf("\nSPSCQueue producer-consumer: %.0f items/s (%.1f ns per item)\n",
              result.callsPerSecond, nsPerOp(result));
}

/* ----------------------------- RingBuffer Tests ----------------------------- */

/**
 * @brief Single-threaded RingBuffer throughput.
 *
 * Measures push/pop cycle without any synchronization overhead.
 * This is the fastest possible queue - zero atomics.
 */
PERF_TEST(RingBuffer, SingleThreadThroughput) {
  UB_PERF_GUARD(perf);

  RingBuffer<int> buffer(1024);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)buffer.tryPush(i);
      int out = 0;
      (void)buffer.tryPop(out);
    }
  });

  volatile int sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        (void)buffer.tryPush(42);
        int out = 0;
        (void)buffer.tryPop(out);
        sink = out;
      },
      "push_pop");

  std::printf("\nRingBuffer single-thread: %.0f push+pop pairs/s (%.1f ns per pair)\n",
              result.callsPerSecond, nsPerOp(result));
}

/**
 * @brief RingBuffer vs SPSCQueue comparison.
 *
 * Demonstrates the performance benefit of using RingBuffer for single-threaded
 * workloads vs SPSCQueue which has atomic overhead.
 */
PERF_TEST(RingBuffer, VsSPSCQueue) {
  UB_PERF_GUARD(perf);

  RingBuffer<int> ringBuffer(1024);
  SPSCQueue<int> spscQueue(1024);

  std::printf("\n=== RingBuffer vs SPSCQueue (single-thread) ===\n");

  // RingBuffer
  volatile int sink = 0;
  auto ringResult = perf.throughputLoop(
      [&] {
        (void)ringBuffer.tryPush(42);
        int out = 0;
        (void)ringBuffer.tryPop(out);
        sink = out;
      },
      "ringbuffer");

  std::printf("RingBuffer:  %.0f ops/s\n", ringResult.callsPerSecond);

  // SPSCQueue
  auto spscResult = perf.throughputLoop(
      [&] {
        (void)spscQueue.tryPush(42);
        int out = 0;
        (void)spscQueue.tryPop(out);
        sink = out;
      },
      "spscqueue");

  std::printf("SPSCQueue:   %.0f ops/s\n", spscResult.callsPerSecond);

  double speedup = ringResult.callsPerSecond / spscResult.callsPerSecond;
  std::printf("RingBuffer is %.2fx faster (no atomic overhead)\n", speedup);
}

/* ----------------------------- SpinLock Tests ----------------------------- */

/**
 * @brief Uncontended SpinLock throughput.
 *
 * Measures lock/unlock cycle without contention.
 */
PERF_TEST(SpinLock, Uncontended) {
  UB_PERF_GUARD(perf);

  SpinLock lock;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      lock.lock();
      lock.unlock();
    }
  });

  volatile int sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        lock.lock();
        sink = sink + 1;
        lock.unlock();
      },
      "lock_unlock");

  std::printf("\nSpinLock uncontended: %.0f lock+unlock pairs/s (%.1f ns per pair)\n",
              result.callsPerSecond, nsPerOp(result));
}

/**
 * @brief Contended SpinLock throughput.
 *
 * Tests performance when multiple threads compete for the lock.
 */
PERF_TEST(SpinLock, Contended) {
  UB_PERF_GUARD(perf);

  SpinLock lock;
  std::atomic<long long> counter{0};

  auto result = perf.contentionRun(
      [&] {
        lock.lock();
        counter.fetch_add(1, std::memory_order_relaxed);
        lock.unlock();
      },
      "contended");

  std::printf("\nSpinLock contended (%d threads): %.0f ops/s\n", perf.threads(),
              result.callsPerSecond);
}

/**
 * @brief Compare SpinLock vs std::mutex under contention.
 */
PERF_TEST(SpinLock, VsMutex) {
  UB_PERF_GUARD(perf);

  SpinLock spinLock;
  std::mutex stdMutex;
  std::atomic<long long> counter{0};

  std::printf("\n=== SpinLock vs std::mutex ===\n");

  // SpinLock
  auto spinResult = perf.contentionRun(
      [&] {
        spinLock.lock();
        counter.fetch_add(1, std::memory_order_relaxed);
        spinLock.unlock();
      },
      "spinlock");

  std::printf("SpinLock:   %.0f ops/s\n", spinResult.callsPerSecond);

  // std::mutex
  counter.store(0);
  auto mutexResult = perf.contentionRun(
      [&] {
        std::lock_guard<std::mutex> guard(stdMutex);
        counter.fetch_add(1, std::memory_order_relaxed);
      },
      "mutex");

  std::printf("std::mutex: %.0f ops/s\n", mutexResult.callsPerSecond);

  double speedup = spinResult.callsPerSecond / mutexResult.callsPerSecond;
  std::printf("SpinLock is %.2fx %s\n", speedup > 1 ? speedup : 1 / speedup,
              speedup > 1 ? "faster" : "slower");
}

/* ----------------------------- Semaphore Tests ----------------------------- */

/**
 * @brief Uncontended Semaphore acquire/release throughput.
 *
 * Tests the atomic fast-path optimization.
 */
PERF_TEST(Semaphore, Uncontended) {
  UB_PERF_GUARD(perf);

  Semaphore sem(1000);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)sem.tryAcquire();
      sem.release();
    }
  });

  volatile bool sink = false;
  auto result = perf.throughputLoop(
      [&] {
        sink = sem.tryAcquire();
        sem.release();
      },
      "acquire_release");

  std::printf("\nSemaphore uncontended: %.0f acquire+release pairs/s (%.1f ns per pair)\n",
              result.callsPerSecond, nsPerOp(result));
}

/**
 * @brief Contended Semaphore signaling throughput.
 *
 * Multiple threads competing for a limited resource.
 */
PERF_TEST(Semaphore, Contended) {
  UB_PERF_GUARD(perf);

  Semaphore sem(4); // Limited permits
  std::atomic<long long> acquired{0};

  auto result = perf.contentionRun(
      [&] {
        if (sem.tryAcquire()) {
          acquired.fetch_add(1, std::memory_order_relaxed);
          sem.release();
        }
      },
      "signaling");

  std::printf("\nSemaphore contended (%d threads, 4 permits): %.0f signals/s\n", perf.threads(),
              result.callsPerSecond);
}

/* ----------------------------- Latch Tests ----------------------------- */

/**
 * @brief Latch round: four threads arrive at a fresh latch and all continue.
 *
 * Three helper threads live across the rounds and enter each round's latch
 * once it is published, so a round times the four arrivals and the wakes
 * and no thread creation. A latch is single use: every round has its own,
 * built before the measurement.
 */
PERF_TEST(Latch, CountdownLatency) {
  UB_PERF_GUARD(perf);

  constexpr int PARTIES = 4;
  const std::size_t PASSES = static_cast<std::size_t>(perf.repeats()) + 1; // one warmup pass
  const std::size_t ROUNDS = PASSES * static_cast<std::size_t>(perf.cycles());

  std::deque<Latch> latches;
  for (std::size_t round = 0; round < ROUNDS; ++round) {
    latches.emplace_back(PARTIES);
  }

  std::atomic<std::size_t> published{0}; // Rounds the helpers may enter.
  std::vector<std::thread> helpers;
  helpers.reserve(PARTIES - 1);
  for (int t = 0; t < PARTIES - 1; ++t) {
    helpers.emplace_back([&] {
      for (std::size_t round = 0; round < ROUNDS; ++round) {
        spinUntil([&] { return published.load(std::memory_order_acquire) > round; });
        latches[round].arriveAndWait();
      }
    });
  }

  std::size_t next = 0;
  const auto PASS = [&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      published.store(next + 1, std::memory_order_release);
      latches[next].arriveAndWait();
      ++next;
    }
  };

  PASS(); // warmup
  auto result = perf.measured(PASS, "round");

  for (auto& helper : helpers) {
    helper.join();
  }

  std::printf("\nLatch round (%d threads): %.0f rounds/s (%.1f us per round)\n", PARTIES,
              result.callsPerSecond, nsPerOp(result) / 1000.0);
}

/**
 * @brief Latch tryWait polling on a triggered latch: the true result, one
 *        atomic load and a mutex round trip. The nonzero-count path is the
 *        load alone.
 */
PERF_TEST(Latch, TryWaitThroughput) {
  UB_PERF_GUARD(perf);

  Latch latch(0); // Already triggered

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile bool ready = latch.tryWait();
      (void)ready;
    }
  });

  volatile bool sink = false;
  auto result = perf.throughputLoop([&] { sink = latch.tryWait(); }, "tryWait");

  std::printf("\nLatch tryWait (triggered, mutex round trip): %.0f ops/s (%.1f ns/op)\n",
              result.callsPerSecond, nsPerOp(result));
}

/* ----------------------------- Barrier Tests ----------------------------- */

/**
 * @brief Barrier phase: four threads arrive and all continue.
 *
 * Three helper threads arrive phase after phase; the measuring thread is the
 * fourth party, so one call is one phase. The helpers leave after the phase
 * whose number the measuring thread writes before it arrives for it, which
 * no helper can pass without that arrival.
 */
PERF_TEST(Barrier, PhaseThroughput) {
  UB_PERF_GUARD(perf);

  constexpr int PARTIES = 4;
  Barrier barrier(PARTIES);
  std::atomic<std::size_t> lastPhase{std::numeric_limits<std::size_t>::max()};

  std::vector<std::thread> helpers;
  helpers.reserve(PARTIES - 1);
  for (int t = 0; t < PARTIES - 1; ++t) {
    helpers.emplace_back([&] {
      for (std::size_t phase = 1;; ++phase) {
        barrier.arriveAndWait();
        if (phase == lastPhase.load(std::memory_order_acquire)) {
          return;
        }
      }
    });
  }

  std::size_t phases = 0;
  const auto PHASE = [&] {
    barrier.arriveAndWait();
    ++phases;
  };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      PHASE();
    }
  });
  auto result = perf.throughputLoop(PHASE, "phase");

  lastPhase.store(phases + 1, std::memory_order_release);
  barrier.arriveAndWait();
  for (auto& helper : helpers) {
    helper.join();
  }

  std::printf("\nBarrier phase (%d threads): %.0f phases/s (%.1f us per phase)\n", PARTIES,
              result.callsPerSecond, nsPerOp(result) / 1000.0);
}

/**
 * @brief Barrier generation query (lock-free fast-path).
 */
PERF_TEST(Barrier, GenerationQuery) {
  UB_PERF_GUARD(perf);

  Barrier barrier(2);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile std::size_t gen = barrier.generation();
      (void)gen;
    }
  });

  volatile std::size_t sink = 0;
  auto result = perf.throughputLoop([&] { sink = barrier.generation(); }, "generation");

  std::printf("\nBarrier generation() (lock-free): %.0f ops/s (%.1f ns/op)\n",
              result.callsPerSecond, nsPerOp(result));
}

/* ----------------------------- ThreadPool Tests ----------------------------- */

/** @brief ThreadPool enqueue cost for the producer while its four workers are busy. */
PERF_TEST(ThreadPool, EnqueueToBusyPool) {
  UB_PERF_GUARD(perf);
  ThreadPool pool(4);
  measureEnqueueToBusyPool(perf, pool, 4);
}

/** @brief ThreadPool enqueue attempts while its four workers take the tasks (diagnostic). */
PERF_TEST(ThreadPool, EnqueueToLiveWorkers) {
  UB_PERF_GUARD(perf);
  ThreadPool pool(4);
  measureEnqueueToLiveWorkers(perf, pool, 4);
}

/** @brief ThreadPool time from an enqueue to the task starting on a sleeping worker. */
PERF_TEST(ThreadPool, WakeLatency) {
  UB_PERF_GUARD(perf);
  ThreadPool pool(1);
  measureWakeLatency(perf, pool);
}

/** @brief ThreadPool worker cost per queued task, one worker. */
PERF_TEST(ThreadPool, DrainOneWorker) {
  UB_PERF_GUARD(perf);
  ThreadPool pool(1);
  measureDrain(perf, pool, 1);
}

/** @brief ThreadPool worker cost per queued task, four workers sharing the queue. */
PERF_TEST(ThreadPool, DrainFourWorkers) {
  UB_PERF_GUARD(perf);
  ThreadPool pool(4);
  measureDrain(perf, pool, 4);
}

/* ----------------------------- ThreadPoolLockFree Tests ----------------------------- */

/** @brief ThreadPoolLockFree enqueue cost for the producer while its four workers are busy. */
PERF_TEST(ThreadPoolLockFree, EnqueueToBusyPool) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(4, drainTasks(perf, 4));
  measureEnqueueToBusyPool(perf, pool, 4);
}

/** @brief ThreadPoolLockFree enqueue attempts while its four workers take the tasks (diagnostic).
 */
PERF_TEST(ThreadPoolLockFree, EnqueueToLiveWorkers) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(4, 4096);
  measureEnqueueToLiveWorkers(perf, pool, 4);
}

/** @brief ThreadPoolLockFree time from an enqueue to the task starting on a sleeping worker. */
PERF_TEST(ThreadPoolLockFree, WakeLatency) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(1);
  measureWakeLatency(perf, pool);
}

/** @brief ThreadPoolLockFree worker cost per queued task, one worker. */
PERF_TEST(ThreadPoolLockFree, DrainOneWorker) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(1, drainTasks(perf, 1));
  measureDrain(perf, pool, 1);
}

/** @brief ThreadPoolLockFree worker cost per queued task, four workers sharing the ring. */
PERF_TEST(ThreadPoolLockFree, DrainFourWorkers) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(4, drainTasks(perf, 4));
  measureDrain(perf, pool, 4);
}

/** @brief ThreadPoolLockFree threadsRunning() with one parked worker. */
PERF_TEST(ThreadPoolLockFree, InFlightQueryIdleOneWorker) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(1);
  measureInFlightQueryIdle(perf, pool, 1);
}

/** @brief ThreadPoolLockFree threadsRunning() with four parked workers. */
PERF_TEST(ThreadPoolLockFree, InFlightQueryIdleFourWorkers) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(4);
  measureInFlightQueryIdle(perf, pool, 4);
}

/** @brief ThreadPoolLockFree threadsRunning() with sixteen parked workers. */
PERF_TEST(ThreadPoolLockFree, InFlightQueryIdleSixteenWorkers) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(16);
  measureInFlightQueryIdle(perf, pool, 16);
}

/** @brief ThreadPoolLockFree threadsRunning() while four workers start and finish tasks. */
PERF_TEST(ThreadPoolLockFree, InFlightQueryBusyFourWorkers) {
  UB_PERF_GUARD(perf);
  ThreadPoolLockFree pool(4);
  measureInFlightQueryBusy(perf, pool, 4);
}

/**
 * @brief ThreadPoolLockFree QUEUE_FULL rejection rate.
 *
 * Tests bounded queue rejection behavior under load.
 */
PERF_TEST(ThreadPoolLockFree, BoundedQueueRejection) {
  UB_PERF_GUARD(perf);

  // Small queue to force rejections
  ThreadPoolLockFree pool(1, 16);
  std::atomic<int> executed{0};
  std::atomic<int> rejected{0};

  // Slow task to create backpressure
  auto slowTask = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                               auto* c = static_cast<std::atomic<int>*>(ctx);
                               // Simulate work with atomic increments.
                               std::atomic<int> dummy{0};
                               for (int i = 0; i < 1000; ++i) {
                                 dummy.fetch_add(1, std::memory_order_relaxed);
                               }
                               c->fetch_add(1, std::memory_order_relaxed);
                               return 0;
                             },
                             &executed};

  const int ATTEMPTS = 1000;
  for (int i = 0; i < ATTEMPTS; ++i) {
    PoolStatus status = pool.tryEnqueue("slow", slowTask);
    if (status == PoolStatus::QUEUE_FULL) {
      rejected.fetch_add(1, std::memory_order_relaxed);
    }
  }

  pool.shutdown();

  int total = executed.load() + rejected.load();
  double rejectRate = 100.0 * rejected.load() / ATTEMPTS;

  std::printf("\nThreadPoolLockFree bounded queue:\n");
  std::printf("  Attempts: %d, Executed: %d, Rejected: %d (%.1f%%)\n", ATTEMPTS, executed.load(),
              rejected.load(), rejectRate);

  // Expect some rejections due to bounded queue with slow tasks
  EXPECT_EQ(total, ATTEMPTS) << "All attempts should be accounted for";
  EXPECT_GT(rejected.load(), 0) << "Expected some rejections with small queue";
}

namespace {

/// Sets this binary's thread default before PERF_MAIN()'s main() parses the
/// flags, so `--threads` overrides it and the CSV rows carry the thread
/// count the contention cases ran with.
const bool THREAD_DEFAULT_SET = []() noexcept {
  ub::detail::perfConfigSingleton().threads = DEFAULT_THREADS;
  return true;
}();

} // namespace

PERF_MAIN()
