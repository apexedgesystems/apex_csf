/**
 * @file IUart_uTest.cpp
 * @brief Unit tests for IUart interface and related types.
 */

#include "src/system/core/hal/base/IUart.hpp"

#include <gtest/gtest.h>

using apex::hal::IUart;
using apex::hal::toString;
using apex::hal::UartConfig;
using apex::hal::UartParity;
using apex::hal::UartStats;
using apex::hal::UartStatus;
using apex::hal::UartStopBits;

/* ----------------------------- UartStatus Tests ----------------------------- */

/** @test Verify UartStatus::OK is zero. */
TEST(UartStatus, OkIsZero) { EXPECT_EQ(static_cast<int>(UartStatus::OK), 0); }

/** @test Verify toString returns non-null for all status values. */
TEST(UartStatus, ToStringNonNull) {
  EXPECT_NE(toString(UartStatus::OK), nullptr);
  EXPECT_NE(toString(UartStatus::WOULD_BLOCK), nullptr);
  EXPECT_NE(toString(UartStatus::BUSY), nullptr);
  EXPECT_NE(toString(UartStatus::ERROR_TIMEOUT), nullptr);
  EXPECT_NE(toString(UartStatus::ERROR_OVERRUN), nullptr);
  EXPECT_NE(toString(UartStatus::ERROR_FRAMING), nullptr);
  EXPECT_NE(toString(UartStatus::ERROR_PARITY), nullptr);
  EXPECT_NE(toString(UartStatus::ERROR_NOISE), nullptr);
  EXPECT_NE(toString(UartStatus::ERROR_NOT_INIT), nullptr);
  EXPECT_NE(toString(UartStatus::ERROR_INVALID_ARG), nullptr);
}

/** @test Verify toString handles unknown status values. */
TEST(UartStatus, ToStringUnknown) {
  const auto UNKNOWN = static_cast<UartStatus>(255);
  EXPECT_STREQ(toString(UNKNOWN), "UNKNOWN");
}

/* ----------------------------- UartParity Tests ----------------------------- */

/** @test Verify UartParity enum values. */
TEST(UartParity, EnumValues) {
  EXPECT_EQ(static_cast<int>(UartParity::NONE), 0);
  EXPECT_EQ(static_cast<int>(UartParity::ODD), 1);
  EXPECT_EQ(static_cast<int>(UartParity::EVEN), 2);
}

/* ----------------------------- UartStopBits Tests ----------------------------- */

/** @test Verify UartStopBits enum values. */
TEST(UartStopBits, EnumValues) {
  EXPECT_EQ(static_cast<int>(UartStopBits::ONE), 0);
  EXPECT_EQ(static_cast<int>(UartStopBits::TWO), 1);
}

/* ----------------------------- UartConfig Tests ----------------------------- */

/** @test Verify UartConfig default values. */
TEST(UartConfig, DefaultValues) {
  const UartConfig CONFIG;

  EXPECT_EQ(CONFIG.baudRate, 115200U);
  EXPECT_EQ(CONFIG.dataBits, 8);
  EXPECT_EQ(CONFIG.parity, UartParity::NONE);
  EXPECT_EQ(CONFIG.stopBits, UartStopBits::ONE);
  EXPECT_FALSE(CONFIG.hwFlowControl);
}

/** @test Verify UartConfig is copyable. */
TEST(UartConfig, Copyable) {
  UartConfig config1;
  config1.baudRate = 9600;
  config1.parity = UartParity::EVEN;

  const UartConfig CONFIG2 = config1;

  EXPECT_EQ(CONFIG2.baudRate, 9600U);
  EXPECT_EQ(CONFIG2.parity, UartParity::EVEN);
}

/* ----------------------------- UartStats Tests ----------------------------- */

/** @test Verify UartStats default values are zero. */
TEST(UartStats, DefaultZero) {
  const UartStats STATS;

  EXPECT_EQ(STATS.bytesRx, 0U);
  EXPECT_EQ(STATS.bytesTx, 0U);
  EXPECT_EQ(STATS.overrunErrors, 0U);
  EXPECT_EQ(STATS.framingErrors, 0U);
  EXPECT_EQ(STATS.parityErrors, 0U);
  EXPECT_EQ(STATS.noiseErrors, 0U);
}

/** @test Verify UartStats reset clears all counters. */
TEST(UartStats, Reset) {
  UartStats stats;
  stats.bytesRx = 100;
  stats.bytesTx = 200;
  stats.overrunErrors = 5;
  stats.framingErrors = 3;

  stats.reset();

  EXPECT_EQ(stats.bytesRx, 0U);
  EXPECT_EQ(stats.bytesTx, 0U);
  EXPECT_EQ(stats.overrunErrors, 0U);
  EXPECT_EQ(stats.framingErrors, 0U);
  EXPECT_EQ(stats.parityErrors, 0U);
  EXPECT_EQ(stats.noiseErrors, 0U);
}

/** @test Verify UartStats totalErrors sums all error counters. */
TEST(UartStats, TotalErrors) {
  UartStats stats;
  stats.overrunErrors = 1;
  stats.framingErrors = 2;
  stats.parityErrors = 3;
  stats.noiseErrors = 4;

  EXPECT_EQ(stats.totalErrors(), 10U);
}

/** @test Verify UartStats totalErrors is zero when no errors. */
TEST(UartStats, TotalErrorsZero) {
  const UartStats STATS;
  EXPECT_EQ(STATS.totalErrors(), 0U);
}
