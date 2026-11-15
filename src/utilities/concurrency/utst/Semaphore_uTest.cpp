/**
 * @file Semaphore_uTest.cpp
 * @brief Unit tests for concurrency::Semaphore.
 *
 * Notes:
 *  - Tests verify acquire/release semantics, blocking behavior, and timeouts.
 *  - Concurrent tests use multiple threads to verify synchronization.
 */

#include "src/utilities/concurrency/inc/Semaphore.hpp"
#include "src/utilities/concurrency/utst/support/ThreadProbe.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

using apex::concurrency::Semaphore;
using apex::concurrency::test::settles;
using apex::concurrency::test::threadsAsleepIn;

/// The largest count the semaphore supports.
constexpr std::size_t MAX_COUNT = std::numeric_limits<std::uint32_t>::max();

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default semaphore has count zero */
TEST(SemaphoreDefaultTest, DefaultCountIsZero) {
  Semaphore sem;
  EXPECT_EQ(sem.count(), 0U);
}

/** @test Semaphore with initial count stores that count */
TEST(SemaphoreDefaultTest, InitialCountStored) {
  Semaphore sem(5);
  EXPECT_EQ(sem.count(), 5U);
}

/* ----------------------------- Basic Operations Tests ----------------------------- */

class SemaphoreTest : public ::testing::Test {
protected:
  static constexpr std::size_t INITIAL_COUNT = 3;
};

/** @test tryAcquire succeeds when count > 0 */
TEST_F(SemaphoreTest, TryAcquireSucceedsWithCount) {
  Semaphore sem(INITIAL_COUNT);
  EXPECT_TRUE(sem.tryAcquire());
  EXPECT_EQ(sem.count(), INITIAL_COUNT - 1);
}

/** @test tryAcquire fails when count == 0 */
TEST_F(SemaphoreTest, TryAcquireFailsAtZero) {
  Semaphore sem(0);
  EXPECT_FALSE(sem.tryAcquire());
  EXPECT_EQ(sem.count(), 0U);
}

/** @test Multiple tryAcquire drain the count */
TEST_F(SemaphoreTest, MultipleTryAcquireDrainCount) {
  Semaphore sem(INITIAL_COUNT);

  for (std::size_t i = 0; i < INITIAL_COUNT; ++i) {
    EXPECT_TRUE(sem.tryAcquire());
  }
  EXPECT_EQ(sem.count(), 0U);
  EXPECT_FALSE(sem.tryAcquire());
}

/** @test release increments count */
TEST_F(SemaphoreTest, ReleaseIncrementsCount) {
  Semaphore sem(0);
  sem.release();
  EXPECT_EQ(sem.count(), 1U);
}

/** @test release with count adds that many */
TEST_F(SemaphoreTest, ReleaseAddsCount) {
  Semaphore sem(0);
  sem.release(5);
  EXPECT_EQ(sem.count(), 5U);
}

/** @test acquire then release maintains balance */
TEST_F(SemaphoreTest, AcquireReleaseBalance) {
  Semaphore sem(INITIAL_COUNT);

  EXPECT_TRUE(sem.tryAcquire());
  EXPECT_TRUE(sem.tryAcquire());
  sem.release();
  sem.release();

  EXPECT_EQ(sem.count(), INITIAL_COUNT);
}

/* ----------------------------- Timeout Tests ----------------------------- */

/** @test tryAcquireFor succeeds immediately when count > 0 */
TEST_F(SemaphoreTest, TryAcquireForSucceedsImmediately) {
  Semaphore sem(1);

  const auto START = std::chrono::steady_clock::now();
  const bool RESULT = sem.tryAcquireFor(std::chrono::seconds(10));
  const auto ELAPSED = std::chrono::steady_clock::now() - START;

  EXPECT_TRUE(RESULT);
  EXPECT_LT(ELAPSED, std::chrono::milliseconds(100));
  EXPECT_EQ(sem.count(), 0U);
}

/** @test tryAcquireFor times out when count == 0 */
TEST_F(SemaphoreTest, TryAcquireForTimesOut) {
  Semaphore sem(0);

  const auto START = std::chrono::steady_clock::now();
  const bool RESULT = sem.tryAcquireFor(std::chrono::milliseconds(50));
  const auto ELAPSED = std::chrono::steady_clock::now() - START;

  EXPECT_FALSE(RESULT);
  EXPECT_GE(ELAPSED, std::chrono::milliseconds(40)); // Allow some tolerance
  EXPECT_EQ(sem.count(), 0U);
}

