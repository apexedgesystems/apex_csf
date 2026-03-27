/**
 * @file Format_uTest.cpp
 * @brief Unit tests for apex::helpers::format.
 *
 * Tests human-readable formatting utilities.
 *
 * Notes:
 *  - Tests verify formatting patterns, not exact strings.
 *  - All functions are cold-path (allocate std::string).
 */

#include "src/utilities/helpers/inc/Format.hpp"

#include <gtest/gtest.h>

#include <string>

using apex::helpers::format::bytesBinary;
using apex::helpers::format::bytesDecimal;
using apex::helpers::format::count;
using apex::helpers::format::frequencyHz;

/* ----------------------------- bytesBinary Tests ----------------------------- */

/** @test Zero bytes formats as "0 B". */
TEST(BytesBinaryTest, ZeroBytes) {
  const std::string RESULT = bytesBinary(0);
  EXPECT_EQ(RESULT, "0 B");
}

/** @test Small values format in bytes. */
TEST(BytesBinaryTest, SmallBytes) {
  const std::string RESULT = bytesBinary(512);
  EXPECT_EQ(RESULT, "512 B");
}

/** @test KiB formatting. */
TEST(BytesBinaryTest, KibiBytes) {
  const std::string RESULT = bytesBinary(1536); // 1.5 KiB
  EXPECT_EQ(RESULT, "1.5 KiB");
}

/** @test MiB formatting. */
TEST(BytesBinaryTest, MebiBytes) {
  const std::string RESULT = bytesBinary(1572864); // 1.5 MiB
  EXPECT_EQ(RESULT, "1.5 MiB");
}

/** @test GiB formatting. */
TEST(BytesBinaryTest, GibiBytes) {
  const std::string RESULT = bytesBinary(1610612736ULL); // 1.5 GiB
  EXPECT_EQ(RESULT, "1.5 GiB");
}

/** @test TiB formatting. */
TEST(BytesBinaryTest, TebiBytes) {
  const std::string RESULT = bytesBinary(1649267441664ULL); // 1.5 TiB
  EXPECT_EQ(RESULT, "1.5 TiB");
}

/* ----------------------------- bytesDecimal Tests ----------------------------- */

/** @test Zero bytes formats as "0 B". */
TEST(BytesDecimalTest, ZeroBytes) {
  const std::string RESULT = bytesDecimal(0);
  EXPECT_EQ(RESULT, "0 B");
}

/** @test Small values format in bytes. */
TEST(BytesDecimalTest, SmallBytes) {
  const std::string RESULT = bytesDecimal(500);
  EXPECT_EQ(RESULT, "500 B");
}

/** @test KB formatting. */
TEST(BytesDecimalTest, KiloBytes) {
  const std::string RESULT = bytesDecimal(1500); // 1.5 KB
  EXPECT_EQ(RESULT, "1.5 KB");
}

/** @test MB formatting. */
TEST(BytesDecimalTest, MegaBytes) {
  const std::string RESULT = bytesDecimal(1500000); // 1.5 MB
  EXPECT_EQ(RESULT, "1.5 MB");
}

/** @test GB formatting. */
TEST(BytesDecimalTest, GigaBytes) {
  const std::string RESULT = bytesDecimal(1500000000ULL); // 1.5 GB
  EXPECT_EQ(RESULT, "1.5 GB");
}

/** @test TB formatting. */
TEST(BytesDecimalTest, TeraBytes) {
  const std::string RESULT = bytesDecimal(1500000000000ULL); // 1.5 TB
  EXPECT_EQ(RESULT, "1.5 TB");
}

/* ----------------------------- frequencyHz Tests ----------------------------- */

/** @test Zero Hz formats as "0 Hz". */
TEST(FrequencyHzTest, ZeroHz) {
  const std::string RESULT = frequencyHz(0);
  EXPECT_EQ(RESULT, "0 Hz");
}

/** @test Small frequencies format in Hz. */
TEST(FrequencyHzTest, SmallHz) {
  const std::string RESULT = frequencyHz(500);
  EXPECT_EQ(RESULT, "500 Hz");
}

/** @test kHz formatting. */
TEST(FrequencyHzTest, KiloHz) {
  const std::string RESULT = frequencyHz(1500); // 1.5 kHz
  EXPECT_EQ(RESULT, "1.5 kHz");
}

/** @test MHz formatting. */
TEST(FrequencyHzTest, MegaHz) {
  const std::string RESULT = frequencyHz(1500000); // 1.5 MHz
  EXPECT_EQ(RESULT, "1.5 MHz");
}

/** @test GHz formatting. */
TEST(FrequencyHzTest, GigaHz) {
  const std::string RESULT = frequencyHz(2400000000ULL); // 2.4 GHz
  EXPECT_EQ(RESULT, "2.40 GHz");
}

/* ----------------------------- count Tests ----------------------------- */

/** @test Zero count formats as "0". */
TEST(CountTest, ZeroCount) {
  const std::string RESULT = count(0);
  EXPECT_EQ(RESULT, "0");
}

/** @test Small counts format as plain numbers. */
TEST(CountTest, SmallCount) {
  const std::string RESULT = count(500);
  EXPECT_EQ(RESULT, "500");
}

/** @test Thousands format with K suffix. */
TEST(CountTest, ThousandCount) {
  const std::string RESULT = count(1500); // 1.5K
  EXPECT_EQ(RESULT, "1.5K");
}

/** @test Millions format with M suffix. */
TEST(CountTest, MillionCount) {
  const std::string RESULT = count(1500000); // 1.5M
  EXPECT_EQ(RESULT, "1.5M");
}

/** @test Billions format with B suffix. */
TEST(CountTest, BillionCount) {
  const std::string RESULT = count(1500000000ULL); // 1.5B
  EXPECT_EQ(RESULT, "1.5B");
}
