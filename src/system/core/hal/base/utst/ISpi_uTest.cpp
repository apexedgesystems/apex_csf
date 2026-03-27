/**
 * @file ISpi_uTest.cpp
 * @brief Unit tests for ISpi interface and related types.
 */

#include "src/system/core/hal/base/ISpi.hpp"

#include <gtest/gtest.h>

using apex::hal::SpiBitOrder;
using apex::hal::SpiConfig;
using apex::hal::SpiDataSize;
using apex::hal::SpiMode;
using apex::hal::SpiStats;
using apex::hal::SpiStatus;
using apex::hal::toString;

/* ----------------------------- SpiStatus Tests ----------------------------- */

/** @test Verify SpiStatus::OK is zero. */
TEST(SpiStatus, OkIsZero) { EXPECT_EQ(static_cast<int>(SpiStatus::OK), 0); }

/** @test Verify toString returns non-null for all status values. */
TEST(SpiStatus, ToStringNonNull) {
  EXPECT_NE(toString(SpiStatus::OK), nullptr);
  EXPECT_NE(toString(SpiStatus::BUSY), nullptr);
  EXPECT_NE(toString(SpiStatus::ERROR_TIMEOUT), nullptr);
  EXPECT_NE(toString(SpiStatus::ERROR_OVERRUN), nullptr);
  EXPECT_NE(toString(SpiStatus::ERROR_CRC), nullptr);
  EXPECT_NE(toString(SpiStatus::ERROR_MODF), nullptr);
  EXPECT_NE(toString(SpiStatus::ERROR_NOT_INIT), nullptr);
  EXPECT_NE(toString(SpiStatus::ERROR_INVALID_ARG), nullptr);
}

/** @test Verify toString handles unknown status values. */
TEST(SpiStatus, ToStringUnknown) {
  const auto UNKNOWN = static_cast<SpiStatus>(255);
  EXPECT_STREQ(toString(UNKNOWN), "UNKNOWN");
}

/** @test Verify toString returns correct strings. */
TEST(SpiStatus, ToStringValues) {
  EXPECT_STREQ(toString(SpiStatus::OK), "OK");
  EXPECT_STREQ(toString(SpiStatus::BUSY), "BUSY");
  EXPECT_STREQ(toString(SpiStatus::ERROR_CRC), "ERROR_CRC");
  EXPECT_STREQ(toString(SpiStatus::ERROR_MODF), "ERROR_MODF");
}

/* ----------------------------- SpiMode Tests ----------------------------- */

/** @test Verify SpiMode enum values. */
TEST(SpiMode, EnumValues) {
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_0), 0);
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_1), 1);
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_2), 2);
  EXPECT_EQ(static_cast<int>(SpiMode::MODE_3), 3);
}

/* ----------------------------- SpiBitOrder Tests ----------------------------- */

/** @test Verify SpiBitOrder enum values. */
TEST(SpiBitOrder, EnumValues) {
  EXPECT_EQ(static_cast<int>(SpiBitOrder::MSB_FIRST), 0);
  EXPECT_EQ(static_cast<int>(SpiBitOrder::LSB_FIRST), 1);
}

/* ----------------------------- SpiDataSize Tests ----------------------------- */

/** @test Verify SpiDataSize enum values. */
TEST(SpiDataSize, EnumValues) {
  EXPECT_EQ(static_cast<int>(SpiDataSize::BITS_8), 0);
  EXPECT_EQ(static_cast<int>(SpiDataSize::BITS_16), 1);
}

/* ----------------------------- SpiConfig Tests ----------------------------- */

/** @test Verify SpiConfig default values. */
TEST(SpiConfig, DefaultValues) {
  const SpiConfig CONFIG;

  EXPECT_EQ(CONFIG.maxClockHz, 1000000U);
  EXPECT_EQ(CONFIG.mode, SpiMode::MODE_0);
  EXPECT_EQ(CONFIG.bitOrder, SpiBitOrder::MSB_FIRST);
  EXPECT_EQ(CONFIG.dataSize, SpiDataSize::BITS_8);
}

/** @test Verify SpiConfig is copyable. */
TEST(SpiConfig, Copyable) {
  SpiConfig config1;
  config1.maxClockHz = 8000000;
  config1.mode = SpiMode::MODE_3;
  config1.bitOrder = SpiBitOrder::LSB_FIRST;
  config1.dataSize = SpiDataSize::BITS_16;

  const SpiConfig CONFIG2 = config1;

  EXPECT_EQ(CONFIG2.maxClockHz, 8000000U);
  EXPECT_EQ(CONFIG2.mode, SpiMode::MODE_3);
  EXPECT_EQ(CONFIG2.bitOrder, SpiBitOrder::LSB_FIRST);
  EXPECT_EQ(CONFIG2.dataSize, SpiDataSize::BITS_16);
}

/* ----------------------------- SpiStats Tests ----------------------------- */

/** @test Verify SpiStats default values are zero. */
TEST(SpiStats, DefaultZero) {
  const SpiStats STATS;

  EXPECT_EQ(STATS.bytesTransferred, 0U);
  EXPECT_EQ(STATS.transferCount, 0U);
  EXPECT_EQ(STATS.crcErrors, 0U);
  EXPECT_EQ(STATS.modfErrors, 0U);
  EXPECT_EQ(STATS.overrunErrors, 0U);
}

/** @test Verify SpiStats reset clears all counters. */
TEST(SpiStats, Reset) {
  SpiStats stats;
  stats.bytesTransferred = 1000;
  stats.transferCount = 50;
  stats.crcErrors = 2;
  stats.modfErrors = 1;
  stats.overrunErrors = 3;

  stats.reset();

  EXPECT_EQ(stats.bytesTransferred, 0U);
  EXPECT_EQ(stats.transferCount, 0U);
  EXPECT_EQ(stats.crcErrors, 0U);
  EXPECT_EQ(stats.modfErrors, 0U);
  EXPECT_EQ(stats.overrunErrors, 0U);
}

/** @test Verify SpiStats totalErrors sums all error counters. */
TEST(SpiStats, TotalErrors) {
  SpiStats stats;
  stats.crcErrors = 1;
  stats.modfErrors = 2;
  stats.overrunErrors = 3;

  EXPECT_EQ(stats.totalErrors(), 6U);
}

/** @test Verify SpiStats totalErrors is zero when no errors. */
TEST(SpiStats, TotalErrorsZero) {
  const SpiStats STATS;
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify SpiStats totalErrors excludes non-error fields. */
TEST(SpiStats, TotalErrorsExcludesNonErrors) {
  SpiStats stats;
  stats.bytesTransferred = 9999;
  stats.transferCount = 100;

  EXPECT_EQ(stats.totalErrors(), 0U);
}
