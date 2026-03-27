/**
 * @file compat_concurrency_uTest.cpp
 * @brief Unit tests for compat_concurrency.hpp (concurrency shims).
 */

#include <atomic>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>

#include "src/utilities/compatibility/inc/compat_concurrency.hpp"

using apex::compat::SrcLoc;
using apex::compat::atom::notifyAll;
using apex::compat::atom::notifyOne;
using apex::compat::atom::waitEq;

// =============================================================================
// SrcLoc Tests
// =============================================================================

/**
 * @test SrcLoc_DefaultConstruction
 * @brief Verifies default SrcLoc has null/zero fields.
 */
TEST(CompatConcurrency, SrcLoc_DefaultConstruction) {
  const SrcLoc loc;

  EXPECT_EQ(loc.file, nullptr);
  EXPECT_EQ(loc.function, nullptr);
  EXPECT_EQ(loc.line, 0u);
  EXPECT_EQ(loc.column, 0u);
}

/**
 * @test SrcLoc_Current
 * @brief Verifies SrcLoc::current() captures location info.
 *
 * On C++20+, this captures actual file/function/line.
 * On C++17, this returns default (null) values.
 */
TEST(CompatConcurrency, SrcLoc_Current) {
  const SrcLoc loc = SrcLoc::current();

// C++20+ should capture real values
#if __cpp_lib_source_location >= 201907L
  EXPECT_NE(loc.file, nullptr);
  EXPECT_NE(loc.function, nullptr);
  EXPECT_GT(loc.line, 0u);

  // File should contain this test file name
  EXPECT_NE(std::strstr(loc.file, "compat_concurrency_uTest.cpp"), nullptr);
#else
  // C++17 returns empty SrcLoc
  EXPECT_EQ(loc.file, nullptr);
  EXPECT_EQ(loc.function, nullptr);
  EXPECT_EQ(loc.line, 0u);
#endif
}

/**
 * @test SrcLoc_Constexpr
 * @brief Verifies SrcLoc::current() is constexpr.
 */
TEST(CompatConcurrency, SrcLoc_Constexpr) {
  // Should compile - current() is constexpr
  constexpr SrcLoc loc = SrcLoc::current();
  (void)loc;
  SUCCEED();
}

// =============================================================================
// Atomic Wait/Notify Tests
// =============================================================================

/**
 * @test AtomicWait_ImmediateReturn
 * @brief Verifies waitEq returns immediately when value differs from expected.
 */
TEST(CompatConcurrency, AtomicWait_ImmediateReturn) {
  std::atomic<int> flag{42};

  // Wait for value != 42 should return immediately since flag is already 42
  // Actually, waitEq waits WHILE value == expected, so if value != expected, it returns
  waitEq(flag, 0); // flag is 42, expected is 0, so returns immediately

  EXPECT_EQ(flag.load(), 42);
}

/**
 * @test AtomicWait_SignalledByNotify
 * @brief Verifies waitEq wakes when value changes and notifyOne is called.
 */
TEST(CompatConcurrency, AtomicWait_SignalledByNotify) {
  std::atomic<int> flag{0};
  std::atomic<bool> threadStarted{false};
  std::atomic<bool> threadFinished{false};

  std::thread waiter([&]() {
    threadStarted.store(true);
    waitEq(flag, 0); // Wait while flag == 0
    threadFinished.store(true);
  });

  // Wait for thread to start
  while (!threadStarted.load()) {
    std::this_thread::yield();
  }

  // Small delay to ensure waiter is in wait state
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Change value and notify
  flag.store(1);
  notifyOne(flag);

  waiter.join();

  EXPECT_TRUE(threadFinished.load());
}

/**
 * @test AtomicWait_NotifyAll
 * @brief Verifies notifyAll wakes multiple waiters.
 */
TEST(CompatConcurrency, AtomicWait_NotifyAll) {
  std::atomic<int> flag{0};
  std::atomic<int> waitersReady{0};
  std::atomic<int> waitersFinished{0};

  constexpr int numWaiters = 3;

  std::vector<std::thread> waiters;
  for (int i = 0; i < numWaiters; ++i) {
    waiters.emplace_back([&]() {
      waitersReady.fetch_add(1);
      waitEq(flag, 0); // Wait while flag == 0
      waitersFinished.fetch_add(1);
    });
  }

  // Wait for all waiters to be ready
  while (waitersReady.load() < numWaiters) {
    std::this_thread::yield();
  }

  // Small delay to ensure waiters are in wait state
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Change value and notify all
  flag.store(1);
  notifyAll(flag);

  for (auto& t : waiters) {
    t.join();
  }

  EXPECT_EQ(waitersFinished.load(), numWaiters);
}

/**
 * @test AtomicWait_DifferentTypes
 * @brief Verifies waitEq works with different integral types.
 */
TEST(CompatConcurrency, AtomicWait_DifferentTypes) {
  // uint8_t
  {
    std::atomic<std::uint8_t> a{1};
    waitEq(a, std::uint8_t{0}); // Should return immediately (1 != 0)
    EXPECT_EQ(a.load(), 1);
  }

  // int16_t
  {
    std::atomic<std::int16_t> a{-100};
    waitEq(a, std::int16_t{0}); // Should return immediately
    EXPECT_EQ(a.load(), -100);
  }

  // uint32_t
  {
    std::atomic<std::uint32_t> a{0xDEADBEEF};
    waitEq(a, std::uint32_t{0}); // Should return immediately
    EXPECT_EQ(a.load(), 0xDEADBEEF);
  }
}

/**
 * @test AtomicNotify_NoWaiters
 * @brief Verifies notifyOne/notifyAll don't crash when no waiters exist.
 */
TEST(CompatConcurrency, AtomicNotify_NoWaiters) {
  std::atomic<int> flag{42};

  // Should not crash or hang
  notifyOne(flag);
  notifyAll(flag);

  EXPECT_EQ(flag.load(), 42);
}
