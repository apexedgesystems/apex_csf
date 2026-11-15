/**
 * @file Barrier_uTest.cpp
 * @brief Unit tests for concurrency::Barrier.
 *
 * Notes:
 *  - Tests verify barrier synchronization and reusability.
 *  - Concurrent tests use multiple threads to verify phase coordination.
 */

#include "src/utilities/concurrency/inc/Barrier.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using apex::concurrency::Barrier;

/* ----------------------------- Basic Operations Tests ----------------------------- */

class BarrierTest : public ::testing::Test {};

/** @test Barrier with expected 1 releases immediately */
TEST_F(BarrierTest, SingleThreadReleasesImmediately) {
  Barrier barrier(1);
  EXPECT_EQ(barrier.expected(), 1U);
  EXPECT_EQ(barrier.generation(), 0U);

  barrier.arriveAndWait();

  EXPECT_EQ(barrier.generation(), 1U);
}

/** @test expected() returns correct value */
TEST_F(BarrierTest, ExpectedReturnsValue) {
  Barrier barrier(5);
  EXPECT_EQ(barrier.expected(), 5U);
}

/** @test generation starts at 0 */
TEST_F(BarrierTest, GenerationStartsAtZero) {
  Barrier barrier(3);
  EXPECT_EQ(barrier.generation(), 0U);
}

/** @test arriveAndDrop decreases expected */
TEST_F(BarrierTest, ArriveAndDropDecreasesExpected) {
  Barrier barrier(3);
  barrier.arriveAndDrop();
  EXPECT_EQ(barrier.expected(), 2U);
}

/* ----------------------------- Timeout Tests ----------------------------- */

/** @test arriveAndWaitFor succeeds when barrier completes */
TEST_F(BarrierTest, ArriveAndWaitForSucceeds) {
  Barrier barrier(1);
  EXPECT_TRUE(barrier.arriveAndWaitFor(std::chrono::seconds(1)));
  EXPECT_EQ(barrier.generation(), 1U);
}

