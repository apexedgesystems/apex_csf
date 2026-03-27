/**
 * @file DataTarget_uTest.cpp
 * @brief Unit tests for DataTarget struct and helper functions.
 */

#include "src/system/core/infrastructure/data/inc/DataTarget.hpp"

#include <gtest/gtest.h>

using system_core::data::DataCategory;
using system_core::data::DataTarget;
using system_core::data::effectiveLen;
using system_core::data::isInBounds;
using system_core::data::isWholeBlock;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed DataTarget has zero fields. */
TEST(DataTarget, DefaultConstruction) {
  const DataTarget T{};
  EXPECT_EQ(T.fullUid, 0U);
  EXPECT_EQ(T.category, DataCategory::STATIC_PARAM);
  EXPECT_EQ(T.byteOffset, 0U);
  EXPECT_EQ(T.byteLen, 0U);
}

/** @test DataTarget size is 12 bytes (aligned). */
TEST(DataTarget, Size) { EXPECT_EQ(sizeof(DataTarget), 12U); }

/* ----------------------------- isWholeBlock ----------------------------- */

/** @test isWholeBlock returns true when byteLen is 0. */
TEST(DataTarget, IsWholeBlockTrue) {
  const DataTarget T{0x007800, DataCategory::STATE, 0, 0};
  EXPECT_TRUE(isWholeBlock(T));
}

/** @test isWholeBlock returns false when byteLen is non-zero. */
TEST(DataTarget, IsWholeBlockFalse) {
  const DataTarget T{0x007800, DataCategory::STATE, 4, 8};
  EXPECT_FALSE(isWholeBlock(T));
}

/* ----------------------------- isInBounds ----------------------------- */

/** @test Whole-block target with zero offset is in bounds. */
TEST(DataTarget, InBoundsWholeBlock) {
  const DataTarget T{0x007800, DataCategory::OUTPUT, 0, 0};
  EXPECT_TRUE(isInBounds(T, 52));
}

/** @test Whole-block target with non-zero offset is out of bounds. */
TEST(DataTarget, OutOfBoundsWholeBlockNonZeroOffset) {
  const DataTarget T{0x007800, DataCategory::OUTPUT, 4, 0};
  EXPECT_FALSE(isInBounds(T, 52));
}

/** @test Partial target within block is in bounds. */
TEST(DataTarget, InBoundsPartial) {
  const DataTarget T{0x007800, DataCategory::OUTPUT, 36, 4};
  EXPECT_TRUE(isInBounds(T, 52));
}

/** @test Partial target at exact end of block is in bounds. */
TEST(DataTarget, InBoundsExactEnd) {
  const DataTarget T{0x007800, DataCategory::OUTPUT, 48, 4};
  EXPECT_TRUE(isInBounds(T, 52));
}

/** @test Partial target extending past block is out of bounds. */
TEST(DataTarget, OutOfBoundsPartial) {
  const DataTarget T{0x007800, DataCategory::OUTPUT, 50, 4};
  EXPECT_FALSE(isInBounds(T, 52));
}

/** @test Zero-length block always out of bounds for partial target. */
TEST(DataTarget, OutOfBoundsZeroBlock) {
  const DataTarget T{0x007800, DataCategory::OUTPUT, 0, 4};
  EXPECT_FALSE(isInBounds(T, 0));
}

/* ----------------------------- effectiveLen ----------------------------- */

/** @test effectiveLen returns blockSize for whole-block target. */
TEST(DataTarget, EffectiveLenWholeBlock) {
  const DataTarget T{0x007800, DataCategory::STATE, 0, 0};
  EXPECT_EQ(effectiveLen(T, 40), 40U);
}

/** @test effectiveLen returns byteLen for partial target. */
TEST(DataTarget, EffectiveLenPartial) {
  const DataTarget T{0x007800, DataCategory::STATE, 8, 16};
  EXPECT_EQ(effectiveLen(T, 40), 16U);
}

/* ----------------------------- Equality ----------------------------- */

/** @test Equal targets compare as equal. */
TEST(DataTarget, Equality) {
  const DataTarget A{0x007800, DataCategory::OUTPUT, 36, 4};
  const DataTarget B{0x007800, DataCategory::OUTPUT, 36, 4};
  EXPECT_EQ(A, B);
}

/** @test Different fullUid targets compare as not equal. */
TEST(DataTarget, InequalityFullUid) {
  const DataTarget A{0x007800, DataCategory::OUTPUT, 36, 4};
  const DataTarget B{0x007A00, DataCategory::OUTPUT, 36, 4};
  EXPECT_NE(A, B);
}

/** @test Different category targets compare as not equal. */
TEST(DataTarget, InequalityCategory) {
  const DataTarget A{0x007800, DataCategory::OUTPUT, 36, 4};
  const DataTarget B{0x007800, DataCategory::STATE, 36, 4};
  EXPECT_NE(A, B);
}

/** @test Different offset targets compare as not equal. */
TEST(DataTarget, InequalityOffset) {
  const DataTarget A{0x007800, DataCategory::OUTPUT, 36, 4};
  const DataTarget B{0x007800, DataCategory::OUTPUT, 40, 4};
  EXPECT_NE(A, B);
}

/** @test Different byteLen targets compare as not equal. */
TEST(DataTarget, InequalityLen) {
  const DataTarget A{0x007800, DataCategory::OUTPUT, 36, 4};
  const DataTarget B{0x007800, DataCategory::OUTPUT, 36, 8};
  EXPECT_NE(A, B);
}