/** @test tryAcquireFor succeeds when released during wait */
TEST_F(SemaphoreTest, TryAcquireForSucceedsOnRelease) {
  Semaphore sem(0);

  std::thread releaser([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    sem.release();
  });

  const auto START = std::chrono::steady_clock::now();
  const bool RESULT = sem.tryAcquireFor(std::chrono::seconds(1));
  const auto ELAPSED = std::chrono::steady_clock::now() - START;

  releaser.join();

  EXPECT_TRUE(RESULT);
  EXPECT_LT(ELAPSED, std::chrono::milliseconds(500));
}

/* ----------------------------- Blocking Tests ----------------------------- */

namespace {

/// Acquirers of one semaphore, with everything they touch on the heap: a case that gives up on
/// them detaches them, and they keep the state they hold.
struct Acquirers {
  Semaphore sem{0};
  std::atomic<int> entering{0};
  std::atomic<int> acquired{0};

  /// Whether `count` threads are blocked inside acquire(). Where the host does not expose a
  /// thread's blocked system call, whether that many have entered and a watch has passed.
  [[nodiscard]] bool asleep(int count) const {
    const int ASLEEP = threadsAsleepIn(&sem, sizeof(sem));
    if (ASLEEP < 0) {
      std::this_thread::sleep_for(apex::concurrency::test::WATCH);
      return entering.load() == count;
    }
    return ASLEEP == count;
  }
};

/// Start `count` threads that each acquire once.
std::vector<std::thread> startAcquirers(const std::shared_ptr<Acquirers>& state, int count) {
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    threads.emplace_back([state]() {
      state->entering.fetch_add(1, std::memory_order_acq_rel);
      state->sem.acquire();
      state->acquired.fetch_add(1, std::memory_order_acq_rel);
    });
  }
  return threads;
}

/// Join the acquirers if all of them returned, else detach them.
void finish(std::vector<std::thread>& threads, bool allReturned) {
  for (auto& t : threads) {
    if (allReturned) {
      t.join();
    } else {
      t.detach();
    }
  }
}

} // namespace

/** @test acquire() on an empty semaphore blocks, and one release() lets exactly that acquirer
 * through. */
TEST_F(SemaphoreTest, AcquireBlocksUntilRelease) {
  const auto state = std::make_shared<Acquirers>();
  auto threads = startAcquirers(state, 1);
  const bool ASLEEP = settles([&]() { return state->asleep(1); });
  EXPECT_TRUE(ASLEEP) << "the acquirer did not block on an empty semaphore";
  EXPECT_EQ(state->acquired.load(), 0);

  state->sem.release();
  const bool RETURNED = settles([&]() { return state->acquired.load() == 1; });
  finish(threads, RETURNED);
  ASSERT_TRUE(RETURNED) << "a release did not wake the blocked acquirer";
  EXPECT_EQ(state->sem.count(), 0U);
}

/** @test release(n) wakes every one of n blocked acquirers: with four threads asleep in
 * acquire(), one release(4) lets all four through. */