/** @test arriveAndWaitFor times out when waiting for others */
TEST_F(BarrierTest, ArriveAndWaitForTimesOut) {
  Barrier barrier(2);

  std::thread waiter([&]() {
    const bool RESULT = barrier.arriveAndWaitFor(std::chrono::milliseconds(50));
    EXPECT_FALSE(RESULT);
  });

  waiter.join();
  // The timed-out arrival is withdrawn: the barrier is back to needing two.
  EXPECT_EQ(barrier.generation(), 0U);
  EXPECT_FALSE(barrier.arriveAndWaitFor(std::chrono::milliseconds(10)));
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test Two threads synchronize at barrier */
TEST_F(BarrierTest, TwoThreadsSynchronize) {
  Barrier barrier(2);
  std::atomic<int> phase{0};

  std::thread t1([&]() {
    phase.fetch_add(1, std::memory_order_relaxed);
    barrier.arriveAndWait();
    phase.fetch_add(10, std::memory_order_relaxed);
  });

  std::thread t2([&]() {
    phase.fetch_add(1, std::memory_order_relaxed);
    barrier.arriveAndWait();
    phase.fetch_add(10, std::memory_order_relaxed);
  });

  t1.join();
  t2.join();

  // Both threads should have added 1, then both added 10
  EXPECT_EQ(phase.load(), 22);
  EXPECT_EQ(barrier.generation(), 1U);
}

/** @test Multiple threads synchronize */
TEST_F(BarrierTest, MultipleThreadsSynchronize) {
  constexpr int THREAD_COUNT = 8;
  Barrier barrier(THREAD_COUNT);
  std::atomic<int> arrivedCount{0};
  std::atomic<int> passedCount{0};

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&]() {
      arrivedCount.fetch_add(1, std::memory_order_relaxed);
      barrier.arriveAndWait();
      passedCount.fetch_add(1, std::memory_order_relaxed);
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(arrivedCount.load(), THREAD_COUNT);
  EXPECT_EQ(passedCount.load(), THREAD_COUNT);
  EXPECT_EQ(barrier.generation(), 1U);
}

/** @test Barrier is reusable across phases */
TEST_F(BarrierTest, ReusableAcrossPhases) {
  constexpr int THREAD_COUNT = 4;
  constexpr int PHASE_COUNT = 5;
  Barrier barrier(THREAD_COUNT);
  std::atomic<int> totalPhases{0};

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&]() {
      for (int p = 0; p < PHASE_COUNT; ++p) {
        barrier.arriveAndWait();
        totalPhases.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Each thread went through PHASE_COUNT phases
  EXPECT_EQ(totalPhases.load(), THREAD_COUNT * PHASE_COUNT);
  EXPECT_EQ(barrier.generation(), static_cast<std::size_t>(PHASE_COUNT));
}

/** @test arriveAndDrop is the last to trigger */
TEST_F(BarrierTest, ArriveAndDropTriggersBarrier) {
  // Test that arriveAndDrop can be the triggering arrival
  Barrier barrier(2);

  std::thread t1([&]() { barrier.arriveAndWait(); });

  // Give t1 time to start waiting; the result no longer depends on it (a
  // drop before or after t1's arrival completes the same phase).
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // arriveAndDrop should trigger the barrier and release t1
  barrier.arriveAndDrop();

  t1.join();

  EXPECT_EQ(barrier.expected(), 1U);
  EXPECT_EQ(barrier.generation(), 1U);
}

/** @test Phased computation pattern */
TEST_F(BarrierTest, PhasedComputation) {
  constexpr int THREAD_COUNT = 4;
  constexpr int ITERATIONS = 3;
  Barrier barrier(THREAD_COUNT);

  // Shared data that threads update
  std::vector<int> data(THREAD_COUNT, 0);
  std::mutex dataMutex;

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&, i]() {
      for (int iter = 0; iter < ITERATIONS; ++iter) {
        // Phase 1: Update local portion
        {
          std::lock_guard<std::mutex> lk(dataMutex);
          data[static_cast<std::size_t>(i)] += iter + 1;
        }

        // Sync: Wait for all threads to complete phase 1
        barrier.arriveAndWait();

        // Phase 2: Read others' data (after barrier ensures visibility)
        int sum = 0;
        {
          std::lock_guard<std::mutex> lk(dataMutex);
          for (int v : data) {
            sum += v;
          }
        }
        (void)sum; // Use sum to avoid unused warning

        // Sync before next iteration
        barrier.arriveAndWait();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Each thread added 1+2+3 = 6
  int total = 0;
  for (int v : data) {
    total += v;
  }
  EXPECT_EQ(total, THREAD_COUNT * (1 + 2 + 3));
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test expected() returns consistent results */
TEST(BarrierDeterminismTest, ExpectedConsistent) {
  Barrier barrier(5);
  const std::size_t FIRST = barrier.expected();
  const std::size_t SECOND = barrier.expected();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_EQ(FIRST, 5U);
}

/** @test generation() returns consistent results */
TEST(BarrierDeterminismTest, GenerationConsistent) {
  Barrier barrier(1);
  const std::size_t FIRST = barrier.generation();
  const std::size_t SECOND = barrier.generation();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_EQ(FIRST, 0U);
}

/* ----------------------------- Phase Semantics Tests ----------------------------- */

/** @test A drop is one arrival of the current phase, not two: with three participants, a drop
 * and one arrival leave the phase waiting for the third. */
TEST_F(BarrierTest, DropCountsOnceInCurrentPhase) {
  Barrier barrier(3);
  barrier.arriveAndDrop();
  EXPECT_FALSE(barrier.arriveAndWaitFor(std::chrono::milliseconds(50)))
      << "two arrivals of three completed the phase";
  EXPECT_EQ(barrier.generation(), 0U);
}

/** @test Drop-then-arrive and arrive-then-drop are the same phase: both end at generation 1 with
 * one participant left for the next phase. */
TEST_F(BarrierTest, DropFirstAndArriveFirstGiveSameGeneration) {
  Barrier dropFirst(2);
  dropFirst.arriveAndDrop();
  dropFirst.arriveAndWait();
  EXPECT_EQ(dropFirst.generation(), 1U);
  EXPECT_EQ(dropFirst.expected(), 1U);

  Barrier arriveFirst(2);
  std::thread arriver([&]() { arriveFirst.arriveAndWait(); });
  // The arriver blocks until the drop completes the phase; whichever order the
  // two arrivals take, the phase completes once with one participant left.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  arriveFirst.arriveAndDrop();
  arriver.join();
  EXPECT_EQ(arriveFirst.generation(), 1U);
  EXPECT_EQ(arriveFirst.expected(), 1U);
}

/** @test A timed-out arrival is withdrawn: one thread timing out twice on a two-party barrier
 * does not complete its phase alone. */
TEST_F(BarrierTest, TimedOutArrivalIsWithdrawn) {
  Barrier barrier(2);
  EXPECT_FALSE(barrier.arriveAndWaitFor(std::chrono::milliseconds(10)));
  EXPECT_FALSE(barrier.arriveAndWaitFor(std::chrono::milliseconds(10)))
      << "the first, timed-out arrival still counted";
  EXPECT_EQ(barrier.generation(), 0U);
}
