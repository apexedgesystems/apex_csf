/**
 * @file Latch_uTest.cpp
 * @brief Unit tests for concurrency::Latch.
 *
 * Notes:
 *  - Tests verify countdown semantics and wait behavior.
 *  - Concurrent tests use multiple threads to verify synchronization.
 */

#include "src/utilities/concurrency/inc/Latch.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using apex::concurrency::Latch;

/* ----------------------------- Basic Operations Tests ----------------------------- */

class LatchTest : public ::testing::Test {};

/** @test Latch with count 0 is immediately triggered */
TEST_F(LatchTest, ZeroCountImmediatelyTriggered) {
  Latch latch(0);
  EXPECT_TRUE(latch.tryWait());
  EXPECT_EQ(latch.count(), 0U);
}

/** @test Latch with positive count is not triggered */
TEST_F(LatchTest, PositiveCountNotTriggered) {
  Latch latch(3);
  EXPECT_FALSE(latch.tryWait());
  EXPECT_EQ(latch.count(), 3U);
}

/** @test countDown decrements the counter */
TEST_F(LatchTest, CountDownDecrements) {
  Latch latch(3);
  latch.countDown();
  EXPECT_EQ(latch.count(), 2U);
}

/** @test countDown with value decrements by that amount */
TEST_F(LatchTest, CountDownWithValue) {
  Latch latch(5);
  latch.countDown(3);
  EXPECT_EQ(latch.count(), 2U);
}

/** @test countDown to zero triggers latch */
TEST_F(LatchTest, CountDownToZeroTriggers) {
  Latch latch(2);
  latch.countDown();
  EXPECT_FALSE(latch.tryWait());
  latch.countDown();
  EXPECT_TRUE(latch.tryWait());
}

/** @test countDown past zero clamps to zero */
TEST_F(LatchTest, CountDownPastZeroClamps) {
  Latch latch(2);
  latch.countDown(10);
  EXPECT_EQ(latch.count(), 0U);
  EXPECT_TRUE(latch.tryWait());
}

/** @test countDown on already-triggered latch is safe */
TEST_F(LatchTest, CountDownOnTriggeredIsSafe) {
  Latch latch(1);
  latch.countDown();
  EXPECT_TRUE(latch.tryWait());

  latch.countDown(); // Should be safe
  EXPECT_TRUE(latch.tryWait());
  EXPECT_EQ(latch.count(), 0U);
}

/* ----------------------------- Wait Tests ----------------------------- */

/** @test wait on already-triggered latch returns immediately */
TEST_F(LatchTest, WaitOnTriggeredReturnsImmediately) {
  Latch latch(0);

  const auto START = std::chrono::steady_clock::now();
  latch.wait();
  const auto ELAPSED = std::chrono::steady_clock::now() - START;

  EXPECT_LT(ELAPSED, std::chrono::milliseconds(50));
}

