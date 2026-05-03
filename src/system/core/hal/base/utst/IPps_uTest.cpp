/**
 * @file IPps_uTest.cpp
 * @brief Unit tests for IPps interface and related types.
 */

#include "src/system/core/hal/base/IPps.hpp"

#include <gtest/gtest.h>

using apex::hal::PpsConfig;
using apex::hal::PpsEdge;
using apex::hal::PpsStats;
using apex::hal::PpsStatus;
using apex::hal::toString;

/* ----------------------------- PpsStatus Tests ----------------------------- */

/** @test Verify PpsStatus::OK is zero. */
TEST(PpsStatus, OkIsZero) { EXPECT_EQ(static_cast<int>(PpsStatus::OK), 0); }

/** @test Verify toString returns non-null for all status values. */
TEST(PpsStatus, ToStringNonNull) {
  EXPECT_NE(toString(PpsStatus::OK), nullptr);
  EXPECT_NE(toString(PpsStatus::NO_NEW_EDGE), nullptr);
  EXPECT_NE(toString(PpsStatus::ERROR_NOT_INIT), nullptr);
  EXPECT_NE(toString(PpsStatus::ERROR_DEVICE), nullptr);
  EXPECT_NE(toString(PpsStatus::ERROR_INVALID_ARG), nullptr);
}

/** @test Verify toString handles unknown status values. */
TEST(PpsStatus, ToStringUnknown) {
  const auto UNKNOWN = static_cast<PpsStatus>(255);
  EXPECT_STREQ(toString(UNKNOWN), "UNKNOWN");
}

/* ----------------------------- PpsEdge Tests ----------------------------- */

/** @test Verify PpsEdge enum values. */
TEST(PpsEdge, EnumValues) {
  EXPECT_EQ(static_cast<int>(PpsEdge::RISING), 0);
  EXPECT_EQ(static_cast<int>(PpsEdge::FALLING), 1);
}

/* ----------------------------- PpsConfig Tests ----------------------------- */

/** @test Verify PpsConfig default values. */
TEST(PpsConfig, DefaultValues) {
  const PpsConfig CONFIG;

  EXPECT_EQ(CONFIG.edge, PpsEdge::RISING);
}

/** @test Verify PpsConfig is copyable. */
TEST(PpsConfig, Copyable) {
  PpsConfig config1;
  config1.edge = PpsEdge::FALLING;

  const PpsConfig CONFIG2 = config1;

  EXPECT_EQ(CONFIG2.edge, PpsEdge::FALLING);
}

/* ----------------------------- PpsStats Tests ----------------------------- */

/** @test Verify PpsStats default values are zero. */
TEST(PpsStats, DefaultZero) {
  const PpsStats STATS;

  EXPECT_EQ(STATS.captureCount, 0U);
  EXPECT_EQ(STATS.errorCount, 0U);
}

/** @test Verify PpsStats reset clears all counters. */
TEST(PpsStats, Reset) {
  PpsStats stats;
  stats.captureCount = 42;
  stats.errorCount = 7;

  stats.reset();

  EXPECT_EQ(stats.captureCount, 0U);
  EXPECT_EQ(stats.errorCount, 0U);
}
