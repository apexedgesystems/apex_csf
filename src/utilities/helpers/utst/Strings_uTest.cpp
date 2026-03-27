/**
 * @file Strings_uTest.cpp
 * @brief Unit tests for apex::helpers::strings.
 *
 * Tests string manipulation and parsing utilities.
 *
 * Notes:
 *  - All functions are RT-safe (no allocation).
 *  - Tests verify boundary conditions and edge cases.
 */

#include "src/utilities/helpers/inc/Strings.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

using apex::helpers::strings::copyToBuffer;
using apex::helpers::strings::copyToFixedArray;
using apex::helpers::strings::endsWith;
using apex::helpers::strings::parseIndexFromName;
using apex::helpers::strings::skipWhitespace;
using apex::helpers::strings::startsWith;
using apex::helpers::strings::stripTrailingWhitespace;

/* ----------------------------- copyToFixedArray Tests ----------------------------- */

/** @test Copy string to fixed array. */
TEST(CopyToFixedArrayTest, CopiesString) {
  std::array<char, 16> buf{};
  copyToFixedArray(buf, "hello");
  EXPECT_STREQ(buf.data(), "hello");
}

/** @test Truncates long strings. */
TEST(CopyToFixedArrayTest, TruncatesLongString) {
  std::array<char, 8> buf{};
  copyToFixedArray(buf, "hello world");
  EXPECT_EQ(std::strlen(buf.data()), 7U); // 8 - 1 for null
  EXPECT_EQ(buf[7], '\0');
}

/** @test Handles null source. */
TEST(CopyToFixedArrayTest, NullSourceSetsEmpty) {
  std::array<char, 16> buf{};
  buf[0] = 'x';
  copyToFixedArray(buf, static_cast<const char*>(nullptr));
  EXPECT_EQ(buf[0], '\0');
}

/** @test Copy with explicit length. */
TEST(CopyToFixedArrayTest, CopiesWithLength) {
  std::array<char, 16> buf{};
  copyToFixedArray(buf, "hello world", 5);
  EXPECT_STREQ(buf.data(), "hello");
}

/** @test Copy from std::string. */
TEST(CopyToFixedArrayTest, CopiesFromStdString) {
  std::array<char, 16> buf{};
  const std::string SRC = "test";
  copyToFixedArray(buf, SRC);
  EXPECT_STREQ(buf.data(), "test");
}

/* ----------------------------- makeFixedChar Tests ----------------------------- */

using apex::helpers::strings::makeFixedChar;

/** @test makeFixedChar creates null-terminated array from string_view. */
TEST(MakeFixedCharTest, CreatesNullTerminatedArray) {
  constexpr auto ARR = makeFixedChar<8>("hello");
  EXPECT_STREQ(ARR.data(), "hello");
  EXPECT_EQ(ARR[5], '\0');
}

/** @test makeFixedChar truncates long strings. */
TEST(MakeFixedCharTest, TruncatesLongString) {
  constexpr auto ARR = makeFixedChar<6>("hello world");
  EXPECT_STREQ(ARR.data(), "hello");
  EXPECT_EQ(ARR[5], '\0');
}

/** @test makeFixedChar handles empty string. */
TEST(MakeFixedCharTest, HandlesEmptyString) {
  constexpr auto ARR = makeFixedChar<8>("");
  EXPECT_STREQ(ARR.data(), "");
  EXPECT_EQ(ARR[0], '\0');
}

/** @test makeFixedChar handles exact fit (N-1 chars). */
TEST(MakeFixedCharTest, HandlesExactFit) {
  constexpr auto ARR = makeFixedChar<6>("hello");
  EXPECT_STREQ(ARR.data(), "hello");
  EXPECT_EQ(ARR[5], '\0');
}

/** @test makeFixedChar is constexpr. */
TEST(MakeFixedCharTest, IsConstexpr) {
  static constexpr auto ARR = makeFixedChar<16>("constexpr test");
  static_assert(ARR[0] == 'c', "Should be constexpr");
  EXPECT_STREQ(ARR.data(), "constexpr test");
}

