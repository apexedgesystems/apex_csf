/**
 * @file II2c_uTest.cpp
 * @brief Unit tests for II2c interface and related types.
 */

#include "src/system/core/hal/base/II2c.hpp"

#include <gtest/gtest.h>

using apex::hal::I2cAddressMode;
using apex::hal::I2cConfig;
using apex::hal::I2cSpeed;
using apex::hal::I2cStats;
using apex::hal::I2cStatus;
using apex::hal::toString;

/* ----------------------------- I2cStatus Tests ----------------------------- */

/** @test Verify I2cStatus::OK is zero. */
TEST(I2cStatus, OkIsZero) { EXPECT_EQ(static_cast<int>(I2cStatus::OK), 0); }

/** @test Verify toString returns non-null for all status values. */
TEST(I2cStatus, ToStringNonNull) {
  EXPECT_NE(toString(I2cStatus::OK), nullptr);
  EXPECT_NE(toString(I2cStatus::BUSY), nullptr);
  EXPECT_NE(toString(I2cStatus::ERROR_TIMEOUT), nullptr);
  EXPECT_NE(toString(I2cStatus::ERROR_NACK), nullptr);
  EXPECT_NE(toString(I2cStatus::ERROR_BUS), nullptr);
  EXPECT_NE(toString(I2cStatus::ERROR_ARBITRATION), nullptr);
  EXPECT_NE(toString(I2cStatus::ERROR_NOT_INIT), nullptr);
  EXPECT_NE(toString(I2cStatus::ERROR_INVALID_ARG), nullptr);
}

/** @test Verify toString handles unknown status values. */
TEST(I2cStatus, ToStringUnknown) {
  const auto UNKNOWN = static_cast<I2cStatus>(255);
  EXPECT_STREQ(toString(UNKNOWN), "UNKNOWN");
}

/** @test Verify toString returns correct strings. */
TEST(I2cStatus, ToStringValues) {
  EXPECT_STREQ(toString(I2cStatus::OK), "OK");
  EXPECT_STREQ(toString(I2cStatus::BUSY), "BUSY");
  EXPECT_STREQ(toString(I2cStatus::ERROR_NACK), "ERROR_NACK");
  EXPECT_STREQ(toString(I2cStatus::ERROR_BUS), "ERROR_BUS");
  EXPECT_STREQ(toString(I2cStatus::ERROR_ARBITRATION), "ERROR_ARBITRATION");
}

/* ----------------------------- I2cSpeed Tests ----------------------------- */

/** @test Verify I2cSpeed enum values. */
TEST(I2cSpeed, EnumValues) {
  EXPECT_EQ(static_cast<int>(I2cSpeed::STANDARD), 0);
  EXPECT_EQ(static_cast<int>(I2cSpeed::FAST), 1);
  EXPECT_EQ(static_cast<int>(I2cSpeed::FAST_PLUS), 2);
}

/* ----------------------------- I2cAddressMode Tests ----------------------------- */

/** @test Verify I2cAddressMode enum values. */
TEST(I2cAddressMode, EnumValues) {
  EXPECT_EQ(static_cast<int>(I2cAddressMode::SEVEN_BIT), 0);
  EXPECT_EQ(static_cast<int>(I2cAddressMode::TEN_BIT), 1);
}

/* ----------------------------- I2cConfig Tests ----------------------------- */

/** @test Verify I2cConfig default values. */
TEST(I2cConfig, DefaultValues) {
  const I2cConfig CONFIG;

  EXPECT_EQ(CONFIG.speed, I2cSpeed::STANDARD);
  EXPECT_EQ(CONFIG.addressMode, I2cAddressMode::SEVEN_BIT);
}

/** @test Verify I2cConfig is copyable. */
TEST(I2cConfig, Copyable) {
  I2cConfig config1;
  config1.speed = I2cSpeed::FAST_PLUS;
  config1.addressMode = I2cAddressMode::TEN_BIT;

  const I2cConfig CONFIG2 = config1;

  EXPECT_EQ(CONFIG2.speed, I2cSpeed::FAST_PLUS);
  EXPECT_EQ(CONFIG2.addressMode, I2cAddressMode::TEN_BIT);
}

/* ----------------------------- I2cStats Tests ----------------------------- */

/** @test Verify I2cStats default values are zero. */
TEST(I2cStats, DefaultZero) {
  const I2cStats STATS;

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.transferCount, 0U);
  EXPECT_EQ(STATS.nackErrors, 0U);
  EXPECT_EQ(STATS.busErrors, 0U);
  EXPECT_EQ(STATS.arbitrationErrors, 0U);
  EXPECT_EQ(STATS.timeoutErrors, 0U);
}

/** @test Verify I2cStats reset clears all counters. */
TEST(I2cStats, Reset) {
  I2cStats stats;
  stats.bytesRx = 500;
  stats.bytesTx = 300;
  stats.transferCount = 50;
  stats.nackErrors = 2;
  stats.busErrors = 1;
  stats.arbitrationErrors = 3;
  stats.timeoutErrors = 4;

  stats.reset();

  EXPECT_EQ(stats.bytesRx, 0U);
  EXPECT_EQ(stats.bytesTx, 0U);
  EXPECT_EQ(stats.transferCount, 0U);
  EXPECT_EQ(stats.nackErrors, 0U);
  EXPECT_EQ(stats.busErrors, 0U);
  EXPECT_EQ(stats.arbitrationErrors, 0U);
  EXPECT_EQ(stats.timeoutErrors, 0U);
}

/** @test Verify I2cStats totalErrors sums all error counters. */
TEST(I2cStats, TotalErrors) {
  I2cStats stats;
  stats.nackErrors = 1;
  stats.busErrors = 2;
  stats.arbitrationErrors = 3;
  stats.timeoutErrors = 4;

  EXPECT_EQ(stats.totalErrors(), 10U);
}

/** @test Verify I2cStats totalErrors is zero when no errors. */
TEST(I2cStats, TotalErrorsZero) {
  const I2cStats STATS;
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify I2cStats totalErrors excludes non-error fields. */
TEST(I2cStats, TotalErrorsExcludesNonErrors) {
  I2cStats stats;
  stats.bytesRx = 9999;
  stats.bytesTx = 8888;
  stats.transferCount = 100;

  EXPECT_EQ(stats.totalErrors(), 0U);
}
