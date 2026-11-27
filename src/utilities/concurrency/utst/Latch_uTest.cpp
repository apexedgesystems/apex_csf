/**
 * @file Latch_uTest.cpp
 * @brief Unit tests for concurrency::Latch.
 *
 * Notes:
 *  - Tests verify countdown semantics and wait behavior.
 *  - Concurrent tests use multiple threads to verify synchronization.
 */

#include "src/utilities/concurrency/inc/Latch.hpp"
#include "src/utilities/concurrency/utst/support/Crew.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using apex::concurrency::Latch;
using apex::concurrency::test::Crew;
using apex::concurrency::test::CrewState;
using apex::concurrency::test::escapesWithin;
using apex::concurrency::test::settles;

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

/** @test wait() blocks while the count is above zero and returns once it reaches zero. */
TEST_F(LatchTest, WaitBlocksUntilCountDown) {
  struct Waiting : CrewState {
    Latch latch{1};
    std::atomic<bool> waiting{false};
    std::atomic<bool> done{false};
  };
  Crew<Waiting> crew;
  crew.start([](Waiting& state) {
    state.waiting.store(true, std::memory_order_release);
    state.latch.wait();
    state.done.store(true, std::memory_order_release);
  });

  ASSERT_TRUE(settles([&]() { return crew->waiting.load(); }));
  EXPECT_FALSE(escapesWithin([&]() { return crew->done.load(); }))
      << "wait() returned with the count at one";

  crew->latch.countDown();
  ASSERT_TRUE(crew.finish()) << "wait() did not return after the count reached zero";
  EXPECT_TRUE(crew->done.load());
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

/** @test arriveAndWait blocks an arrival until the count reaches zero: the first of two arrivals
 * does not return while the second is missing, and returns once it has arrived. */
TEST_F(LatchTest, ArriveAndWaitBlocksUntilTheLastArrival) {
  struct Arrivals : CrewState {
    Latch latch{2};
    std::atomic<bool> arriving{false};
    std::atomic<int> returned{0};
  };
  Crew<Arrivals> crew;
  crew.start([](Arrivals& state) {
    state.arriving.store(true, std::memory_order_release);
    state.latch.arriveAndWait();
    state.returned.fetch_add(1, std::memory_order_acq_rel);
  });

  ASSERT_TRUE(settles([&]() { return crew->arriving.load(); }));
  EXPECT_FALSE(escapesWithin([&]() { return crew->returned.load() > 0; }))
      << "the first of two arrivals returned with the count still at one";

  crew.start([](Arrivals& state) {
    state.latch.arriveAndWait();
    state.returned.fetch_add(1, std::memory_order_acq_rel);
  });
  ASSERT_TRUE(crew.finish()) << crew->returned.load() << " of 2 arrivals returned";

  EXPECT_EQ(crew->latch.count(), 0U);
  EXPECT_TRUE(crew->latch.tryWait());
}

/** @test arriveAndWait on a latch that is already triggered returns at once and leaves it
 * triggered. */
TEST_F(LatchTest, ArriveAndWaitOnTriggeredLatchReturns) {
  struct Triggered : CrewState {
    Latch latch{1};
  };
  Crew<Triggered> crew;
  crew->latch.countDown();
  ASSERT_EQ(crew->latch.count(), 0U);

  crew.start([](Triggered& state) { state.latch.arriveAndWait(); });
  ASSERT_TRUE(crew.finish()) << "arriveAndWait() on a triggered latch did not return";

  EXPECT_EQ(crew->latch.count(), 0U);
  EXPECT_TRUE(crew->latch.tryWait());
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test Ten threads counting down once each trigger a latch of ten. */
TEST_F(LatchTest, MultipleThreadsCountDown) {
  constexpr int THREAD_COUNT = 10;
  struct Counting : CrewState {
    Latch latch{THREAD_COUNT};
  };
  Crew<Counting> crew;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    crew.start([](Counting& state) { state.latch.countDown(); });
  }
  ASSERT_TRUE(crew.finish()) << "a countDown() did not return";

  EXPECT_TRUE(crew->latch.tryWait());
  EXPECT_EQ(crew->latch.count(), 0U);
}

/** @test One countDown() to zero releases every waiter: five threads in wait() stay there while
 * the count is one, and all five return after it. */
TEST_F(LatchTest, MultipleWaitersReleased) {
  constexpr int WAITER_COUNT = 5;
  struct Waiters : CrewState {
    Latch latch{1};
    std::atomic<int> entering{0};
    std::atomic<int> released{0};
  };
  Crew<Waiters> crew;
  for (int i = 0; i < WAITER_COUNT; ++i) {
    crew.start([](Waiters& state) {
      state.entering.fetch_add(1, std::memory_order_acq_rel);
      state.latch.wait();
      state.released.fetch_add(1, std::memory_order_acq_rel);
    });
  }

  ASSERT_TRUE(settles([&]() { return crew->entering.load() == WAITER_COUNT; }));
  EXPECT_FALSE(escapesWithin([&]() { return crew->released.load() > 0; }))
      << crew->released.load() << " waiter(s) returned with the count at one";

  crew->latch.countDown();
  ASSERT_TRUE(crew.finish()) << crew->released.load() << " of " << WAITER_COUNT
                             << " waiters returned";
  EXPECT_EQ(crew->released.load(), WAITER_COUNT);
}

/** @test A start latch and a done latch coordinate workers: four workers do nothing until the
 * start latch is triggered, then each works once and counts the done latch down, which a timed
 * wait then finds triggered. */
TEST_F(LatchTest, WorkerCoordination) {
  constexpr int WORKER_COUNT = 4;
  struct Coordinated : CrewState {
    Latch startLatch{1};
    Latch doneLatch{WORKER_COUNT};
    std::atomic<int> entering{0};
    std::atomic<int> workDone{0};
  };
  Crew<Coordinated> crew;
  for (int i = 0; i < WORKER_COUNT; ++i) {
    crew.start([](Coordinated& state) {
      state.entering.fetch_add(1, std::memory_order_acq_rel);
      state.startLatch.wait();
      state.workDone.fetch_add(1, std::memory_order_acq_rel);
      state.doneLatch.countDown();
    });
  }

  ASSERT_TRUE(settles([&]() { return crew->entering.load() == WORKER_COUNT; }));
  EXPECT_FALSE(escapesWithin([&]() { return crew->workDone.load() > 0; }))
      << "a worker ran before the start latch was triggered";

  crew->startLatch.countDown();
  EXPECT_TRUE(crew->doneLatch.waitFor(apex::concurrency::test::BOUND))
      << "the done latch was not triggered";
  ASSERT_TRUE(crew.finish()) << crew->workDone.load() << " of " << WORKER_COUNT
                             << " workers finished";
  EXPECT_EQ(crew->workDone.load(), WORKER_COUNT);
}

/* ----------------------------- Lifetime Tests ----------------------------- */

namespace {

/// A waiter that destroys a stack latch as soon as its wait returns, while a second thread does
/// the final countDown() through a slot: the shape of a progress poll on a completion latch.
/// Whether the final countDown() can still be touching the latch is what ThreadSanitizer decides;
/// the plain build only shows the loop completing.
template <class Wait> void destroyAfterWait(Wait wait) {
  std::atomic<Latch*> slot{nullptr};
  std::atomic<bool> quit{false};
  std::thread counter([&]() {
    while (!quit.load(std::memory_order_acquire)) {
      Latch* latch = slot.exchange(nullptr, std::memory_order_acq_rel);
      if (latch != nullptr) {
        latch->countDown();
      }
    }
  });
  for (int i = 0; i < 5000; ++i) {
    Latch latch(1);
    slot.store(&latch, std::memory_order_release);
    wait(latch);
  }
  quit.store(true, std::memory_order_release);
  counter.join();
}

/// Run destroyAfterWait() on a thread the case waits for with a bound: a wait that never returns
/// fails the case, and everything the loop touches is on that thread's own stack.
template <class Wait> bool destroyAfterWaitFinishes(Wait wait) {
  struct Nothing : CrewState {};
  Crew<Nothing> crew;
  crew.start([wait](Nothing&) { destroyAfterWait(wait); });
  return crew.finish(std::chrono::seconds(20));
}

} // namespace

/** @test A waiter polling tryWait() may destroy the latch once it reads zero: the final countDown()
 * is done with the latch by then. */
TEST(LatchLifetimeTest, WaiterDestroysAfterTryWait) {
  EXPECT_TRUE(destroyAfterWaitFinishes([](Latch& latch) {
    while (!latch.tryWait()) {
    }
  }));
}

/** @test The same with a zero-length waitFor(). */
TEST(LatchLifetimeTest, WaiterDestroysAfterWaitFor) {
  EXPECT_TRUE(destroyAfterWaitFinishes([](Latch& latch) {
    while (!latch.waitFor(std::chrono::microseconds(0))) {
    }
  }));
}

/** @test The control: wait() already takes the mutex before returning. */
TEST(LatchLifetimeTest, WaiterDestroysAfterWait) {
  EXPECT_TRUE(destroyAfterWaitFinishes([](Latch& latch) { latch.wait(); }));
}
