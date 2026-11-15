/**
 * @file SpinLock_uTest.cpp
 * @brief Unit tests for concurrency::SpinLock.
 *
 * Notes:
 *  - Tests verify lock/unlock semantics and mutual exclusion.
 *  - Concurrent tests stress the lock with multiple threads.
 */

#include "src/utilities/concurrency/inc/SpinLock.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using apex::concurrency::SpinLock;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default SpinLock is unlocked */
TEST(SpinLockDefaultTest, DefaultIsUnlocked) {
  SpinLock lock;
  EXPECT_FALSE(lock.isLocked());
}

/* ----------------------------- Basic Operations Tests ----------------------------- */

class SpinLockTest : public ::testing::Test {};

/** @test lock() acquires the lock */
TEST_F(SpinLockTest, LockAcquires) {
  SpinLock lock;
  lock.lock();
  EXPECT_TRUE(lock.isLocked());
  lock.unlock();
}

/** @test unlock() releases the lock */
TEST_F(SpinLockTest, UnlockReleases) {
  SpinLock lock;
  lock.lock();
  lock.unlock();
  EXPECT_FALSE(lock.isLocked());
}

/** @test tryLock() succeeds on unlocked lock */
TEST_F(SpinLockTest, TryLockSucceedsOnUnlocked) {
  SpinLock lock;
  EXPECT_TRUE(lock.tryLock());
  EXPECT_TRUE(lock.isLocked());
  lock.unlock();
}

/** @test tryLock() fails on locked lock */
TEST_F(SpinLockTest, TryLockFailsOnLocked) {
  SpinLock lock;
  lock.lock();
  EXPECT_FALSE(lock.tryLock());
  lock.unlock();
}

/** @test Multiple lock/unlock cycles work */
TEST_F(SpinLockTest, MultipleLockUnlockCycles) {
  SpinLock lock;

  for (int i = 0; i < 100; ++i) {
    lock.lock();
    EXPECT_TRUE(lock.isLocked());
    lock.unlock();
    EXPECT_FALSE(lock.isLocked());
  }
}

/* ----------------------------- std::lock_guard Compatibility ----------------------------- */

/** @test SpinLock works with std::lock_guard */
TEST_F(SpinLockTest, WorksWithLockGuard) {
  SpinLock lock;

  {
    std::lock_guard<SpinLock> guard(lock);
    EXPECT_TRUE(lock.isLocked());
  }

  EXPECT_FALSE(lock.isLocked());
}

/** @test Nested lock_guard scopes work correctly */
TEST_F(SpinLockTest, NestedLockGuardScopes) {
  SpinLock lock1;
  SpinLock lock2;

  {
    std::lock_guard<SpinLock> g1(lock1);
    EXPECT_TRUE(lock1.isLocked());

    {
      std::lock_guard<SpinLock> g2(lock2);
      EXPECT_TRUE(lock2.isLocked());
    }

    EXPECT_FALSE(lock2.isLocked());
    EXPECT_TRUE(lock1.isLocked());
  }

  EXPECT_FALSE(lock1.isLocked());
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test SpinLock provides mutual exclusion: eight threads increment a plain counter under the
 * lock, so an increment lost to two holders shows in the total. */
TEST_F(SpinLockTest, MutualExclusion) {
  SpinLock lock;
  int counter = 0;
  constexpr int THREAD_COUNT = 8;
  constexpr int ITERATIONS = 10000;

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < ITERATIONS; ++j) {
        std::lock_guard<SpinLock> guard(lock);
        ++counter;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(counter, THREAD_COUNT * ITERATIONS);
}

/** @test SpinLock protects data structure integrity */
TEST_F(SpinLockTest, ProtectsDataIntegrity) {
  SpinLock lock;
  std::vector<int> data;
  constexpr int THREAD_COUNT = 4;
  constexpr int ITEMS_PER_THREAD = 1000;

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&, i]() {
      for (int j = 0; j < ITEMS_PER_THREAD; ++j) {
        std::lock_guard<SpinLock> guard(lock);
        data.push_back(i * ITEMS_PER_THREAD + j);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(data.size(), static_cast<std::size_t>(THREAD_COUNT * ITEMS_PER_THREAD));
}

/** @test tryLock() admits one holder at a time: each thread that gets the lock marks itself
 * inside and must find nobody already there. */
TEST_F(SpinLockTest, TryLockAdmitsOneHolderAtATime) {
  SpinLock lock;
  std::atomic<bool> inside{false};
  std::atomic<int> successCount{0};
  std::atomic<int> twoHolders{0};
  constexpr int THREAD_COUNT = 8;
  constexpr int ATTEMPTS = 20000;

  std::vector<std::thread> threads;
  threads.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < ATTEMPTS; ++j) {
        if (lock.tryLock()) {
          if (inside.exchange(true, std::memory_order_acq_rel)) {
            twoHolders.fetch_add(1, std::memory_order_relaxed);
          }
          successCount.fetch_add(1, std::memory_order_relaxed);
          inside.store(false, std::memory_order_release);
          lock.unlock();
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_GT(successCount.load(), 0);
  EXPECT_EQ(twoHolders.load(), 0) << "tryLock() succeeded while another thread held the lock";
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test isLocked() returns consistent results */
TEST(SpinLockDeterminismTest, IsLockedConsistent) {
  SpinLock lock;
  const bool FIRST = lock.isLocked();
  const bool SECOND = lock.isLocked();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_FALSE(FIRST);
}

/** @test tryLock() is deterministic on unlocked lock */
TEST(SpinLockDeterminismTest, TryLockDeterministic) {
  SpinLock lock;

  // First call should succeed
  EXPECT_TRUE(lock.tryLock());
  lock.unlock();

  // Second call should also succeed (lock is unlocked)
  EXPECT_TRUE(lock.tryLock());
  lock.unlock();
}

/* ----------------------------- Observer Tests ----------------------------- */

/** @test isLocked() is a read: a thread polling it never makes tryLock() fail on a lock nobody
 * holds. */
TEST_F(SpinLockTest, IsLockedDoesNotDisturbTryLock) {
  SpinLock lock;
  std::atomic<bool> running{false};
  std::atomic<bool> stop{false};
  std::thread prober([&]() {
    running.store(true, std::memory_order_release);
    while (!stop.load(std::memory_order_acquire)) {
      (void)lock.isLocked();
    }
  });
  while (!running.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  unsigned failures = 0;
  for (int i = 0; i < 100000; ++i) {
    if (lock.tryLock()) {
      lock.unlock();
    } else {
      ++failures;
    }
  }
  stop.store(true, std::memory_order_release);
  prober.join();
  EXPECT_EQ(failures, 0U);
}
