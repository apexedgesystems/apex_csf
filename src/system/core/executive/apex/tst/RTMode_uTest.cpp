/**
 * @file RTMode_uTest.cpp
 * @brief Unit tests for RTMode enum, RTConfig, and helper functions.
 */

#include "src/system/core/executive/apex/inc/RTMode.hpp"

#include <gtest/gtest.h>

/* ----------------------------- RTMode Enum Tests ----------------------------- */

/** @test RTMode enum values are sequential starting from 0. */
TEST(RTMode, EnumValuesAreSequential) {
  EXPECT_EQ(static_cast<std::uint8_t>(executive::RTMode::HARD_TICK_COMPLETE), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::RTMode::HARD_PERIOD_COMPLETE), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::RTMode::SOFT_SKIP_ON_BUSY), 2);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::RTMode::SOFT_LAG_TOLERANT), 3);
  EXPECT_EQ(static_cast<std::uint8_t>(executive::RTMode::SOFT_LOG_ONLY), 4);
}

/* ----------------------------- rtModeToString Tests ----------------------------- */

/** @test rtModeToString returns correct strings for all modes. */
TEST(RTMode, RtModeToStringReturnsCorrectStrings) {
  EXPECT_EQ(executive::rtModeToString(executive::RTMode::HARD_TICK_COMPLETE), "HARD_TICK_COMPLETE");
  EXPECT_EQ(executive::rtModeToString(executive::RTMode::HARD_PERIOD_COMPLETE),
            "HARD_PERIOD_COMPLETE");
  EXPECT_EQ(executive::rtModeToString(executive::RTMode::SOFT_SKIP_ON_BUSY), "SOFT_SKIP_ON_BUSY");
  EXPECT_EQ(executive::rtModeToString(executive::RTMode::SOFT_LAG_TOLERANT), "SOFT_LAG_TOLERANT");
  EXPECT_EQ(executive::rtModeToString(executive::RTMode::SOFT_LOG_ONLY), "SOFT_LOG_ONLY");
}

/** @test rtModeToString returns UNKNOWN for invalid values. */
TEST(RTMode, RtModeToStringReturnsUnknownForInvalidValue) {
  const auto invalid = static_cast<executive::RTMode>(255);
  EXPECT_EQ(executive::rtModeToString(invalid), "UNKNOWN");
}

/** @test rtModeToString is constexpr. */
TEST(RTMode, RtModeToStringIsConstexpr) {
  constexpr auto str = executive::rtModeToString(executive::RTMode::HARD_PERIOD_COMPLETE);
  EXPECT_EQ(str, "HARD_PERIOD_COMPLETE");
}

/* ----------------------------- parseRTMode Tests ----------------------------- */

/** @test parseRTMode parses short form strings. */
TEST(RTMode, ParseRTModeShortForm) {
  executive::RTMode mode{};

  EXPECT_TRUE(executive::parseRTMode("tick-complete", mode));
  EXPECT_EQ(mode, executive::RTMode::HARD_TICK_COMPLETE);

  EXPECT_TRUE(executive::parseRTMode("period-complete", mode));
  EXPECT_EQ(mode, executive::RTMode::HARD_PERIOD_COMPLETE);

  EXPECT_TRUE(executive::parseRTMode("skip-on-busy", mode));
  EXPECT_EQ(mode, executive::RTMode::SOFT_SKIP_ON_BUSY);

  EXPECT_TRUE(executive::parseRTMode("lag-tolerant", mode));
  EXPECT_EQ(mode, executive::RTMode::SOFT_LAG_TOLERANT);

  EXPECT_TRUE(executive::parseRTMode("log-only", mode));
  EXPECT_EQ(mode, executive::RTMode::SOFT_LOG_ONLY);
}

/** @test parseRTMode parses enum name strings. */
TEST(RTMode, ParseRTModeEnumNames) {
  executive::RTMode mode{};

  EXPECT_TRUE(executive::parseRTMode("HARD_TICK_COMPLETE", mode));
  EXPECT_EQ(mode, executive::RTMode::HARD_TICK_COMPLETE);

  EXPECT_TRUE(executive::parseRTMode("HARD_PERIOD_COMPLETE", mode));
  EXPECT_EQ(mode, executive::RTMode::HARD_PERIOD_COMPLETE);

  EXPECT_TRUE(executive::parseRTMode("SOFT_SKIP_ON_BUSY", mode));
  EXPECT_EQ(mode, executive::RTMode::SOFT_SKIP_ON_BUSY);

  EXPECT_TRUE(executive::parseRTMode("SOFT_LAG_TOLERANT", mode));
  EXPECT_EQ(mode, executive::RTMode::SOFT_LAG_TOLERANT);

  EXPECT_TRUE(executive::parseRTMode("SOFT_LOG_ONLY", mode));
  EXPECT_EQ(mode, executive::RTMode::SOFT_LOG_ONLY);
}