/** @test wait blocks until countDown */
TEST_F(LatchTest, WaitBlocksUntilCountDown) {
  Latch latch(1);
  std::atomic<bool> waiting{false};
  std::atomic<bool> done{false};

  std::thread waiter([&]() {
    waiting.store(true, std::memory_order_release);
    latch.wait();
    done.store(true, std::memory_order_release);
  });

  // Wait for waiter to start waiting
  while (!waiting.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(done.load(std::memory_order_acquire));

  // Trigger latch
  latch.countDown();
  waiter.join();

  EXPECT_TRUE(done.load(std::memory_order_acquire));
}

/** @test waitFor returns true on triggered latch */
TEST_F(LatchTest, WaitForSucceedsOnTriggered) {
  Latch latch(0);
  EXPECT_TRUE(latch.waitFor(std::chrono::seconds(1)));
}

/** @test waitFor times out on untriggered latch */
TEST_F(LatchTest, WaitForTimesOut) {
  Latch latch(1);

  const auto START = std::chrono::steady_clock::now();
  const bool RESULT = latch.waitFor(std::chrono::milliseconds(50));
  const auto ELAPSED = std::chrono::steady_clock::now() - START;

  EXPECT_FALSE(RESULT);
  EXPECT_GE(ELAPSED, std::chrono::milliseconds(40));
}

/** @test waitFor succeeds when triggered during wait */
TEST_F(LatchTest, WaitForSucceedsOnTrigger) {
  Latch latch(1);

  std::thread trigger([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    latch.countDown();
  });

  const auto START = std::chrono::steady_clock::now();
  const bool RESULT = latch.waitFor(std::chrono::seconds(1));
  const auto ELAPSED = std::chrono::steady_clock::now() - START;

  trigger.join();

  EXPECT_TRUE(RESULT);
  EXPECT_LT(ELAPSED, std::chrono::milliseconds(500));
}

/* ----------------------------- arriveAndWait Tests ----------------------------- */

/** @test arriveAndWait decrements and waits */
TEST_F(LatchTest, ArriveAndWaitDecrementsAndWaits) {
  Latch latch(2);
  std::atomic<int> completed{0};

  std::thread t1([&]() {
    latch.arriveAndWait();
    completed.fetch_add(1, std::memory_order_relaxed);
  });

  std::thread t2([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    latch.arriveAndWait();
    completed.fetch_add(1, std::memory_order_relaxed);
  });

  t1.join();
  t2.join();

  EXPECT_EQ(completed.load(), 2);
  EXPECT_TRUE(latch.tryWait());
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test Multiple threads count down */
TEST_F(LatchTest, MultipleThreadsCountDown) {
  constexpr int THREAD_COUNT = 10;
  Latch latch(THREAD_COUNT);

  std::vector<std::thread> threads;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&]() { latch.countDown(); });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_TRUE(latch.tryWait());
  EXPECT_EQ(latch.count(), 0U);
}

/** @test Multiple waiters are all released */
TEST_F(LatchTest, MultipleWaitersReleased) {
  Latch latch(1);
  constexpr int WAITER_COUNT = 5;
  std::atomic<int> released{0};

  std::vector<std::thread> waiters;
  for (int i = 0; i < WAITER_COUNT; ++i) {
    waiters.emplace_back([&]() {
      latch.wait();
      released.fetch_add(1, std::memory_order_relaxed);
    });
  }

  // Give waiters time to start waiting
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(released.load(), 0);

  // Trigger latch
  latch.countDown();

  for (auto& t : waiters) {
    t.join();
  }

  EXPECT_EQ(released.load(), WAITER_COUNT);
}

/** @test Worker coordination pattern */
TEST_F(LatchTest, WorkerCoordination) {
  constexpr int WORKER_COUNT = 4;
  Latch startLatch(1);
  Latch doneLatch(WORKER_COUNT);
  std::atomic<int> workDone{0};

  std::vector<std::thread> workers;
  for (int i = 0; i < WORKER_COUNT; ++i) {
    workers.emplace_back([&]() {
      startLatch.wait(); // Wait for start signal
      workDone.fetch_add(1, std::memory_order_relaxed);
      doneLatch.countDown(); // Signal done
    });
  }

  // Workers should be waiting
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(workDone.load(), 0);

  // Start workers
  startLatch.countDown();

  // Wait for all workers to finish
  doneLatch.wait();

  for (auto& t : workers) {
    t.join();
  }

  EXPECT_EQ(workDone.load(), WORKER_COUNT);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test tryWait returns consistent results on triggered latch */
TEST(LatchDeterminismTest, TryWaitConsistentOnTriggered) {
  Latch latch(0);
  const bool FIRST = latch.tryWait();
  const bool SECOND = latch.tryWait();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_TRUE(FIRST);
}

/** @test count() returns consistent results */
TEST(LatchDeterminismTest, CountConsistent) {
  Latch latch(5);
  const std::size_t FIRST = latch.count();
  const std::size_t SECOND = latch.count();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_EQ(FIRST, 5U);
}
