/**
 * @file ShutdownConfig_uTest.cpp
 * @brief Unit tests for ShutdownConfig and ShutdownStage.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive_Shutdown.hpp"

#include <gtest/gtest.h>

/* ----------------------------- ShutdownConfig Mode Tests ----------------------------- */

/** @test ShutdownConfig::Mode enum values. */
TEST(ShutdownConfigMode, EnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownConfig::SIGNAL_ONLY), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownConfig::SCHEDULED), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownConfig::RELATIVE_TIME), 2);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownConfig::CLOCK_CYCLE), 3);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownConfig::COMBINED), 4);
}

/* ----------------------------- ShutdownConfig Default Tests ----------------------------- */

/** @test ShutdownConfig default construction. */
TEST(ShutdownConfig, DefaultConstruction) {
  const executive::ShutdownConfig config{};

  EXPECT_EQ(config.mode, executive::ShutdownConfig::SIGNAL_ONLY);
  EXPECT_EQ(config.shutdownAtEpochNs, 0);
  EXPECT_EQ(config.relativeSeconds, 0);
  EXPECT_EQ(config.targetClockCycle, 0);
  EXPECT_TRUE(config.allowEarlySignal);
  EXPECT_FALSE(config.skipCleanup);
}

/** @test ShutdownConfig can be modified. */
TEST(ShutdownConfig, CanBeModified) {
  executive::ShutdownConfig config{};

  config.mode = executive::ShutdownConfig::CLOCK_CYCLE;
  config.targetClockCycle = 6000;
  config.skipCleanup = true;

  EXPECT_EQ(config.mode, executive::ShutdownConfig::CLOCK_CYCLE);
  EXPECT_EQ(config.targetClockCycle, 6000U);
  EXPECT_TRUE(config.skipCleanup);
}

/* ----------------------------- ShutdownStage Tests ----------------------------- */

/** @test ShutdownStage enum values are sequential. */
TEST(ShutdownStage, EnumValuesAreSequential) {
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownStage::STAGE_SIGNAL_RECEIVED), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownStage::STAGE_STOP_CLOCK), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownStage::STAGE_DRAIN_TASKS), 2);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownStage::STAGE_CLEANUP_RESOURCES), 3);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownStage::STAGE_FINAL_STATS), 4);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownStage::STAGE_COMPLETE), 5);
}

/** @test ShutdownStage count. */
TEST(ShutdownStage, HasSixStages) {
  constexpr std::uint8_t STAGE_COUNT = 6;
  EXPECT_EQ(static_cast<std::uint8_t>(executive::ShutdownStage::STAGE_COMPLETE) + 1, STAGE_COUNT);
}
