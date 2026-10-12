/**
 * @file TickLateness_uTest.cpp
 * @brief Unit tests for the clock-tick lateness record.
 */

#include "src/system/core/executive/posix/inc/TickLateness.hpp"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

using executive::TickLateness;

/* ----------------------------- Empty ----------------------------- */

/** @test Nothing recorded reads as zero everywhere. */
TEST(TickLateness, EmptyReadsZero) {
  const TickLateness L{};
  EXPECT_EQ(L.count, 0U);
  EXPECT_EQ(L.percentileNs(0.5), 0);
  EXPECT_EQ(L.percentileNs(0.99), 0);
  EXPECT_DOUBLE_EQ(L.meanNs(), 0.0);
  EXPECT_EQ(L.maxNs, 0);
  EXPECT_EQ(L.minNs, std::numeric_limits<std::int64_t>::max());
}

/* ----------------------------- Bucketing ----------------------------- */

/** @test A wake lands in the bucket of its whole microsecond. */
TEST(TickLateness, BucketIsWholeMicrosecond) {
  TickLateness l{};
  l.record(0);
  l.record(999);
  l.record(1'000);
  l.record(1'999);
  l.record(2'000);
  EXPECT_EQ(l.buckets[0], 2U);
  EXPECT_EQ(l.buckets[1], 2U);
  EXPECT_EQ(l.buckets[2], 1U);
  EXPECT_EQ(l.count, 5U);
  EXPECT_EQ(l.overflow, 0U);
  EXPECT_EQ(l.minNs, 0);
  EXPECT_EQ(l.maxNs, 2'000);
}

/** @test A millisecond or more goes to the overflow bucket and still sets the maximum. */
TEST(TickLateness, MillisecondOrMoreOverflows) {
  TickLateness l{};
  l.record(999'999);
  l.record(1'000'000);
  l.record(5'000'000);
  EXPECT_EQ(l.buckets[TickLateness::NUM_BUCKETS - 1], 1U);
  EXPECT_EQ(l.overflow, 2U);
  EXPECT_EQ(l.count, 3U);
  EXPECT_EQ(l.maxNs, 5'000'000);
  EXPECT_EQ(l.sumNs, 6'999'999U);
}

/** @test An early wake counts in bucket 0 and in `early`, and does not pollute the sum. */
TEST(TickLateness, EarlyWakeCountedSeparately) {
  TickLateness l{};
  l.record(-250);
  l.record(4'000);
  EXPECT_EQ(l.early, 1U);
  EXPECT_EQ(l.buckets[0], 1U);
  EXPECT_EQ(l.count, 2U);
  EXPECT_EQ(l.minNs, -250);
  EXPECT_EQ(l.sumNs, 4'000U);
  EXPECT_DOUBLE_EQ(l.meanNs(), 4'000.0);
}

/* ----------------------------- Percentiles ----------------------------- */

/** @test Percentiles return the upper edge of the bucket holding the ranked sample. */
TEST(TickLateness, PercentileIsBucketUpperEdge) {
  TickLateness l{};
  for (std::int64_t us = 0; us < 100; ++us) {
    l.record(us * 1'000); // one sample in each of buckets 0..99
  }
  EXPECT_EQ(l.percentileNs(0.50), 50'000);  // rank 50 sits in bucket 49
  EXPECT_EQ(l.percentileNs(0.99), 99'000);  // rank 99 sits in bucket 98
  EXPECT_EQ(l.percentileNs(1.00), 100'000); // rank 100 sits in bucket 99
  EXPECT_EQ(l.percentileNs(0.00), 1'000);   // rank clamps to 1
}

/** @test A percentile that lands in the overflow bucket reports the exact maximum. */
TEST(TickLateness, PercentileInOverflowReportsMax) {
  TickLateness l{};
  l.record(10'000);
  l.record(20'000);
  l.record(3'500'000);
  EXPECT_EQ(l.percentileNs(0.5), 21'000); // rank 2 sits in bucket 20: upper edge 21 us
  EXPECT_EQ(l.percentileNs(1.0), 3'500'000);
}

/** @test Percentile arguments outside [0, 1] clamp. */
TEST(TickLateness, PercentileClampsArgument) {
  TickLateness l{};
  l.record(7'000);
  EXPECT_EQ(l.percentileNs(-1.0), 8'000);
  EXPECT_EQ(l.percentileNs(2.0), 8'000);
}

/* ----------------------------- Mean and reset ----------------------------- */

/** @test The mean averages the non-negative values only. */
TEST(TickLateness, MeanOfLateValues) {
  TickLateness l{};
  l.record(1'000);
  l.record(3'000);
  l.record(-500);
  EXPECT_DOUBLE_EQ(l.meanNs(), 2'000.0);
}

/** @test Reset returns the record to the empty state. */
TEST(TickLateness, ResetClearsEverything) {
  TickLateness l{};
  l.record(12'345);
  l.record(2'000'000);
  l.record(-1);
  l.reset();
  EXPECT_EQ(l.count, 0U);
  EXPECT_EQ(l.overflow, 0U);
  EXPECT_EQ(l.early, 0U);
  EXPECT_EQ(l.sumNs, 0U);
  EXPECT_EQ(l.maxNs, 0);
  EXPECT_EQ(l.minNs, std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(l.buckets[12], 0U);
  EXPECT_EQ(l.percentileNs(0.5), 0);
}
