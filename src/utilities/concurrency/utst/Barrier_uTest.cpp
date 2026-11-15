/**
 * @file Barrier_uTest.cpp
 * @brief Unit tests for concurrency::Barrier.
 *
 * Notes:
 *  - Tests verify barrier synchronization and reusability.
 *  - The threaded cases hold the last arrival back, check that nobody has
 *    passed, and only then let it arrive, so a barrier that does not block
 *    fails them.
 */

#include "src/utilities/concurrency/inc/Barrier.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

using apex::concurrency::Barrier;
using apex::concurrency::test::escapesWithin;
using apex::concurrency::test::settles;

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

/** @test No arrival passes a barrier before the last one arrives, and every thread that passes
 * sees all of them arrived. Seven of eight threads arrive; none may pass while the eighth is
 * held back. */
TEST_F(BarrierTest, EveryArrivalWaitsForTheLast) {
  constexpr int THREAD_COUNT = 8;
  Barrier barrier(THREAD_COUNT);
  std::atomic<int> arrived{0};
  std::atomic<int> passed{0};
  std::array<std::atomic<int>, THREAD_COUNT> arrivedWhenPassing{};

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  const auto PARTICIPANT = [&](int index) {
    arrived.fetch_add(1, std::memory_order_acq_rel);
    barrier.arriveAndWait();
    arrivedWhenPassing[static_cast<std::size_t>(index)].store(
        arrived.load(std::memory_order_acquire), std::memory_order_release);
    passed.fetch_add(1, std::memory_order_acq_rel);
  };
  for (int i = 0; i < THREAD_COUNT - 1; ++i) {
    threads.emplace_back(PARTICIPANT, i);
  }

  ASSERT_TRUE(settles([&]() { return arrived.load() == THREAD_COUNT - 1; }));
  EXPECT_FALSE(escapesWithin([&]() { return passed.load() > 0; }))
      << passed.load() << " thread(s) passed with one arrival still missing";

  threads.emplace_back(PARTICIPANT, THREAD_COUNT - 1);
  for (auto& t : threads) {
    t.join();
  }

  for (const auto& seen : arrivedWhenPassing) {
    EXPECT_EQ(seen.load(), THREAD_COUNT);
  }
  EXPECT_EQ(barrier.generation(), 1U);
}

/** @test A barrier is reusable: in every phase no thread passes before the last one arrives,
 * and every thread that passes sees all of that phase's arrivals. The last thread is held back
 * in each phase until the others are seen not to have passed. */
TEST_F(BarrierTest, EachPhaseReleasesOnlyWhenAllHaveArrived) {
  constexpr int THREAD_COUNT = 4;
  constexpr int PHASE_COUNT = 3;
  Barrier barrier(THREAD_COUNT);
  std::array<std::atomic<int>, PHASE_COUNT> arrived{};
  std::array<std::atomic<int>, PHASE_COUNT> passed{};
  std::array<std::atomic<bool>, PHASE_COUNT> lastMayArrive{};
  std::array<std::array<std::atomic<int>, PHASE_COUNT>, THREAD_COUNT> arrivedWhenPassing{};

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&, i]() {
      for (std::size_t phase = 0; phase < PHASE_COUNT; ++phase) {
        if (i == THREAD_COUNT - 1) {
          while (!lastMayArrive[phase].load(std::memory_order_acquire)) {
            std::this_thread::yield();
          }
        }
        arrived[phase].fetch_add(1, std::memory_order_acq_rel);
        barrier.arriveAndWait();
        arrivedWhenPassing[static_cast<std::size_t>(i)][phase].store(
            arrived[phase].load(std::memory_order_acquire), std::memory_order_release);
        passed[phase].fetch_add(1, std::memory_order_acq_rel);
      }
    });
  }

  for (std::size_t phase = 0; phase < PHASE_COUNT; ++phase) {
    ASSERT_TRUE(settles([&]() { return arrived[phase].load() == THREAD_COUNT - 1; }))
        << "phase " << phase;
    EXPECT_FALSE(escapesWithin([&]() { return passed[phase].load() > 0; }))
        << passed[phase].load() << " thread(s) passed phase " << phase
        << " with one arrival still missing";
    lastMayArrive[phase].store(true, std::memory_order_release);
  }
  for (auto& t : threads) {
    t.join();
  }

  for (std::size_t thread = 0; thread < THREAD_COUNT; ++thread) {
    for (std::size_t phase = 0; phase < PHASE_COUNT; ++phase) {
      EXPECT_EQ(arrivedWhenPassing[thread][phase].load(), THREAD_COUNT)
          << "thread " << thread << ", phase " << phase;
    }
  }
  EXPECT_EQ(barrier.generation(), static_cast<std::size_t>(PHASE_COUNT));
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
  std::atomic<bool> arriving{false};
  std::atomic<bool> passed{false};
  std::thread arriver([&]() {
    arriving.store(true, std::memory_order_release);
    arriveFirst.arriveAndWait();
    passed.store(true, std::memory_order_release);
  });
  // The arriver may not pass a two-party barrier alone; the drop is the second arrival.
  ASSERT_TRUE(settles([&]() { return arriving.load(); }));
  EXPECT_FALSE(escapesWithin([&]() { return passed.load(); }))
      << "one arrival of two passed the barrier";
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
