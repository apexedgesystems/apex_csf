/**
 * @file DataCategory_uTest.cpp
 * @brief Unit tests for DataCategory enum and helper functions.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"

#include <gtest/gtest.h>

using system_core::data::DataCategory;
using system_core::data::isModelInput;
using system_core::data::isModelOutput;
using system_core::data::isParam;
using system_core::data::isReadOnly;
using system_core::data::toString;

/* ----------------------------- Enum Values ----------------------------- */

/** @test DataCategory enum has expected values. */
TEST(DataCategory, EnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(DataCategory::STATIC_PARAM), 0);
  EXPECT_EQ(static_cast<std::uint8_t>(DataCategory::TUNABLE_PARAM), 1);
  EXPECT_EQ(static_cast<std::uint8_t>(DataCategory::STATE), 2);
  EXPECT_EQ(static_cast<std::uint8_t>(DataCategory::INPUT), 3);
  EXPECT_EQ(static_cast<std::uint8_t>(DataCategory::OUTPUT), 4);
}

/* ----------------------------- toString ----------------------------- */

/** @test toString returns correct strings for all categories. */
TEST(DataCategory, ToStringReturnsExpected) {
  EXPECT_STREQ(toString(DataCategory::STATIC_PARAM), "STATIC_PARAM");
  EXPECT_STREQ(toString(DataCategory::TUNABLE_PARAM), "TUNABLE_PARAM");
  EXPECT_STREQ(toString(DataCategory::STATE), "STATE");
  EXPECT_STREQ(toString(DataCategory::INPUT), "INPUT");
  EXPECT_STREQ(toString(DataCategory::OUTPUT), "OUTPUT");
}

/** @test toString returns UNKNOWN for invalid values. */
TEST(DataCategory, ToStringUnknown) {
  auto invalid = static_cast<DataCategory>(99);
  EXPECT_STREQ(toString(invalid), "UNKNOWN");
}

/* ----------------------------- isParam ----------------------------- */

/** @test isParam returns true for parameter categories. */
TEST(DataCategory, IsParamTrue) {
  EXPECT_TRUE(isParam(DataCategory::STATIC_PARAM));
  EXPECT_TRUE(isParam(DataCategory::TUNABLE_PARAM));
}

/** @test isParam returns false for non-parameter categories. */
TEST(DataCategory, IsParamFalse) {
  EXPECT_FALSE(isParam(DataCategory::STATE));
  EXPECT_FALSE(isParam(DataCategory::INPUT));
  EXPECT_FALSE(isParam(DataCategory::OUTPUT));
}

/* ----------------------------- isReadOnly ----------------------------- */

/** @test isReadOnly returns true only for STATIC_PARAM. */
TEST(DataCategory, IsReadOnlyTrue) { EXPECT_TRUE(isReadOnly(DataCategory::STATIC_PARAM)); }

/** @test isReadOnly returns false for mutable categories. */
TEST(DataCategory, IsReadOnlyFalse) {
  EXPECT_FALSE(isReadOnly(DataCategory::TUNABLE_PARAM));
  EXPECT_FALSE(isReadOnly(DataCategory::STATE));
  EXPECT_FALSE(isReadOnly(DataCategory::INPUT));
  EXPECT_FALSE(isReadOnly(DataCategory::OUTPUT));
}

/* ----------------------------- isModelInput ----------------------------- */

/** @test isModelInput returns true for input-like categories. */
TEST(DataCategory, IsModelInputTrue) {
  EXPECT_TRUE(isModelInput(DataCategory::STATIC_PARAM));
  EXPECT_TRUE(isModelInput(DataCategory::TUNABLE_PARAM));
  EXPECT_TRUE(isModelInput(DataCategory::INPUT));
}

/** @test isModelInput returns false for non-input categories. */
TEST(DataCategory, IsModelInputFalse) {
  EXPECT_FALSE(isModelInput(DataCategory::STATE));
  EXPECT_FALSE(isModelInput(DataCategory::OUTPUT));
}

/* ----------------------------- isModelOutput ----------------------------- */

/** @test isModelOutput returns true only for OUTPUT. */
TEST(DataCategory, IsModelOutputTrue) { EXPECT_TRUE(isModelOutput(DataCategory::OUTPUT)); }

/** @test isModelOutput returns false for non-output categories. */
TEST(DataCategory, IsModelOutputFalse) {
  EXPECT_FALSE(isModelOutput(DataCategory::STATIC_PARAM));
  EXPECT_FALSE(isModelOutput(DataCategory::TUNABLE_PARAM));
  EXPECT_FALSE(isModelOutput(DataCategory::STATE));
  EXPECT_FALSE(isModelOutput(DataCategory::INPUT));
}
