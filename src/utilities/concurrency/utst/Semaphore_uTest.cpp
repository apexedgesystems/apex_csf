/**
 * @file Semaphore_uTest.cpp
 * @brief Unit tests for concurrency::Semaphore.
 *
 * Notes:
 *  - Tests verify acquire/release semantics, blocking behavior, and timeouts.
 *  - Concurrent tests use multiple threads to verify synchronization.
 */

#include "src/utilities/concurrency/inc/Semaphore.hpp"
#include "src/utilities/concurrency/utst/support/Crew.hpp"
#include "src/utilities/concurrency/utst/support/ThreadProbe.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>

using apex::concurrency::Semaphore;
using apex::concurrency::test::Crew;
using apex::concurrency::test::CrewState;
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

/// Acquirers of one semaphore. A Crew holds this on the heap, so a case that gives up on the
/// acquirers detaches them with the state they hold.
struct Acquirers : CrewState {
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
void startAcquirers(Crew<Acquirers>& crew, int count) {
  for (int i = 0; i < count; ++i) {
    crew.start([](Acquirers& state) {
      state.entering.fetch_add(1, std::memory_order_acq_rel);
      state.sem.acquire();
      state.acquired.fetch_add(1, std::memory_order_acq_rel);
    });
  }
}

} // namespace

/** @test acquire() on an empty semaphore blocks, and one release() lets it return. The acquirer
 * is seen asleep inside the semaphore before the release. */
TEST_F(SemaphoreTest, AcquireBlocksUntilRelease) {
  Crew<Acquirers> crew;
  startAcquirers(crew, 1);
  EXPECT_TRUE(settles([&]() { return crew->asleep(1); }))
      << "the acquirer did not block on an empty semaphore";
  EXPECT_EQ(crew->acquired.load(), 0);

  crew->sem.release();
  ASSERT_TRUE(crew.finish()) << "a release did not wake the blocked acquirer";
  EXPECT_EQ(crew->acquired.load(), 1);
  EXPECT_EQ(crew->sem.count(), 0U);
}

/** @test release(n) wakes n blocked acquirers, not one: four threads asleep in acquire() all
 * return after a single release(4). */
TEST_F(SemaphoreTest, ReleaseOfSeveralWakesEveryBlockedAcquirer) {
  constexpr int ACQUIRERS = 4;
  Crew<Acquirers> crew;
  startAcquirers(crew, ACQUIRERS);
  EXPECT_TRUE(settles([&]() { return crew->asleep(ACQUIRERS); }))
      << "the acquirers did not all block";
  EXPECT_EQ(crew->acquired.load(), 0);

  crew->sem.release(ACQUIRERS);
  ASSERT_TRUE(crew.finish()) << crew->acquired.load() << " of " << ACQUIRERS
                             << " blocked acquirers returned after release(" << ACQUIRERS << ")";
  EXPECT_EQ(crew->acquired.load(), ACQUIRERS);
  EXPECT_EQ(crew->sem.count(), 0U);
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test Every release is matched by one acquire under contention: four threads acquire a
 * thousand times each while the case releases four thousand, and the count ends at zero. */
TEST_F(SemaphoreTest, ConcurrentAcquireRelease) {
  constexpr int THREAD_COUNT = 4;
  constexpr int ITERATIONS = 1000;
  struct Exchange : CrewState {
    Semaphore sem{0};
    std::atomic<int> totalAcquired{0};
  };
  Crew<Exchange> crew;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    crew.start([](Exchange& state) {
      for (int j = 0; j < ITERATIONS; ++j) {
        state.sem.acquire();
        state.totalAcquired.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  const int TOTAL_RELEASES = THREAD_COUNT * ITERATIONS;
  for (int i = 0; i < TOTAL_RELEASES; ++i) {
    crew->sem.release();
  }
  ASSERT_TRUE(crew.finish()) << crew->totalAcquired.load() << " of " << TOTAL_RELEASES
                             << " releases were acquired";

  EXPECT_EQ(crew->totalAcquired.load(), TOTAL_RELEASES);
  EXPECT_EQ(crew->sem.count(), 0U);
}

/** @test A semaphore of three limits holders to three: ten threads acquire and release a hundred
 * times each, and no more than three hold it at once. */
TEST_F(SemaphoreTest, ResourcePoolLimiting) {
  constexpr int POOL_SIZE = 3;
  constexpr int THREAD_COUNT = 10;
  struct Limited : CrewState {
    Semaphore pool{POOL_SIZE};
    std::atomic<int> currentlyHeld{0};
    std::atomic<int> maxHeld{0};
  };
  Crew<Limited> crew;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    crew.start([](Limited& state) {
      for (int j = 0; j < 100; ++j) {
        state.pool.acquire();

        const int HELD = state.currentlyHeld.fetch_add(1, std::memory_order_relaxed) + 1;
        int expected = state.maxHeld.load(std::memory_order_relaxed);
        while (HELD > expected &&
               !state.maxHeld.compare_exchange_weak(expected, HELD, std::memory_order_relaxed)) {
        }

        std::this_thread::yield();

        state.currentlyHeld.fetch_sub(1, std::memory_order_relaxed);
        state.pool.release();
      }
    });
  }
  ASSERT_TRUE(crew.finish()) << "the holders did not all finish";

  EXPECT_LE(crew->maxHeld.load(), POOL_SIZE);
  EXPECT_EQ(crew->pool.count(), static_cast<std::size_t>(POOL_SIZE));
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
