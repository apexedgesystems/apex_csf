/**
 * @file Barrier_uTest.cpp
 * @brief Unit tests for concurrency::Barrier.
 *
 * Notes:
 *  - Tests verify barrier synchronization and reusability.
 *  - The threaded cases hold the last arrival back, check that nobody has
 *    passed, and only then let it arrive, so a barrier that does not block
 *    fails them.
 *  - Those cases run every arrival on a Crew thread and wait a bounded time
 *    for it, so a barrier that never releases fails them too.
 */

#include "src/utilities/concurrency/inc/Barrier.hpp"
#include "src/utilities/concurrency/utst/support/Crew.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

using apex::concurrency::Barrier;
using apex::concurrency::test::Crew;
using apex::concurrency::test::CrewState;
using apex::concurrency::test::escapesWithin;
using apex::concurrency::test::settles;

/* ----------------------------- Basic Operations Tests ----------------------------- */

class BarrierTest : public ::testing::Test {};

/** @test Barrier with expected 1 releases immediately */
TEST_F(BarrierTest, SingleThreadReleasesImmediately) {
  struct Alone : CrewState {
    Barrier barrier{1};
  };
  Crew<Alone> crew;
  EXPECT_EQ(crew->barrier.expected(), 1U);
  EXPECT_EQ(crew->barrier.generation(), 0U);

  crew.start([](Alone& state) { state.barrier.arriveAndWait(); });
  ASSERT_TRUE(crew.finish()) << "the only participant did not pass";

  EXPECT_EQ(crew->barrier.generation(), 1U);
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
  struct Arrivals : CrewState {
    Barrier barrier{THREAD_COUNT};
    std::atomic<int> arrived{0};
    std::atomic<int> passed{0};
    std::array<std::atomic<int>, THREAD_COUNT> arrivedWhenPassing{};
  };
  Crew<Arrivals> crew;
  const auto PARTICIPANT = [](int index) {
    return [index](Arrivals& state) {
      state.arrived.fetch_add(1, std::memory_order_acq_rel);
      state.barrier.arriveAndWait();
      state.arrivedWhenPassing[static_cast<std::size_t>(index)].store(
          state.arrived.load(std::memory_order_acquire), std::memory_order_release);
      state.passed.fetch_add(1, std::memory_order_acq_rel);
    };
  };
  for (int i = 0; i < THREAD_COUNT - 1; ++i) {
    crew.start(PARTICIPANT(i));
  }

  ASSERT_TRUE(settles([&]() { return crew->arrived.load() == THREAD_COUNT - 1; }));
  EXPECT_FALSE(escapesWithin([&]() { return crew->passed.load() > 0; }))
      << crew->passed.load() << " thread(s) passed with one arrival still missing";

  crew.start(PARTICIPANT(THREAD_COUNT - 1));
  ASSERT_TRUE(crew.finish()) << crew->passed.load() << " of " << THREAD_COUNT
                             << " threads passed after the last arrival";

  for (const auto& seen : crew->arrivedWhenPassing) {
    EXPECT_EQ(seen.load(), THREAD_COUNT);
  }
  EXPECT_EQ(crew->barrier.generation(), 1U);
}

/** @test A barrier is reusable: in every phase no thread passes before the last one arrives,
 * and every thread that passes sees all of that phase's arrivals. The last thread is held back
 * in each phase until the others are seen not to have passed. */
TEST_F(BarrierTest, EachPhaseReleasesOnlyWhenAllHaveArrived) {
  constexpr int THREAD_COUNT = 4;
  constexpr int PHASE_COUNT = 3;
  struct Phases : CrewState {
    Barrier barrier{THREAD_COUNT};
    std::array<std::atomic<int>, PHASE_COUNT> arrived{};
    std::array<std::atomic<int>, PHASE_COUNT> passed{};
    std::array<std::atomic<bool>, PHASE_COUNT> lastMayArrive{};
    std::array<std::array<std::atomic<int>, PHASE_COUNT>, THREAD_COUNT> arrivedWhenPassing{};
  };
  Crew<Phases> crew;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    crew.start([i](Phases& state) {
      for (std::size_t phase = 0; phase < PHASE_COUNT; ++phase) {
        if (i == THREAD_COUNT - 1) {
          while (!state.lastMayArrive[phase].load(std::memory_order_acquire)) {
            if (state.abandoned.load(std::memory_order_acquire)) {
              return;
            }
            std::this_thread::yield();
          }
        }
        state.arrived[phase].fetch_add(1, std::memory_order_acq_rel);
        state.barrier.arriveAndWait();
        state.arrivedWhenPassing[static_cast<std::size_t>(i)][phase].store(
            state.arrived[phase].load(std::memory_order_acquire), std::memory_order_release);
        state.passed[phase].fetch_add(1, std::memory_order_acq_rel);
      }
    });
  }

  for (std::size_t phase = 0; phase < PHASE_COUNT; ++phase) {
    ASSERT_TRUE(settles([&]() { return crew->arrived[phase].load() == THREAD_COUNT - 1; }))
        << "phase " << phase;
    EXPECT_FALSE(escapesWithin([&]() { return crew->passed[phase].load() > 0; }))
        << crew->passed[phase].load() << " thread(s) passed phase " << phase
        << " with one arrival still missing";
    crew->lastMayArrive[phase].store(true, std::memory_order_release);
  }
  ASSERT_TRUE(crew.finish()) << "not every thread passed the last phase";

  for (std::size_t thread = 0; thread < THREAD_COUNT; ++thread) {
    for (std::size_t phase = 0; phase < PHASE_COUNT; ++phase) {
      EXPECT_EQ(crew->arrivedWhenPassing[thread][phase].load(), THREAD_COUNT)
          << "thread " << thread << ", phase " << phase;
    }
  }
  EXPECT_EQ(crew->barrier.generation(), static_cast<std::size_t>(PHASE_COUNT));
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
 * one participant left for the next phase. Each arrival runs on a thread of its own, so an
 * arrival that never returns fails the case. */
TEST_F(BarrierTest, DropFirstAndArriveFirstGiveSameGeneration) {
  struct Pair : CrewState {
    Barrier barrier{2};
    std::atomic<bool> arriving{false};
    std::atomic<bool> passed{false};
  };
  const auto ARRIVER = [](Pair& state) {
    state.arriving.store(true, std::memory_order_release);
    state.barrier.arriveAndWait();
    state.passed.store(true, std::memory_order_release);
  };

  Crew<Pair> dropFirst;
  dropFirst->barrier.arriveAndDrop();
  dropFirst.start(ARRIVER);
  ASSERT_TRUE(dropFirst.finish()) << "the arrival after a drop did not pass";
  EXPECT_EQ(dropFirst->barrier.generation(), 1U);
  EXPECT_EQ(dropFirst->barrier.expected(), 1U);

  Crew<Pair> arriveFirst;
  arriveFirst.start(ARRIVER);
  // The arriver may not pass a two-party barrier alone; the drop is the second arrival.
  ASSERT_TRUE(settles([&]() { return arriveFirst->arriving.load(); }));
  EXPECT_FALSE(escapesWithin([&]() { return arriveFirst->passed.load(); }))
      << "one arrival of two passed the barrier";
  arriveFirst->barrier.arriveAndDrop();
  ASSERT_TRUE(arriveFirst.finish()) << "the drop did not release the waiting arrival";
  EXPECT_EQ(arriveFirst->barrier.generation(), 1U);
  EXPECT_EQ(arriveFirst->barrier.expected(), 1U);
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