/** @test makeFixedChar zeros remainder of array. */
TEST(MakeFixedCharTest, ZerosRemainder) {
  constexpr auto ARR = makeFixedChar<10>("hi");
  EXPECT_EQ(ARR[0], 'h');
  EXPECT_EQ(ARR[1], 'i');
  EXPECT_EQ(ARR[2], '\0');
  EXPECT_EQ(ARR[3], '\0');
  EXPECT_EQ(ARR[9], '\0');
}

/* ----------------------------- copyToBuffer Tests ----------------------------- */

/** @test Copy to raw buffer. */
TEST(CopyToBufferTest, CopiesString) {
  char buf[16] = {};
  copyToBuffer(buf, sizeof(buf), "hello");
  EXPECT_STREQ(buf, "hello");
}

/** @test Truncates long strings. */
TEST(CopyToBufferTest, TruncatesLongString) {
  char buf[8] = {};
  copyToBuffer(buf, sizeof(buf), "hello world");
  EXPECT_EQ(std::strlen(buf), 7U);
}

/** @test Null destination is safe. */
TEST(CopyToBufferTest, NullDestIsSafe) {
  // Should not crash
  copyToBuffer(nullptr, 16, "hello");
}

/** @test Zero size is safe. */
TEST(CopyToBufferTest, ZeroSizeIsSafe) {
  char buf[16] = {'x'};
  copyToBuffer(buf, 0, "hello");
  EXPECT_EQ(buf[0], 'x'); // Unchanged
}

/* ----------------------------- startsWith Tests ----------------------------- */

/** @test String starts with prefix. */
TEST(StartsWithTest, MatchingPrefix) { EXPECT_TRUE(startsWith("cpu0", "cpu")); }

/** @test String does not start with prefix. */
TEST(StartsWithTest, NonMatchingPrefix) { EXPECT_FALSE(startsWith("mem0", "cpu")); }

/** @test Empty prefix matches any string. */
TEST(StartsWithTest, EmptyPrefixMatches) { EXPECT_TRUE(startsWith("hello", "")); }

/** @test Prefix longer than string returns false. */
TEST(StartsWithTest, PrefixLongerThanString) { EXPECT_FALSE(startsWith("hi", "hello")); }

/** @test Null string returns false. */
TEST(StartsWithTest, NullStringReturnsFalse) { EXPECT_FALSE(startsWith(nullptr, "cpu")); }

/** @test Null prefix returns false. */
TEST(StartsWithTest, NullPrefixReturnsFalse) { EXPECT_FALSE(startsWith("cpu0", nullptr)); }

/* ----------------------------- endsWith Tests ----------------------------- */

/** @test String ends with suffix. */
TEST(EndsWithTest, MatchingSuffix) { EXPECT_TRUE(endsWith("file.log", ".log")); }

/** @test String does not end with suffix. */
TEST(EndsWithTest, NonMatchingSuffix) { EXPECT_FALSE(endsWith("file.txt", ".log")); }

/** @test Empty suffix matches any string. */
TEST(EndsWithTest, EmptySuffixMatches) { EXPECT_TRUE(endsWith("hello", "")); }

/** @test Suffix longer than string returns false. */
TEST(EndsWithTest, SuffixLongerThanString) { EXPECT_FALSE(endsWith("hi", "hello")); }

/** @test Null string returns false. */
TEST(EndsWithTest, NullStringReturnsFalse) { EXPECT_FALSE(endsWith(nullptr, ".log")); }

/** @test Null suffix returns false. */
TEST(EndsWithTest, NullSuffixReturnsFalse) { EXPECT_FALSE(endsWith("file.log", nullptr)); }

/* ----------------------------- parseIndexFromName Tests ----------------------------- */

/** @test Parses index from prefixed name. */
TEST(ParseIndexFromNameTest, ParsesIndex) {
  const std::int32_t IDX = parseIndexFromName("cpu3", "cpu");
  EXPECT_EQ(IDX, 3);
}

/** @test Returns -1 for non-matching prefix. */
TEST(ParseIndexFromNameTest, NonMatchingPrefixReturnsNegative) {
  const std::int32_t IDX = parseIndexFromName("mem0", "cpu");
  EXPECT_EQ(IDX, -1);
}

