/**
 * @file ICan_uTest.cpp
 * @brief Unit tests for ICan interface and related types.
 */

#include "src/system/core/hal/base/ICan.hpp"

#include <gtest/gtest.h>

using apex::hal::CanConfig;
using apex::hal::CanFilter;
using apex::hal::CanFrame;
using apex::hal::CanId;
using apex::hal::CanMode;
using apex::hal::CanStats;
using apex::hal::CanStatus;
using apex::hal::toString;

/* ----------------------------- CanStatus Tests ----------------------------- */

/** @test Verify CanStatus::OK is zero. */
TEST(CanStatus, OkIsZero) { EXPECT_EQ(static_cast<int>(CanStatus::OK), 0); }

/** @test Verify toString returns non-null for all status values. */
TEST(CanStatus, ToStringNonNull) {
  EXPECT_NE(toString(CanStatus::OK), nullptr);
  EXPECT_NE(toString(CanStatus::WOULD_BLOCK), nullptr);
  EXPECT_NE(toString(CanStatus::BUSY), nullptr);
  EXPECT_NE(toString(CanStatus::ERROR_TIMEOUT), nullptr);
  EXPECT_NE(toString(CanStatus::ERROR_OVERRUN), nullptr);
  EXPECT_NE(toString(CanStatus::ERROR_BUS_OFF), nullptr);
  EXPECT_NE(toString(CanStatus::ERROR_PASSIVE), nullptr);
  EXPECT_NE(toString(CanStatus::ERROR_NOT_INIT), nullptr);
  EXPECT_NE(toString(CanStatus::ERROR_INVALID_ARG), nullptr);
}

/** @test Verify toString handles unknown status values. */
TEST(CanStatus, ToStringUnknown) {
  const auto UNKNOWN = static_cast<CanStatus>(255);
  EXPECT_STREQ(toString(UNKNOWN), "UNKNOWN");
}

/* ----------------------------- CanMode Tests ----------------------------- */

/** @test Verify CanMode enum values. */
TEST(CanMode, EnumValues) {
  EXPECT_EQ(static_cast<int>(CanMode::NORMAL), 0);
  EXPECT_EQ(static_cast<int>(CanMode::LOOPBACK), 1);
  EXPECT_EQ(static_cast<int>(CanMode::SILENT), 2);
  EXPECT_EQ(static_cast<int>(CanMode::SILENT_LOOPBACK), 3);
}

/* ----------------------------- CanId Tests ----------------------------- */

/** @test Verify CanId default values. */
TEST(CanId, DefaultValues) {
  const CanId ID;

  EXPECT_EQ(ID.id, 0U);
  EXPECT_FALSE(ID.extended);
  EXPECT_FALSE(ID.remote);
}

/** @test Verify CanId aggregate initialization. */
TEST(CanId, AggregateInit) {
  const CanId ID = {0x123, true, false};

  EXPECT_EQ(ID.id, 0x123U);
  EXPECT_TRUE(ID.extended);
  EXPECT_FALSE(ID.remote);
}

/* ----------------------------- CanFrame Tests ----------------------------- */

/** @test Verify CanFrame default values. */
TEST(CanFrame, DefaultValues) {
  const CanFrame FRAME;

  EXPECT_EQ(FRAME.canId.id, 0U);
  EXPECT_FALSE(FRAME.canId.extended);
  EXPECT_EQ(FRAME.dlc, 0U);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(FRAME.data[i], 0U);
  }
}

/** @test Verify CanFrame is copyable. */
TEST(CanFrame, Copyable) {
  CanFrame frame1;
  frame1.canId.id = 0x7FF;
  frame1.dlc = 2;
  frame1.data[0] = 0xAB;
  frame1.data[1] = 0xCD;

  const CanFrame FRAME2 = frame1;

  EXPECT_EQ(FRAME2.canId.id, 0x7FFU);
  EXPECT_EQ(FRAME2.dlc, 2U);
  EXPECT_EQ(FRAME2.data[0], 0xABU);
  EXPECT_EQ(FRAME2.data[1], 0xCDU);
}

/* ----------------------------- CanFilter Tests ----------------------------- */

/** @test Verify CanFilter default values. */
TEST(CanFilter, DefaultValues) {
  const CanFilter FILTER;

  EXPECT_EQ(FILTER.id, 0U);
  EXPECT_EQ(FILTER.mask, 0U);
  EXPECT_FALSE(FILTER.extended);
}

/** @test Verify CanFilter aggregate initialization. */
TEST(CanFilter, AggregateInit) {
  const CanFilter FILTER = {0x321, 0x7FF, false};

  EXPECT_EQ(FILTER.id, 0x321U);
  EXPECT_EQ(FILTER.mask, 0x7FFU);
  EXPECT_FALSE(FILTER.extended);
}

/* ----------------------------- CanConfig Tests ----------------------------- */

/** @test Verify CanConfig default values. */
TEST(CanConfig, DefaultValues) {
  const CanConfig CONFIG;

  EXPECT_EQ(CONFIG.bitrate, 500000U);
  EXPECT_EQ(CONFIG.mode, CanMode::NORMAL);
  EXPECT_TRUE(CONFIG.autoRetransmit);
}

/** @test Verify CanConfig is copyable. */
TEST(CanConfig, Copyable) {
  CanConfig config1;
  config1.bitrate = 250000;
  config1.mode = CanMode::LOOPBACK;
  config1.autoRetransmit = false;

  const CanConfig CONFIG2 = config1;

  EXPECT_EQ(CONFIG2.bitrate, 250000U);
  EXPECT_EQ(CONFIG2.mode, CanMode::LOOPBACK);
  EXPECT_FALSE(CONFIG2.autoRetransmit);
}

/* ----------------------------- CanStats Tests ----------------------------- */

/** @test Verify CanStats default values are zero. */
TEST(CanStats, DefaultZero) {
  const CanStats STATS;

  EXPECT_EQ(STATS.framesTx, 0U);
  EXPECT_EQ(STATS.framesRx, 0U);
  EXPECT_EQ(STATS.errorFrames, 0U);
  EXPECT_EQ(STATS.busOffCount, 0U);
  EXPECT_EQ(STATS.txOverflows, 0U);
  EXPECT_EQ(STATS.rxOverflows, 0U);
}

/** @test Verify CanStats reset clears all counters. */
TEST(CanStats, Reset) {
  CanStats stats;
  stats.framesTx = 100;
  stats.framesRx = 200;
  stats.errorFrames = 5;
  stats.busOffCount = 1;
  stats.txOverflows = 3;
  stats.rxOverflows = 7;

  stats.reset();

  EXPECT_EQ(stats.framesTx, 0U);
  EXPECT_EQ(stats.framesRx, 0U);
  EXPECT_EQ(stats.errorFrames, 0U);
  EXPECT_EQ(stats.busOffCount, 0U);
  EXPECT_EQ(stats.txOverflows, 0U);
  EXPECT_EQ(stats.rxOverflows, 0U);
}

/** @test Verify CanStats totalErrors sums all error counters. */
TEST(CanStats, TotalErrors) {
  CanStats stats;
  stats.errorFrames = 1;
  stats.busOffCount = 2;
  stats.txOverflows = 3;
  stats.rxOverflows = 4;

  EXPECT_EQ(stats.totalErrors(), 10U);
}

/** @test Verify CanStats totalErrors is zero when no errors. */
TEST(CanStats, TotalErrorsZero) {
  const CanStats STATS;
  EXPECT_EQ(STATS.totalErrors(), 0U);
}
