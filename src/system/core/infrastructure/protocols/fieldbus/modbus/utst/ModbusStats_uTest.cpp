/**
 * @file ModbusStats_uTest.cpp
 * @brief Unit tests for ModbusStats struct.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStats.hpp"

#include <gtest/gtest.h>

using apex::protocols::fieldbus::modbus::ModbusStats;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed stats have all counters at zero. */
TEST(ModbusStatsTest, DefaultConstruction) {
  ModbusStats stats;
  EXPECT_EQ(stats.requestsSent, 0);
  EXPECT_EQ(stats.responsesReceived, 0);
  EXPECT_EQ(stats.exceptionsReceived, 0);
  EXPECT_EQ(stats.bytesTx, 0);
  EXPECT_EQ(stats.bytesRx, 0);
  EXPECT_EQ(stats.crcErrors, 0);
  EXPECT_EQ(stats.frameErrors, 0);
  EXPECT_EQ(stats.timeouts, 0);
  EXPECT_EQ(stats.ioErrors, 0);
  EXPECT_EQ(stats.lastResponseTimeUs, 0);
}

/* ----------------------------- reset ----------------------------- */

/** @test reset() clears all counters to zero. */
TEST(ModbusStatsTest, Reset) {
  ModbusStats stats;
  stats.requestsSent = 100;
  stats.responsesReceived = 90;
  stats.exceptionsReceived = 5;
  stats.bytesTx = 1000;
  stats.bytesRx = 900;
  stats.crcErrors = 2;
  stats.frameErrors = 1;
  stats.timeouts = 3;
  stats.ioErrors = 1;
  stats.lastResponseTimeUs = 5000;

  stats.reset();

  EXPECT_EQ(stats.requestsSent, 0);
  EXPECT_EQ(stats.responsesReceived, 0);
  EXPECT_EQ(stats.exceptionsReceived, 0);
  EXPECT_EQ(stats.bytesTx, 0);
  EXPECT_EQ(stats.bytesRx, 0);
  EXPECT_EQ(stats.crcErrors, 0);
  EXPECT_EQ(stats.frameErrors, 0);
  EXPECT_EQ(stats.timeouts, 0);
  EXPECT_EQ(stats.ioErrors, 0);
  EXPECT_EQ(stats.lastResponseTimeUs, 0);
}

/* ----------------------------- totalBytes ----------------------------- */

/** @test totalBytes returns sum of bytesTx and bytesRx. */
TEST(ModbusStatsTest, TotalBytes) {
  ModbusStats stats;
  stats.bytesTx = 1000;
  stats.bytesRx = 500;
  EXPECT_EQ(stats.totalBytes(), 1500);
}

/** @test totalBytes returns zero for default stats. */
TEST(ModbusStatsTest, TotalBytesZero) {
  ModbusStats stats;
  EXPECT_EQ(stats.totalBytes(), 0);
}

/* ----------------------------- totalTransactions ----------------------------- */

/** @test totalTransactions returns requestsSent. */
TEST(ModbusStatsTest, TotalTransactions) {
  ModbusStats stats;
  stats.requestsSent = 42;
  EXPECT_EQ(stats.totalTransactions(), 42);
}

/* ----------------------------- totalResponses ----------------------------- */

/** @test totalResponses returns sum of responses and exceptions. */
TEST(ModbusStatsTest, TotalResponses) {
  ModbusStats stats;
  stats.responsesReceived = 90;
  stats.exceptionsReceived = 5;
  EXPECT_EQ(stats.totalResponses(), 95);
}

/* ----------------------------- totalErrors ----------------------------- */

/** @test totalErrors returns sum of all error counters. */
TEST(ModbusStatsTest, TotalErrors) {
  ModbusStats stats;
  stats.crcErrors = 2;
  stats.frameErrors = 1;
  stats.timeouts = 3;
  stats.ioErrors = 1;
  EXPECT_EQ(stats.totalErrors(), 7);
}

/** @test totalErrors returns zero for default stats. */
TEST(ModbusStatsTest, TotalErrorsZero) {
  ModbusStats stats;
  EXPECT_EQ(stats.totalErrors(), 0);
}

/* ----------------------------- successRate ----------------------------- */

/** @test successRate returns 100.0 for perfect communication. */
TEST(ModbusStatsTest, SuccessRatePerfect) {
  ModbusStats stats;
  stats.requestsSent = 100;
  stats.responsesReceived = 100;
  EXPECT_DOUBLE_EQ(stats.successRate(), 100.0);
}

/** @test successRate returns 0.0 for no requests. */
TEST(ModbusStatsTest, SuccessRateNoRequests) {
  ModbusStats stats;
  EXPECT_DOUBLE_EQ(stats.successRate(), 0.0);
}

/** @test successRate calculates correctly for partial success. */
TEST(ModbusStatsTest, SuccessRatePartial) {
  ModbusStats stats;
  stats.requestsSent = 100;
  stats.responsesReceived = 75;
  EXPECT_DOUBLE_EQ(stats.successRate(), 75.0);
}

/** @test successRate does not count exceptions as failures. */
TEST(ModbusStatsTest, SuccessRateWithExceptions) {
  ModbusStats stats;
  stats.requestsSent = 100;
  stats.responsesReceived = 80;  // 80% success rate
  stats.exceptionsReceived = 10; // These are valid responses, not counted in success
  // Success rate is responsesReceived / requestsSent = 80%
  EXPECT_DOUBLE_EQ(stats.successRate(), 80.0);
}