/** @test Returns -1 for no digits after prefix. */
TEST(ParseIndexFromNameTest, NoDigitsReturnsNegative) {
  const std::int32_t IDX = parseIndexFromName("cpuX", "cpu");
  EXPECT_EQ(IDX, -1);
}

/** @test Parses multi-digit index. */
TEST(ParseIndexFromNameTest, MultiDigitIndex) {
  const std::int32_t IDX = parseIndexFromName("node123", "node");
  EXPECT_EQ(IDX, 123);
}

/** @test Null name returns -1. */
TEST(ParseIndexFromNameTest, NullNameReturnsNegative) {
  const std::int32_t IDX = parseIndexFromName(nullptr, "cpu");
  EXPECT_EQ(IDX, -1);
}

/** @test Null prefix returns -1. */
TEST(ParseIndexFromNameTest, NullPrefixReturnsNegative) {
  const std::int32_t IDX = parseIndexFromName("cpu0", nullptr);
  EXPECT_EQ(IDX, -1);
}

/* ----------------------------- skipWhitespace Tests ----------------------------- */

/** @test Skips leading spaces. */
TEST(SkipWhitespaceTest, SkipsSpaces) {
  const char* RESULT = skipWhitespace("   hello");
  EXPECT_STREQ(RESULT, "hello");
}

/** @test Skips tabs. */
TEST(SkipWhitespaceTest, SkipsTabs) {
  const char* RESULT = skipWhitespace("\t\thello");
  EXPECT_STREQ(RESULT, "hello");
}

/** @test Returns same pointer if no whitespace. */
TEST(SkipWhitespaceTest, NoWhitespace) {
  const char* STR = "hello";
  const char* RESULT = skipWhitespace(STR);
  EXPECT_EQ(RESULT, STR);
}

/** @test Returns pointer to end for all-whitespace string. */
TEST(SkipWhitespaceTest, AllWhitespace) {
  const char* RESULT = skipWhitespace("   ");
  EXPECT_STREQ(RESULT, "");
}

/** @test Null returns null. */
TEST(SkipWhitespaceTest, NullReturnsNull) {
  const char* RESULT = skipWhitespace(nullptr);
  EXPECT_EQ(RESULT, nullptr);
}

/* ----------------------------- stripTrailingWhitespace Tests ----------------------------- */

/** @test Strips trailing newline. */
TEST(StripTrailingWhitespaceTest, StripsNewline) {
  char buf[] = "hello\n";
  std::size_t len = std::strlen(buf);
  stripTrailingWhitespace(buf, len);
  EXPECT_STREQ(buf, "hello");
  EXPECT_EQ(len, 5U);
}

/** @test Strips trailing carriage return and newline. */
TEST(StripTrailingWhitespaceTest, StripsCRLF) {
  char buf[] = "hello\r\n";
  std::size_t len = std::strlen(buf);
  stripTrailingWhitespace(buf, len);
  EXPECT_STREQ(buf, "hello");
  EXPECT_EQ(len, 5U);
}

/** @test Strips trailing spaces. */
TEST(StripTrailingWhitespaceTest, StripsSpaces) {
  char buf[] = "hello   ";
  std::size_t len = std::strlen(buf);
  stripTrailingWhitespace(buf, len);
  EXPECT_STREQ(buf, "hello");
  EXPECT_EQ(len, 5U);
}

/** @test No trailing whitespace leaves string unchanged. */
TEST(StripTrailingWhitespaceTest, NoWhitespace) {
  char buf[] = "hello";
  std::size_t len = std::strlen(buf);
  stripTrailingWhitespace(buf, len);
  EXPECT_STREQ(buf, "hello");
  EXPECT_EQ(len, 5U);
}

/** @test Empty string is handled. */
TEST(StripTrailingWhitespaceTest, EmptyString) {
  char buf[] = "";
  std::size_t len = 0;
  stripTrailingWhitespace(buf, len);
  EXPECT_STREQ(buf, "");
  EXPECT_EQ(len, 0U);
}