/** @test parseRTMode returns false for invalid strings. */
TEST(RTMode, ParseRTModeInvalidStrings) {
  executive::RTMode mode = executive::RTMode::HARD_PERIOD_COMPLETE;

  EXPECT_FALSE(executive::parseRTMode("invalid", mode));
  EXPECT_FALSE(executive::parseRTMode("", mode));
  EXPECT_FALSE(executive::parseRTMode("HARD", mode));
  EXPECT_FALSE(executive::parseRTMode("soft_log_only", mode)); // Wrong case

  // Mode should remain unchanged on failure
  EXPECT_EQ(mode, executive::RTMode::HARD_PERIOD_COMPLETE);
}

/* ----------------------------- RTConfig Tests ----------------------------- */

/** @test RTConfig default construction. */
TEST(RTConfig, DefaultConstruction) {
  const executive::RTConfig config{};
  EXPECT_EQ(config.mode, executive::RTMode::HARD_PERIOD_COMPLETE);
  EXPECT_EQ(config.maxLagTicks, 10U);
}

/** @test RTConfig isHardMode for HARD modes. */
TEST(RTConfig, IsHardModeReturnsTrueForHardModes) {
  executive::RTConfig config{};

  config.mode = executive::RTMode::HARD_TICK_COMPLETE;
  EXPECT_TRUE(config.isHardMode());

  config.mode = executive::RTMode::HARD_PERIOD_COMPLETE;
  EXPECT_TRUE(config.isHardMode());
}

/** @test RTConfig isHardMode returns false for SOFT modes. */
TEST(RTConfig, IsHardModeReturnsFalseForSoftModes) {
  executive::RTConfig config{};

  config.mode = executive::RTMode::SOFT_SKIP_ON_BUSY;
  EXPECT_FALSE(config.isHardMode());

  config.mode = executive::RTMode::SOFT_LAG_TOLERANT;
  EXPECT_FALSE(config.isHardMode());

  config.mode = executive::RTMode::SOFT_LOG_ONLY;
  EXPECT_FALSE(config.isHardMode());
}

/** @test RTConfig isSoftMode for SOFT modes. */
TEST(RTConfig, IsSoftModeReturnsTrueForSoftModes) {
  executive::RTConfig config{};

  config.mode = executive::RTMode::SOFT_SKIP_ON_BUSY;
  EXPECT_TRUE(config.isSoftMode());

  config.mode = executive::RTMode::SOFT_LAG_TOLERANT;
  EXPECT_TRUE(config.isSoftMode());

  config.mode = executive::RTMode::SOFT_LOG_ONLY;
  EXPECT_TRUE(config.isSoftMode());
}

/** @test RTConfig isSoftMode returns false for HARD modes. */
TEST(RTConfig, IsSoftModeReturnsFalseForHardModes) {
  executive::RTConfig config{};

  config.mode = executive::RTMode::HARD_TICK_COMPLETE;
  EXPECT_FALSE(config.isSoftMode());

  config.mode = executive::RTMode::HARD_PERIOD_COMPLETE;
  EXPECT_FALSE(config.isSoftMode());
}

/** @test RTConfig needsDeadlineTracking. */
TEST(RTConfig, NeedsDeadlineTracking) {
  executive::RTConfig config{};

  // Modes that need deadline tracking
  config.mode = executive::RTMode::HARD_PERIOD_COMPLETE;
  EXPECT_TRUE(config.needsDeadlineTracking());

  config.mode = executive::RTMode::SOFT_SKIP_ON_BUSY;
  EXPECT_TRUE(config.needsDeadlineTracking());

  config.mode = executive::RTMode::SOFT_LOG_ONLY;
  EXPECT_TRUE(config.needsDeadlineTracking());

  // Modes that don't need deadline tracking
  config.mode = executive::RTMode::HARD_TICK_COMPLETE;
  EXPECT_FALSE(config.needsDeadlineTracking());

  config.mode = executive::RTMode::SOFT_LAG_TOLERANT;
  EXPECT_FALSE(config.needsDeadlineTracking());
}

/** @test RTConfig methods are constexpr. */
TEST(RTConfig, MethodsAreConstexpr) {
  constexpr executive::RTConfig config{};
  constexpr bool isHard = config.isHardMode();
  constexpr bool isSoft = config.isSoftMode();
  constexpr bool needsTracking = config.needsDeadlineTracking();

  EXPECT_TRUE(isHard);
  EXPECT_FALSE(isSoft);
  EXPECT_TRUE(needsTracking);
}
