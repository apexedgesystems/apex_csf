/**
 * @file toBytes_uTest.cpp
 * @brief Unit tests for apex::helpers::bytes::toBytes (C++17-safe).
 *
 * Coverage:
 *  - Integral types
 *  - Floating-point types
 *  - Trivially copyable struct
 *  - Char array (with null terminator)
 *  - 1-byte edge case
 *
 * Notes:
 *  - Endian-agnostic: compares against a memcpy-based ground truth.
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

using ::testing::Eq;

namespace { // Helpers ---------------------------------------------------------

template <typename T> std::array<std::uint8_t, sizeof(T)> makeBytes(const T& v) {
  std::array<std::uint8_t, sizeof(T)> out{};
  std::memcpy(out.data(), &v, sizeof(T));
  return out;
}

} // namespace

// Fixture ---------------------------------------------------------------------

class ToBytesTest : public ::testing::Test {};

// -----------------------------------------------------------------------------

/**
 * @test Integral types serialize to raw bytes equal to memcpy reference.
 */
TEST_F(ToBytesTest, IntegralTypes) {
  // Arrange
  std::int32_t intVal = 0x12345678;

  // Act
  auto bytes = apex::helpers::bytes::toBytes(intVal);
  auto expected = makeBytes(intVal);

  // Assert
  ASSERT_EQ(bytes.size(), sizeof(intVal));
  EXPECT_THAT(bytes, Eq(expected));
}

/**
 * @test Floating-point types serialize to raw bytes equal to memcpy reference.
 */
TEST_F(ToBytesTest, FloatingPointTypes) {
  // Arrange
  float floatVal = 1.5f;

  // Act
  auto bytes = apex::helpers::bytes::toBytes(floatVal);
  auto expected = makeBytes(floatVal);

  // Assert
  ASSERT_EQ(bytes.size(), sizeof(floatVal));
  EXPECT_THAT(bytes, Eq(expected));
}

/**
 * @test Trivially copyable struct serializes to raw bytes equal to memcpy reference.
 */
TEST_F(ToBytesTest, StructType) {
  // Arrange
  struct TrivialStruct {
    std::uint16_t a;
    std::uint16_t b;
  };
  TrivialStruct s{0x1234u, 0xABCDu};

  // Act
  auto bytes = apex::helpers::bytes::toBytes(s);
  auto expected = makeBytes(s);

  // Assert
  ASSERT_EQ(bytes.size(), sizeof(s));
  EXPECT_THAT(bytes, Eq(expected));
}

/**
 * @test Char array (including null terminator) serializes as expected.
 */
TEST_F(ToBytesTest, CharArray) {
  // Arrange
  std::array<char, 3> text = {'A', 'B', '\0'}; // size == 3 including '\0'

  // Act
  auto bytes = apex::helpers::bytes::toBytes(text);
  auto expected = makeBytes(text);

  // Assert
  ASSERT_EQ(bytes.size(), sizeof(text));
  EXPECT_THAT(bytes, Eq(expected));
}

/**
 * @test One-byte edge case serializes trivially.
 */
TEST_F(ToBytesTest, EdgeCase_OneByte) {
  // Arrange
  std::uint8_t byteVal = 0xFFu;

  // Act
  auto bytes = apex::helpers::bytes::toBytes(byteVal);
  auto expected = makeBytes(byteVal);

  // Assert
  ASSERT_EQ(bytes.size(), sizeof(byteVal));
  EXPECT_THAT(bytes, Eq(expected));
}
