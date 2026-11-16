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
 *  - ThreadPool / ThreadPoolLockFree enqueue throughput, worker cost per
 *    queued task (one and four workers) and rejection
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

#include <sys/resource.h>

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

/// Nanoseconds per operation from a result's call rate. A call that performs
/// several operations passes how many.
double nsPerOp(const ub::PerfResult& result, double opsPerCall = 1.0) {
  const double OPS_PER_SECOND = result.callsPerSecond * opsPerCall;
  return (OPS_PER_SECOND > 0.0) ? 1e9 / OPS_PER_SECOND : 0.0;
}

/// The calling thread's time in the kernel so far, in nanoseconds.
double kernelNsOfThisThread() {
  rusage usage{};
  getrusage(RUSAGE_THREAD, &usage);
  return static_cast<double>(usage.ru_stime.tv_sec) * 1e9 +
         static_cast<double>(usage.ru_stime.tv_usec) * 1e3;
}

/// The producer's kernel time per enqueue over a measured phase that began
/// at `before`. An enqueue enters the kernel only to wake a worker, so this
/// is the part of the enqueue figure that is wakes: near zero while the
/// workers stay busy, most of the figure while they keep going idle.
double kernelNsPerEnqueue(double before, const ub::PerfCase& perf) {
  const double ENQUEUES = static_cast<double>(perf.cycles()) * perf.repeats();
  return (kernelNsOfThisThread() - before) / ENQUEUES;
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
 * @brief Worker cost per task with the work already queued.
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

  std::printf("\n%s drain (%d worker%s): %.0f tasks/s, %.1f ns per task, "
              "%.1f ns of worker time per task\n",
              perf.testName().c_str(), workers, (workers == 1) ? "" : "s", RESULT.callsPerSecond,
              nsPerOp(RESULT), nsPerOp(RESULT) * workers);
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
 * @brief Producer-consumer SPSCQueue throughput.
 *
 * Tests the intended use case: one producer thread, one consumer thread.
 */
PERF_TEST(SPSCQueue, ProducerConsumer) {
  UB_PERF_GUARD(perf);

  SPSCQueue<int> queue(4096);
  std::atomic<bool> done{false};
  std::atomic<long long> consumed{0};

  // Consumer thread
  std::thread consumer([&] {
    int value = 0;
    while (!done.load(std::memory_order_relaxed) || !queue.empty()) {
      if (queue.tryPop(value)) {
        consumed.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      while (!queue.tryPush(i)) {
        std::this_thread::yield();
      }
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        while (!queue.tryPush(42)) {
          std::this_thread::yield();
        }
      },
      "produce");

  done.store(true);
  consumer.join();

  std::printf("\nSPSCQueue producer-consumer: %.0f ops/s\n", result.callsPerSecond);
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

/**
 * @brief ThreadPool task enqueue throughput.
 */
PERF_TEST(ThreadPool, EnqueueThroughput) {
  UB_PERF_GUARD(perf);

  ThreadPool pool(4);
  std::atomic<int> counter{0};

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      pool.enqueueTask("warmup", task);
    }
    while (counter.load() < perf.cycles() / 10) {
      std::this_thread::yield();
    }
    counter.store(0);
  });

  const double KERNEL_BEFORE = kernelNsOfThisThread();
  auto result = perf.throughputLoop([&] { pool.enqueueTask("bench", task); }, "enqueue");
  const double KERNEL_NS = kernelNsPerEnqueue(KERNEL_BEFORE, perf);

  // Wait for tasks to complete
  while (counter.load() < static_cast<int>(perf.cycles() * perf.repeats())) {
    std::this_thread::yield();
  }

  std::printf("\nThreadPool enqueue: %.0f ops/s (%.1f ns/op, %.0f of them in the kernel)\n",
              result.callsPerSecond, nsPerOp(result), KERNEL_NS);
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

/**
 * @brief ThreadPoolLockFree enqueue throughput.
 *
 * Tests lock-free submission path with bounded queue.
 */
PERF_TEST(ThreadPoolLockFree, EnqueueThroughput) {
  UB_PERF_GUARD(perf);

  ThreadPoolLockFree pool(4, 4096);
  std::atomic<int> counter{0};

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  // A full ring rejects an enqueue, so the waits below count the tasks the
  // pool accepted, not the attempts: waiting for a dropped task never ends.
  int accepted = 0;
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      if (pool.tryEnqueue("warmup", task) == PoolStatus::SUCCESS) {
        ++accepted;
      }
    }
    while (counter.load() < accepted) {
      std::this_thread::yield();
    }
    counter.store(0);
    accepted = 0;
  });

  const double KERNEL_BEFORE = kernelNsOfThisThread();
  auto result = perf.throughputLoop(
      [&] {
        if (pool.tryEnqueue("bench", task) == PoolStatus::SUCCESS) {
          ++accepted;
        }
      },
      "enqueue");
  const double KERNEL_NS = kernelNsPerEnqueue(KERNEL_BEFORE, perf);

  // Wait for the accepted tasks to complete
  while (counter.load() < accepted) {
    std::this_thread::yield();
  }

  std::printf("\nThreadPoolLockFree enqueue: %.0f ops/s (%.1f ns/op, %.0f of them in the "
              "kernel), %d of %d accepted\n",
              result.callsPerSecond, nsPerOp(result), KERNEL_NS, accepted,
              perf.cycles() * perf.repeats());
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

