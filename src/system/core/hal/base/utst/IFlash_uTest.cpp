/**
 * @file IFlash_uTest.cpp
 * @brief Unit tests for IFlash interface types (enums, structs).
 *
 * Tests the platform-agnostic flash types defined in IFlash.hpp.
 * No hardware or mock needed -- these test pure value types.
 */

#include "src/system/core/hal/base/IFlash.hpp"

#include <gtest/gtest.h>

using apex::hal::FlashGeometry;
using apex::hal::FlashStats;
using apex::hal::FlashStatus;

/* ----------------------------- FlashStatus Tests ----------------------------- */

/** @test Verify FlashStatus::OK is zero. */
TEST(FlashStatus, OkIsZero) { EXPECT_EQ(static_cast<int>(FlashStatus::OK), 0); }

/** @test Verify toString returns non-null for all values. */
TEST(FlashStatus, ToStringNonNull) {
  EXPECT_NE(toString(FlashStatus::OK), nullptr);
  EXPECT_NE(toString(FlashStatus::BUSY), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_TIMEOUT), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_NOT_INIT), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_INVALID_ARG), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_ALIGNMENT), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_WRITE_PROTECTED), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_ERASE_FAILED), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_PROGRAM_FAILED), nullptr);
  EXPECT_NE(toString(FlashStatus::ERROR_VERIFY_FAILED), nullptr);
}

/** @test Verify toString returns UNKNOWN for invalid value. */
TEST(FlashStatus, ToStringUnknown) {
  const char* RESULT = toString(static_cast<FlashStatus>(255));
  EXPECT_STREQ(RESULT, "UNKNOWN");
}

/** @test Verify toString returns expected strings. */
TEST(FlashStatus, ToStringValues) {
  EXPECT_STREQ(toString(FlashStatus::OK), "OK");
  EXPECT_STREQ(toString(FlashStatus::BUSY), "BUSY");
  EXPECT_STREQ(toString(FlashStatus::ERROR_TIMEOUT), "ERROR_TIMEOUT");
  EXPECT_STREQ(toString(FlashStatus::ERROR_NOT_INIT), "ERROR_NOT_INIT");
  EXPECT_STREQ(toString(FlashStatus::ERROR_INVALID_ARG), "ERROR_INVALID_ARG");
  EXPECT_STREQ(toString(FlashStatus::ERROR_ALIGNMENT), "ERROR_ALIGNMENT");
  EXPECT_STREQ(toString(FlashStatus::ERROR_WRITE_PROTECTED), "ERROR_WRITE_PROTECTED");
  EXPECT_STREQ(toString(FlashStatus::ERROR_ERASE_FAILED), "ERROR_ERASE_FAILED");
  EXPECT_STREQ(toString(FlashStatus::ERROR_PROGRAM_FAILED), "ERROR_PROGRAM_FAILED");
  EXPECT_STREQ(toString(FlashStatus::ERROR_VERIFY_FAILED), "ERROR_VERIFY_FAILED");
}

/* ----------------------------- FlashGeometry Tests ----------------------------- */

/** @test Verify FlashGeometry defaults to zero. */
TEST(FlashGeometry, DefaultValues) {
  FlashGeometry geo;

  EXPECT_EQ(geo.baseAddress, 0U);
  EXPECT_EQ(geo.totalSize, 0U);
  EXPECT_EQ(geo.pageSize, 0U);
  EXPECT_EQ(geo.writeAlignment, 0U);
  EXPECT_EQ(geo.pageCount, 0U);
  EXPECT_EQ(geo.bankCount, 0U);
}

/** @test Verify FlashGeometry is copyable. */
TEST(FlashGeometry, Copyable) {
  FlashGeometry geo;
  geo.baseAddress = 0x08000000;
  geo.totalSize = 1048576;
  geo.pageSize = 2048;
  geo.writeAlignment = 8;
  geo.pageCount = 512;
  geo.bankCount = 2;

  const FlashGeometry COPY = geo;

  EXPECT_EQ(COPY.baseAddress, 0x08000000U);
  EXPECT_EQ(COPY.totalSize, 1048576U);
  EXPECT_EQ(COPY.pageSize, 2048U);
  EXPECT_EQ(COPY.writeAlignment, 8U);
  EXPECT_EQ(COPY.pageCount, 512U);
  EXPECT_EQ(COPY.bankCount, 2U);
}

/* ----------------------------- FlashStats Tests ----------------------------- */

/** @test Verify FlashStats defaults to zero. */
TEST(FlashStats, DefaultZero) {
  FlashStats stats;

  EXPECT_EQ(stats.bytesWritten, 0U);
  EXPECT_EQ(stats.bytesRead, 0U);
  EXPECT_EQ(stats.pagesErased, 0U);
  EXPECT_EQ(stats.writeErrors, 0U);
  EXPECT_EQ(stats.eraseErrors, 0U);
  EXPECT_EQ(stats.readErrors, 0U);
}

/** @test Verify reset clears all counters. */
TEST(FlashStats, Reset) {
  FlashStats stats;
  stats.bytesWritten = 100;
  stats.bytesRead = 200;
  stats.pagesErased = 5;
  stats.writeErrors = 1;
  stats.eraseErrors = 2;
  stats.readErrors = 3;

  stats.reset();

  EXPECT_EQ(stats.bytesWritten, 0U);
  EXPECT_EQ(stats.bytesRead, 0U);
  EXPECT_EQ(stats.pagesErased, 0U);
  EXPECT_EQ(stats.writeErrors, 0U);
  EXPECT_EQ(stats.eraseErrors, 0U);
  EXPECT_EQ(stats.readErrors, 0U);
}

/** @test Verify totalErrors sums error counters. */
TEST(FlashStats, TotalErrors) {
  FlashStats stats;
  stats.writeErrors = 3;
  stats.eraseErrors = 2;
  stats.readErrors = 1;

  EXPECT_EQ(stats.totalErrors(), 6U);
}

/** @test Verify totalErrors returns zero when no errors. */
TEST(FlashStats, TotalErrorsZero) {
  FlashStats stats;

  EXPECT_EQ(stats.totalErrors(), 0U);
}

/** @test Verify totalErrors excludes non-error counters. */
TEST(FlashStats, TotalErrorsExcludesNonErrors) {
  FlashStats stats;
  stats.bytesWritten = 1000;
  stats.bytesRead = 2000;
  stats.pagesErased = 50;

  EXPECT_EQ(stats.totalErrors(), 0U);
}
