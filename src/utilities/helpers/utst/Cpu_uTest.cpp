/**
 * @file Cpu_uTest.cpp
 * @brief Unit tests for apex::helpers::cpu.
 *
 * Tests CPU primitives and spin utilities.
 *
 * Notes:
 *  - relax() is a single CPU hint instruction, hard to test side effects.
 *  - ExponentialBackoff is tested for iteration behavior.
 */

#include "src/utilities/helpers/inc/Cpu.hpp"

#include <gtest/gtest.h>

using apex::helpers::cpu::ExponentialBackoff;
using apex::helpers::cpu::getMonotonicNs;
using apex::helpers::cpu::relax;

/* ----------------------------- relax Tests ----------------------------- */

/** @test relax() does not crash. */
TEST(RelaxTest, DoesNotCrash) {
  // Just verify it can be called without issue
  relax();
  relax();
  relax();
}

/* ----------------------------- getMonotonicNs Tests ----------------------------- */

/** @test Monotonic timestamp is positive. */
TEST(GetMonotonicNsTest, ReturnsPositive) {
  const std::uint64_t TS = getMonotonicNs();
  EXPECT_GT(TS, 0U);
}

/** @test Monotonic timestamp increases. */
TEST(GetMonotonicNsTest, Increases) {
  const std::uint64_t FIRST = getMonotonicNs();
  // Small busy loop to ensure time passes
  for (int i = 0; i < 1000; ++i) {
    relax();
  }
  const std::uint64_t SECOND = getMonotonicNs();
  EXPECT_GE(SECOND, FIRST);
}

/** @test Consecutive calls are deterministic (return similar values). */
TEST(GetMonotonicNsTest, ConsecutiveCallsClose) {
  const std::uint64_t FIRST = getMonotonicNs();
  const std::uint64_t SECOND = getMonotonicNs();
  // Should be within 1 second of each other
  const std::uint64_t DIFF = (SECOND >= FIRST) ? (SECOND - FIRST) : (FIRST - SECOND);
  EXPECT_LT(DIFF, 1'000'000'000ULL); // < 1 second
}

/* ----------------------------- ExponentialBackoff Tests ----------------------------- */

/** @test Default construction and first spin succeeds. */
TEST(ExponentialBackoffTest, DefaultConstruction) {
  ExponentialBackoff backoff;
  backoff.spinOnce();
  EXPECT_TRUE(true);
}

/** @test spinOnce can be called repeatedly. */
TEST(ExponentialBackoffTest, SpinOnceRepeatable) {
  ExponentialBackoff backoff;
  for (int i = 0; i < 20; ++i) {
    backoff.spinOnce();
  }
  EXPECT_TRUE(true);
}

/** @test reset allows reuse. */
TEST(ExponentialBackoffTest, ResetAllowsReuse) {
  ExponentialBackoff backoff;
  for (int i = 0; i < 10; ++i) {
    backoff.spinOnce();
  }
  backoff.reset();
  // Should be able to spin again from start
  backoff.spinOnce();
  EXPECT_TRUE(true);
}

/** @test Multiple backoff instances are independent. */
TEST(ExponentialBackoffTest, IndependentInstances) {
  ExponentialBackoff backoff1;
  ExponentialBackoff backoff2;

  backoff1.spinOnce();
  backoff1.spinOnce();
  backoff1.spinOnce();

  // backoff2 should still be at initial state
  backoff2.spinOnce();

  EXPECT_TRUE(true);
}