/**
 * @brief Compare ThreadPool vs ThreadPoolLockFree enqueue throughput.
 */
PERF_TEST(ThreadPoolLockFree, VsThreadPool) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== ThreadPool vs ThreadPoolLockFree ===\n");

  std::atomic<int> counter{0};

  auto task = DelegateU8{[](void* ctx) noexcept -> std::uint8_t {
                           auto* c = static_cast<std::atomic<int>*>(ctx);
                           c->fetch_add(1, std::memory_order_relaxed);
                           return 0;
                         },
                         &counter};

  // ThreadPool (mutex-based)
  {
    ThreadPool pool(4);
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles() / 10; ++i) {
        pool.enqueueTask("warmup", task);
      }
      while (counter.load() < perf.cycles() / 10) {
        std::this_thread::yield();
      }
      counter.store(0);
    });

    auto result = perf.throughputLoop([&] { pool.enqueueTask("bench", task); }, "mutex_pool");
    std::printf("ThreadPool (mutex):     %.0f ops/s\n", result.callsPerSecond);

    while (counter.load() < static_cast<int>(perf.cycles() * perf.repeats())) {
      std::this_thread::yield();
    }
    counter.store(0);
  }

  // ThreadPoolLockFree (its ring is bounded: wait for the accepted tasks)
  {
    ThreadPoolLockFree pool(4, 4096);
    int accepted = 0;
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles() / 10; ++i) {
        if (pool.tryEnqueue("warmup", task) == PoolStatus::SUCCESS) {
          ++accepted;
        }
      }
      while (counter.load() < accepted) {
        std::this_thread::yield();
      }
      counter.store(0);
      accepted = 0;
    });

    auto result = perf.throughputLoop(
        [&] {
          if (pool.tryEnqueue("bench", task) == PoolStatus::SUCCESS) {
            ++accepted;
          }
        },
        "lockfree_pool");
    std::printf("ThreadPoolLockFree:     %.0f ops/s (%d of %d accepted)\n", result.callsPerSecond,
                accepted, perf.cycles() * perf.repeats());

    while (counter.load() < accepted) {
      std::this_thread::yield();
    }
  }
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

/// PERF_MAIN() with this binary's thread default set before the flags are
/// parsed, so the CSV rows carry the thread count the contention cases ran.
int main(int argc, char** argv) {
  auto& cfg = ub::detail::perfConfigSingleton();
  cfg.threads = DEFAULT_THREADS;
  ub::parsePerfFlags(cfg, &argc, argv);
  ub::setGlobalPerfConfig(&cfg);
  ub::installPerfEventListener(cfg);
  ::testing::InitGoogleTest(&argc, argv);
  const int RESULT = RUN_ALL_TESTS();
  VERNIER_WARN_IF_NO_TESTS_RAN_UNDER_PROFILE(cfg);
  return RESULT;
}