/* ----------------------------- Hex Utilities Tests ----------------------------- */

using apex::helpers::strings::fromHexBuffer;
using apex::helpers::strings::hexCharToNibble;
using apex::helpers::strings::isHexDigit;
using apex::helpers::strings::nibbleToHexChar;
using apex::helpers::strings::parseHexU32;
using apex::helpers::strings::parseHexU64;
using apex::helpers::strings::toHexArray;
using apex::helpers::strings::toHexBuffer;

/** @test isHexDigit returns true for valid hex digits. */
TEST(HexUtilitiesTest, IsHexDigit_ValidDigits) {
  EXPECT_TRUE(isHexDigit('0'));
  EXPECT_TRUE(isHexDigit('9'));
  EXPECT_TRUE(isHexDigit('a'));
  EXPECT_TRUE(isHexDigit('f'));
  EXPECT_TRUE(isHexDigit('A'));
  EXPECT_TRUE(isHexDigit('F'));
}

/** @test isHexDigit returns false for invalid characters. */
TEST(HexUtilitiesTest, IsHexDigit_InvalidChars) {
  EXPECT_FALSE(isHexDigit('g'));
  EXPECT_FALSE(isHexDigit('G'));
  EXPECT_FALSE(isHexDigit(' '));
  EXPECT_FALSE(isHexDigit('\0'));
  EXPECT_FALSE(isHexDigit('x'));
}

/** @test hexCharToNibble converts valid hex chars. */
TEST(HexUtilitiesTest, HexCharToNibble_ValidChars) {
  EXPECT_EQ(hexCharToNibble('0'), 0U);
  EXPECT_EQ(hexCharToNibble('9'), 9U);
  EXPECT_EQ(hexCharToNibble('a'), 10U);
  EXPECT_EQ(hexCharToNibble('f'), 15U);
  EXPECT_EQ(hexCharToNibble('A'), 10U);
  EXPECT_EQ(hexCharToNibble('F'), 15U);
}

/** @test hexCharToNibble returns 0xFF for invalid chars. */
TEST(HexUtilitiesTest, HexCharToNibble_InvalidChars) {
  EXPECT_EQ(hexCharToNibble('g'), 0xFFU);
  EXPECT_EQ(hexCharToNibble(' '), 0xFFU);
}

/** @test nibbleToHexChar converts values 0-15. */
TEST(HexUtilitiesTest, NibbleToHexChar_ValidValues) {
  EXPECT_EQ(nibbleToHexChar(0), '0');
  EXPECT_EQ(nibbleToHexChar(9), '9');
  EXPECT_EQ(nibbleToHexChar(10), 'a');
  EXPECT_EQ(nibbleToHexChar(15), 'f');
}

/** @test nibbleToHexChar returns '?' for invalid values. */
TEST(HexUtilitiesTest, NibbleToHexChar_InvalidValue) { EXPECT_EQ(nibbleToHexChar(16), '?'); }

/** @test parseHexU32 parses simple hex string. */
TEST(HexUtilitiesTest, ParseHexU32_Simple) {
  std::uint32_t value = 0;
  const std::size_t DIGITS = parseHexU32("1a2b", value);
  EXPECT_EQ(DIGITS, 4U);
  EXPECT_EQ(value, 0x1A2BU);
}

/** @test parseHexU32 handles 0x prefix. */
TEST(HexUtilitiesTest, ParseHexU32_WithPrefix) {
  std::uint32_t value = 0;
  const std::size_t DIGITS = parseHexU32("0x1a2b", value);
  EXPECT_EQ(DIGITS, 4U);
  EXPECT_EQ(value, 0x1A2BU);
}

/** @test parseHexU32 stops at non-hex character. */
TEST(HexUtilitiesTest, ParseHexU32_StopsAtNonHex) {
  std::uint32_t value = 0;
  const std::size_t DIGITS = parseHexU32("1a2bxyz", value);
  EXPECT_EQ(DIGITS, 4U);
  EXPECT_EQ(value, 0x1A2BU);
}

