/**
 * @file Semaphore_uTest.cpp
 * @brief Unit tests for concurrency::Semaphore.
 *
 * Notes:
 *  - Tests verify acquire/release semantics, blocking behavior, and timeouts.
 *  - Concurrent tests use multiple threads to verify synchronization.
 */

#include "src/utilities/concurrency/inc/Semaphore.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using apex::concurrency::Semaphore;

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

/** @test acquire blocks until release */
TEST_F(SemaphoreTest, AcquireBlocksUntilRelease) {
  Semaphore sem(0);
  std::atomic<bool> acquired{false};

  std::thread waiter([&]() {
    sem.acquire();
    acquired.store(true, std::memory_order_release);
  });

  // Give waiter time to block
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(acquired.load(std::memory_order_acquire));

  // Release should unblock waiter
  sem.release();
  waiter.join();

  EXPECT_TRUE(acquired.load(std::memory_order_acquire));
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