TEST_F(SemaphoreTest, ReleaseOfSeveralWakesEveryBlockedAcquirer) {
  constexpr int ACQUIRERS = 4;
  const auto state = std::make_shared<Acquirers>();
  auto threads = startAcquirers(state, ACQUIRERS);
  const bool ASLEEP = settles([&]() { return state->asleep(ACQUIRERS); });
  EXPECT_TRUE(ASLEEP) << "the acquirers did not all block";
  EXPECT_EQ(state->acquired.load(), 0);

  state->sem.release(ACQUIRERS);
  const bool RETURNED = settles([&]() { return state->acquired.load() == ACQUIRERS; });
  finish(threads, RETURNED);
  ASSERT_TRUE(RETURNED) << state->acquired.load() << " of " << ACQUIRERS
                        << " blocked acquirers returned after release(" << ACQUIRERS << ")";
  EXPECT_EQ(state->sem.count(), 0U);
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test Multiple threads can acquire and release */
TEST_F(SemaphoreTest, ConcurrentAcquireRelease) {
  Semaphore sem(0);
  constexpr int THREAD_COUNT = 4;
  constexpr int ITERATIONS = 1000;
  std::atomic<int> totalAcquired{0};

  // Start consumer threads
  std::vector<std::thread> consumers;
  consumers.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    consumers.emplace_back([&]() {
      for (int j = 0; j < ITERATIONS; ++j) {
        sem.acquire();
        totalAcquired.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Producer releases enough for all consumers
  const int TOTAL_RELEASES = THREAD_COUNT * ITERATIONS;
  for (int i = 0; i < TOTAL_RELEASES; ++i) {
    sem.release();
  }

  for (auto& t : consumers) {
    t.join();
  }

  EXPECT_EQ(totalAcquired.load(), TOTAL_RELEASES);
  EXPECT_EQ(sem.count(), 0U);
}

/** @test Semaphore as resource pool limiter */
TEST_F(SemaphoreTest, ResourcePoolLimiting) {
  constexpr int POOL_SIZE = 3;
  constexpr int THREAD_COUNT = 10;
  Semaphore pool(POOL_SIZE);
  std::atomic<int> currentlyHeld{0};
  std::atomic<int> maxHeld{0};

  std::vector<std::thread> workers;
  workers.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    workers.emplace_back([&]() {
      for (int j = 0; j < 100; ++j) {
        pool.acquire();

        const int HELD = currentlyHeld.fetch_add(1, std::memory_order_relaxed) + 1;
        int expected = maxHeld.load(std::memory_order_relaxed);
        while (HELD > expected &&
               !maxHeld.compare_exchange_weak(expected, HELD, std::memory_order_relaxed)) {
        }

        // Simulate work
        std::this_thread::yield();

        currentlyHeld.fetch_sub(1, std::memory_order_relaxed);
        pool.release();
      }
    });
  }

  for (auto& t : workers) {
    t.join();
  }

  // Max concurrent should never exceed pool size
  EXPECT_LE(maxHeld.load(), POOL_SIZE);
  EXPECT_EQ(pool.count(), static_cast<std::size_t>(POOL_SIZE));
}

/* ----------------------------- Count Bound Tests ----------------------------- */

/** @test The count holds the largest supported value, UINT32_MAX, as an initial count: it reads
 * back exactly and an acquire takes one from it. */
TEST_F(SemaphoreTest, HoldsTheMaximumInitialCount) {
  Semaphore sem(MAX_COUNT);
  EXPECT_EQ(sem.count(), MAX_COUNT);
  EXPECT_TRUE(sem.tryAcquire());
  EXPECT_EQ(sem.count(), MAX_COUNT - 1);
}

/** @test One release of the largest supported argument takes an empty semaphore to UINT32_MAX. */
TEST_F(SemaphoreTest, ReleaseOfTheMaximumFromZero) {
  Semaphore sem(0);
  sem.release(MAX_COUNT);
  EXPECT_EQ(sem.count(), MAX_COUNT);
  EXPECT_TRUE(sem.tryAcquire());
  EXPECT_EQ(sem.count(), MAX_COUNT - 1);
}

/** @test A release that brings the permits outstanding exactly to UINT32_MAX is within the
 * supported range: from one below the bound, release(1) reaches it and acquires count down from
 * there. */
TEST_F(SemaphoreTest, ReleaseReachesTheMaximumFromOneBelow) {
  Semaphore sem(MAX_COUNT - 1);
  sem.release(1);
  EXPECT_EQ(sem.count(), MAX_COUNT);
  EXPECT_TRUE(sem.tryAcquire());
  EXPECT_TRUE(sem.tryAcquire());
  EXPECT_EQ(sem.count(), MAX_COUNT - 2);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test count() returns consistent results */
TEST(SemaphoreDeterminismTest, CountConsistent) {
  Semaphore sem(10);
  const std::size_t FIRST = sem.count();
  const std::size_t SECOND = sem.count();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_EQ(FIRST, 10U);
}

/** @test tryAcquire is deterministic on empty semaphore */
TEST(SemaphoreDeterminismTest, TryAcquireConsistentOnEmpty) {
  Semaphore sem(0);
  const bool FIRST = sem.tryAcquire();
  const bool SECOND = sem.tryAcquire();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_FALSE(FIRST);
}