/** @test parseHexU32 returns 0 for null input. */
TEST(HexUtilitiesTest, ParseHexU32_NullInput) {
  std::uint32_t value = 42;
  const std::size_t DIGITS = parseHexU32(nullptr, value);
  EXPECT_EQ(DIGITS, 0U);
  EXPECT_EQ(value, 0U);
}

/** @test parseHexU64 parses 64-bit value. */
TEST(HexUtilitiesTest, ParseHexU64_LargeValue) {
  std::uint64_t value = 0;
  const std::size_t DIGITS = parseHexU64("0xDEADBEEFCAFE", value);
  EXPECT_EQ(DIGITS, 12U);
  EXPECT_EQ(value, 0xDEADBEEFCAFEULL);
}

/** @test toHexBuffer converts bytes to hex. */
TEST(HexUtilitiesTest, ToHexBuffer_Simple) {
  const std::uint8_t DATA[] = {0xDE, 0xAD, 0xBE, 0xEF};
  char buf[16] = {};
  const std::size_t LEN = toHexBuffer(DATA, 4, buf, sizeof(buf));
  EXPECT_EQ(LEN, 8U);
  EXPECT_STREQ(buf, "deadbeef");
}

/** @test toHexBuffer truncates when buffer too small. */
TEST(HexUtilitiesTest, ToHexBuffer_Truncates) {
  const std::uint8_t DATA[] = {0xDE, 0xAD, 0xBE, 0xEF};
  char buf[5] = {};
  const std::size_t LEN = toHexBuffer(DATA, 4, buf, sizeof(buf));
  EXPECT_EQ(LEN, 4U);
  EXPECT_STREQ(buf, "dead");
}

/** @test toHexBuffer handles null buffer. */
TEST(HexUtilitiesTest, ToHexBuffer_NullBuffer) {
  const std::uint8_t DATA[] = {0xDE, 0xAD};
  const std::size_t LEN = toHexBuffer(DATA, 2, nullptr, 16);
  EXPECT_EQ(LEN, 0U);
}

/** @test toHexArray template version. */
TEST(HexUtilitiesTest, ToHexArray_Simple) {
  const std::uint8_t DATA[] = {0x01, 0x02, 0x03};
  std::array<char, 16> buf{};
  const std::size_t LEN = toHexArray(DATA, 3, buf);
  EXPECT_EQ(LEN, 6U);
  EXPECT_STREQ(buf.data(), "010203");
}

/** @test fromHexBuffer parses hex string to bytes. */
TEST(HexUtilitiesTest, FromHexBuffer_Simple) {
  std::uint8_t buf[4] = {};
  const std::size_t BYTES = fromHexBuffer("deadbeef", buf, sizeof(buf));
  EXPECT_EQ(BYTES, 4U);
  EXPECT_EQ(buf[0], 0xDE);
  EXPECT_EQ(buf[1], 0xAD);
  EXPECT_EQ(buf[2], 0xBE);
  EXPECT_EQ(buf[3], 0xEF);
}

/** @test fromHexBuffer handles 0x prefix. */
TEST(HexUtilitiesTest, FromHexBuffer_WithPrefix) {
  std::uint8_t buf[2] = {};
  const std::size_t BYTES = fromHexBuffer("0x1a2b", buf, sizeof(buf));
  EXPECT_EQ(BYTES, 2U);
  EXPECT_EQ(buf[0], 0x1A);
  EXPECT_EQ(buf[1], 0x2B);
}

/** @test fromHexBuffer stops at odd character. */
TEST(HexUtilitiesTest, FromHexBuffer_OddLength) {
  std::uint8_t buf[4] = {};
  const std::size_t BYTES = fromHexBuffer("abc", buf, sizeof(buf));
  EXPECT_EQ(BYTES, 1U);
  EXPECT_EQ(buf[0], 0xAB);
}

/** @test fromHexBuffer returns 0 for null input. */
TEST(HexUtilitiesTest, FromHexBuffer_NullInput) {
  std::uint8_t buf[4] = {};
  const std::size_t BYTES = fromHexBuffer(nullptr, buf, sizeof(buf));
  EXPECT_EQ(BYTES, 0U);
}
