/**
 * @file RfcommStats_uTest.cpp
 * @brief Unit tests for RfcommStats.
 */

#include "RfcommStats.hpp"

#include <gtest/gtest.h>

namespace bt = apex::protocols::wireless::bluetooth;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify default construction initializes all fields to zero. */
TEST(RfcommStats, DefaultConstruction) {
  bt::RfcommStats stats;

  EXPECT_EQ(stats.bytesRx, 0u);
  EXPECT_EQ(stats.bytesTx, 0u);
  EXPECT_EQ(stats.readsCompleted, 0u);
  EXPECT_EQ(stats.writesCompleted, 0u);
  EXPECT_EQ(stats.readWouldBlock, 0u);
  EXPECT_EQ(stats.writeWouldBlock, 0u);
  EXPECT_EQ(stats.readErrors, 0u);
  EXPECT_EQ(stats.writeErrors, 0u);
  EXPECT_EQ(stats.connectAttempts, 0u);
  EXPECT_EQ(stats.connectSuccesses, 0u);
  EXPECT_EQ(stats.disconnects, 0u);
}

/* ----------------------------- Method Tests ----------------------------- */

/** @test Verify reset clears all counters. */
TEST(RfcommStats, ResetClearsAllCounters) {
  bt::RfcommStats stats;
  stats.bytesRx = 100;
  stats.bytesTx = 200;
  stats.readsCompleted = 10;
  stats.writesCompleted = 20;
  stats.readWouldBlock = 5;
  stats.writeWouldBlock = 3;
  stats.readErrors = 2;
  stats.writeErrors = 1;
  stats.connectAttempts = 3;
  stats.connectSuccesses = 2;
  stats.disconnects = 1;

  stats.reset();

  EXPECT_EQ(stats.bytesRx, 0u);
  EXPECT_EQ(stats.bytesTx, 0u);
  EXPECT_EQ(stats.readsCompleted, 0u);
  EXPECT_EQ(stats.writesCompleted, 0u);
  EXPECT_EQ(stats.readWouldBlock, 0u);
  EXPECT_EQ(stats.writeWouldBlock, 0u);
  EXPECT_EQ(stats.readErrors, 0u);
  EXPECT_EQ(stats.writeErrors, 0u);
  EXPECT_EQ(stats.connectAttempts, 0u);
  EXPECT_EQ(stats.connectSuccesses, 0u);
  EXPECT_EQ(stats.disconnects, 0u);
}

/** @test Verify totalBytes calculation. */
TEST(RfcommStats, TotalBytesCalculation) {
  bt::RfcommStats stats;
  stats.bytesRx = 100;
  stats.bytesTx = 200;

  EXPECT_EQ(stats.totalBytes(), 300u);
}

/** @test Verify totalErrors calculation. */
TEST(RfcommStats, TotalErrorsCalculation) {
  bt::RfcommStats stats;
  stats.readErrors = 5;
  stats.writeErrors = 3;

  EXPECT_EQ(stats.totalErrors(), 8u);
}

/** @test Verify totalOperations calculation. */
TEST(RfcommStats, TotalOperationsCalculation) {
  bt::RfcommStats stats;
  stats.readsCompleted = 10;
  stats.writesCompleted = 20;

  EXPECT_EQ(stats.totalOperations(), 30u);
}

/** @test Verify totalWouldBlock calculation. */
TEST(RfcommStats, TotalWouldBlockCalculation) {
  bt::RfcommStats stats;
  stats.readWouldBlock = 7;
  stats.writeWouldBlock = 4;

  EXPECT_EQ(stats.totalWouldBlock(), 11u);
}

/** @test Verify connectionSuccessRate with no attempts. */
TEST(RfcommStats, ConnectionSuccessRateNoAttempts) {
  bt::RfcommStats stats;
  EXPECT_DOUBLE_EQ(stats.connectionSuccessRate(), 1.0);
}

/** @test Verify connectionSuccessRate with attempts. */
TEST(RfcommStats, ConnectionSuccessRateWithAttempts) {
  bt::RfcommStats stats;
  stats.connectAttempts = 10;
  stats.connectSuccesses = 8;

  EXPECT_DOUBLE_EQ(stats.connectionSuccessRate(), 0.8);
}

/** @test Verify connectionSuccessRate with zero successes. */
TEST(RfcommStats, ConnectionSuccessRateZeroSuccesses) {
  bt::RfcommStats stats;
  stats.connectAttempts = 5;
  stats.connectSuccesses = 0;

  EXPECT_DOUBLE_EQ(stats.connectionSuccessRate(), 0.0);
}
