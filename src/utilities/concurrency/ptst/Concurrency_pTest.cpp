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
 *  - Latch countdown coordination and tryWait fast-path
 *  - Barrier phase synchronization and generation query
 *  - ThreadPool / ThreadPoolLockFree enqueue throughput and rejection
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

  std::printf("\nLockFreeQueue single-thread: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000);
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

  std::printf("\nSPSCQueue single-thread: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000);
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

  std::printf("\nRingBuffer single-thread: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000);
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
        ++sink;
        lock.unlock();
      },
      "lock_unlock");

  std::printf("\nSpinLock uncontended: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000);
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

  std::printf("\nSemaphore uncontended: %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000);
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
 * @brief Latch countdown coordination latency.
 *
 * Measures time for N threads to count down and synchronize.
 */
PERF_TEST(Latch, CountdownLatency) {
  UB_PERF_GUARD(perf);

  const int NUM_THREADS = 4;
  std::vector<double> latencies;

  // Run multiple iterations to get stable measurements
  for (int rep = 0; rep < perf.repeats(); ++rep) {
    Latch latch(NUM_THREADS);
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back([&latch] { latch.arriveAndWait(); });
    }

    for (auto& t : threads) {
      t.join();
    }

    auto end = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(end - start).count();
    latencies.push_back(us);
  }

  std::sort(latencies.begin(), latencies.end());
  double median = latencies[latencies.size() / 2];
  double p10 = latencies[latencies.size() / 10];
  double p90 = latencies[latencies.size() * 9 / 10];

  std::printf("\nLatch countdown (%d threads): median=%.1f us, p10=%.1f, p90=%.1f\n", NUM_THREADS,
              median, p10, p90);
}

/**
 * @brief Latch tryWait polling (lock-free fast-path).
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

  std::printf("\nLatch tryWait (lock-free): %.0f ops/s (%.1f ns/op)\n", result.callsPerSecond,
              result.stats.median * 1000);
}

/* ----------------------------- Barrier Tests ----------------------------- */

/**
 * @brief Barrier phase synchronization throughput.
 *
 * Measures how many synchronization phases can be completed per second.
 */
PERF_TEST(Barrier, PhaseThroughput) {
  UB_PERF_GUARD(perf);

  const int NUM_THREADS = 4;
  const int PHASES_PER_REP = 100;
  std::vector<double> phaseRates;

  for (int rep = 0; rep < perf.repeats(); ++rep) {
    Barrier barrier(NUM_THREADS);
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back([&barrier, &stop] {
        for (int p = 0; p < PHASES_PER_REP && !stop.load(std::memory_order_relaxed); ++p) {
          barrier.arriveAndWait();
        }
      });
    }

    for (auto& t : threads) {
      t.join();
    }

    auto end = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    double phasesPerSec = PHASES_PER_REP / secs;
    phaseRates.push_back(phasesPerSec);
  }

  std::sort(phaseRates.begin(), phaseRates.end());
  double median = phaseRates[phaseRates.size() / 2];

  std::printf("\nBarrier phase sync (%d threads): %.0f phases/s (%.1f us/phase)\n", NUM_THREADS,
              median, 1'000'000.0 / median);
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
              result.callsPerSecond, result.stats.median * 1000);
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

  auto result = perf.throughputLoop([&] { pool.enqueueTask("bench", task); }, "enqueue");

  // Wait for tasks to complete
  while (counter.load() < static_cast<int>(perf.cycles() * perf.repeats())) {
    std::this_thread::yield();
  }

  std::printf("\nThreadPool enqueue: %.0f ops/s (%.1f us/op)\n", result.callsPerSecond,
              result.stats.median);
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

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 10; ++i) {
      (void)pool.tryEnqueue("warmup", task);
    }
    while (counter.load() < perf.cycles() / 10) {
      std::this_thread::yield();
    }
    counter.store(0);
  });

  auto result = perf.throughputLoop([&] { (void)pool.tryEnqueue("bench", task); }, "enqueue");

  // Wait for tasks to complete
  while (counter.load() < static_cast<int>(perf.cycles() * perf.repeats())) {
    std::this_thread::yield();
  }

  std::printf("\nThreadPoolLockFree enqueue: %.0f ops/s (%.1f us/op)\n", result.callsPerSecond,
              result.stats.median);
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

  // ThreadPoolLockFree
  {
    ThreadPoolLockFree pool(4, 4096);
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles() / 10; ++i) {
        (void)pool.tryEnqueue("warmup", task);
      }
      while (counter.load() < perf.cycles() / 10) {
        std::this_thread::yield();
      }
      counter.store(0);
    });

    auto result =
        perf.throughputLoop([&] { (void)pool.tryEnqueue("bench", task); }, "lockfree_pool");
    std::printf("ThreadPoolLockFree:     %.0f ops/s\n", result.callsPerSecond);

    while (counter.load() < static_cast<int>(perf.cycles() * perf.repeats())) {
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

PERF_MAIN()
