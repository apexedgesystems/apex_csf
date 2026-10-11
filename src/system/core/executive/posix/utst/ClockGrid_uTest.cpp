/**
 * @file ClockGrid_uTest.cpp
 * @brief Unit tests for the exact tick-deadline grid.
 */

#include "src/system/core/executive/posix/inc/ClockGrid.hpp"

#include <cstdint>

#include <gtest/gtest.h>

using executive::clockgrid::deadlineNs;
using executive::clockgrid::NS_PER_S;
using executive::clockgrid::periodNs;
using executive::clockgrid::ticksElapsed;

namespace {

constexpr std::uint64_t ANCHOR = 123'456'789'012ULL;

/// Every rate the validator admits, with the awkward ones called out:
/// primes, non-divisors of 1000, both ends of the range.
constexpr std::uint32_t RATES[] = {1, 2, 3, 7, 30, 60, 100, 128, 300, 333, 600, 799, 999, 1000};

} // namespace

/* ----------------------------- Second boundaries ----------------------------- */

/** @test Every whole second is exact for every rate: no fraction accumulates. */
TEST(ClockGrid, SecondBoundariesAreExact) {
  for (const std::uint32_t HZ : RATES) {
    for (std::uint64_t s = 0; s <= 100'000; s += 997) {
      EXPECT_EQ(deadlineNs(ANCHOR, s * HZ, HZ), ANCHOR + s * NS_PER_S) << "hz=" << HZ << " s=" << s;
    }
  }
}

/** @test A rate that divides 1000 lands on exact millisecond multiples. */
TEST(ClockGrid, DivisorRatesLandOnWholeMilliseconds) {
  for (std::uint64_t n = 0; n < 3'000; ++n) {
    EXPECT_EQ(deadlineNs(ANCHOR, n, 100), ANCHOR + n * 10'000'000ULL);
    EXPECT_EQ(deadlineNs(ANCHOR, n, 1000), ANCHOR + n * 1'000'000ULL);
    EXPECT_EQ(deadlineNs(ANCHOR, n, 1), ANCHOR + n * NS_PER_S);
  }
}

/* ----------------------------- Inside a second ----------------------------- */

/** @test Within a second the deadline is the ideal instant truncated to whole nanoseconds. */
TEST(ClockGrid, SubSecondShareIsTruncatedIdeal) {
  for (const std::uint32_t HZ : RATES) {
    for (std::uint64_t n = 0; n < 2ULL * HZ; ++n) {
      const long double IDEAL =
          static_cast<long double>(ANCHOR) +
          static_cast<long double>(n) * static_cast<long double>(NS_PER_S) / HZ;
      const std::uint64_t D = deadlineNs(ANCHOR, n, HZ);
      EXPECT_LE(static_cast<long double>(D), IDEAL + 0.5L) << "hz=" << HZ << " n=" << n;
      EXPECT_GT(static_cast<long double>(D), IDEAL - 1.0L) << "hz=" << HZ << " n=" << n;
    }
  }
}

/** @test Deadlines are strictly increasing and spaced by the period or one nanosecond more. */
TEST(ClockGrid, SpacingIsPeriodOrPeriodPlusOne) {
  for (const std::uint32_t HZ : RATES) {
    const std::uint64_t P = periodNs(HZ);
    std::uint64_t prev = deadlineNs(ANCHOR, 0, HZ);
    for (std::uint64_t n = 1; n < 3ULL * HZ + 5; ++n) {
      const std::uint64_t D = deadlineNs(ANCHOR, n, HZ);
      const std::uint64_t GAP = D - prev;
      EXPECT_TRUE(GAP == P || GAP == P + 1) << "hz=" << HZ << " n=" << n << " gap=" << GAP;
      prev = D;
    }
  }
}

/** @test The 600 Hz case from the ticket: 1,666,666 or 1,666,667 ns apart, exact at one second. */
TEST(ClockGrid, SixHundredHertzIsHonest) {
  EXPECT_EQ(periodNs(600), 1'666'666U);
  EXPECT_EQ(deadlineNs(0, 1, 600), 1'666'666U);
  EXPECT_EQ(deadlineNs(0, 3, 600), 5'000'000U);
  EXPECT_EQ(deadlineNs(0, 600, 600), NS_PER_S);
  EXPECT_EQ(deadlineNs(0, 36'000, 600), 60 * NS_PER_S);
}

/* ----------------------------- Inverse ----------------------------- */

/** @test ticksElapsed is the exact inverse of deadlineNs at every deadline and just before it. */
TEST(ClockGrid, TicksElapsedInvertsDeadline) {
  for (const std::uint32_t HZ : RATES) {
    for (std::uint64_t n = 0; n < 3ULL * HZ + 7; ++n) {
      const std::uint64_t D = deadlineNs(ANCHOR, n, HZ);
      EXPECT_EQ(ticksElapsed(ANCHOR, D, HZ), n + 1) << "hz=" << HZ << " n=" << n;
      if (n > 0) {
        EXPECT_EQ(ticksElapsed(ANCHOR, D - 1, HZ), n) << "hz=" << HZ << " n=" << n;
      }
    }
  }
}

/** @test Before the anchor nothing has elapsed; at the anchor tick 0 has. */
TEST(ClockGrid, TicksElapsedEdges) {
  EXPECT_EQ(ticksElapsed(ANCHOR, ANCHOR - 1, 100), 0U);
  EXPECT_EQ(ticksElapsed(ANCHOR, ANCHOR, 100), 1U);
  EXPECT_EQ(ticksElapsed(ANCHOR, ANCHOR + NS_PER_S - 1, 100), 100U);
  EXPECT_EQ(ticksElapsed(ANCHOR, ANCHOR + NS_PER_S, 100), 101U);
}

/** @test A stall measured in ticks: the resync arithmetic for a whole-second gap. */
TEST(ClockGrid, StallInTicks) {
  const std::uint64_t ISSUED = 250; // ticks 0..249 already issued
  const std::uint64_t NOW = deadlineNs(ANCHOR, ISSUED, 100) + 3 * NS_PER_S + 15'000'000;
  const std::uint64_t PASSED = ticksElapsed(ANCHOR, NOW, 100);
  EXPECT_EQ(PASSED - ISSUED, 302U); // 3.015 s of 10 ms ticks, counting the one due now
}
