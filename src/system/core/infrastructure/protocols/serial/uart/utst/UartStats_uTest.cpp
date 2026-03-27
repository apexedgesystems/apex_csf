/**
 * @file UartStats_uTest.cpp
 * @brief Unit tests for UartStats struct.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStats.hpp"

#include <gtest/gtest.h>

using apex::protocols::serial::uart::UartStats;

/* ----------------------------- Default Construction ----------------------------- */

/** @test UartStats default construction initializes all counters to zero. */
TEST(UartStatsTest, DefaultConstruction) {
  UartStats stats{};
  EXPECT_EQ(stats.bytesRx, 0u);
  EXPECT_EQ(stats.bytesTx, 0u);
  EXPECT_EQ(stats.readsCompleted, 0u);
  EXPECT_EQ(stats.writesCompleted, 0u);
  EXPECT_EQ(stats.readWouldBlock, 0u);
  EXPECT_EQ(stats.writeWouldBlock, 0u);
  EXPECT_EQ(stats.readErrors, 0u);
  EXPECT_EQ(stats.writeErrors, 0u);
}

/* ----------------------------- Method Tests ----------------------------- */

/** @test UartStats::reset() sets all counters to zero. */
TEST(UartStatsTest, ResetSetsAllToZero) {
  UartStats stats{};
  stats.bytesRx = 1000;
  stats.bytesTx = 2000;
  stats.readsCompleted = 100;
  stats.writesCompleted = 200;
  stats.readWouldBlock = 10;
  stats.writeWouldBlock = 20;
  stats.readErrors = 3;
  stats.writeErrors = 4;

  stats.reset();

  EXPECT_EQ(stats.bytesRx, 0u);
  EXPECT_EQ(stats.bytesTx, 0u);
  EXPECT_EQ(stats.readsCompleted, 0u);
  EXPECT_EQ(stats.writesCompleted, 0u);
  EXPECT_EQ(stats.readWouldBlock, 0u);
  EXPECT_EQ(stats.writeWouldBlock, 0u);
  EXPECT_EQ(stats.readErrors, 0u);
  EXPECT_EQ(stats.writeErrors, 0u);
}

/** @test UartStats::totalBytes() returns sum of received and transmitted. */
TEST(UartStatsTest, TotalBytesReturnsSum) {
  UartStats stats{};
  stats.bytesRx = 1000;
  stats.bytesTx = 2500;
  EXPECT_EQ(stats.totalBytes(), 3500u);
}

/** @test UartStats::totalErrors() returns sum of read and write errors. */
TEST(UartStatsTest, TotalErrorsReturnsSum) {
  UartStats stats{};
  stats.readErrors = 5;
  stats.writeErrors = 8;
  EXPECT_EQ(stats.totalErrors(), 13u);
}

/** @test UartStats::totalOperations() returns sum of reads and writes completed. */
TEST(UartStatsTest, TotalOperationsReturnsSum) {
  UartStats stats{};
  stats.readsCompleted = 50;
  stats.writesCompleted = 75;
  EXPECT_EQ(stats.totalOperations(), 125u);
}

/** @test UartStats::totalWouldBlock() returns sum of read and write would-block events. */
TEST(UartStatsTest, TotalWouldBlockReturnsSum) {
  UartStats stats{};
  stats.readWouldBlock = 15;
  stats.writeWouldBlock = 25;
  EXPECT_EQ(stats.totalWouldBlock(), 40u);
}

/* ----------------------------- Noexcept Tests ----------------------------- */

/** @test Helper methods are noexcept per header contract. */
TEST(UartStatsTest, NoexceptContract) {
  UartStats stats{};
  static_assert(noexcept(stats.reset()), "reset() should be noexcept");
  static_assert(noexcept(stats.totalBytes()), "totalBytes() should be noexcept");
  static_assert(noexcept(stats.totalErrors()), "totalErrors() should be noexcept");
  static_assert(noexcept(stats.totalOperations()), "totalOperations() should be noexcept");
  static_assert(noexcept(stats.totalWouldBlock()), "totalWouldBlock() should be noexcept");
  SUCCEED();
}
