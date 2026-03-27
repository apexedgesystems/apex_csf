/**
 * @file ByteOps_uTest.cpp
 * @brief Unit tests for apex::helpers::byte_ops (extract*, load*, store*).
 *
 * Coverage:
 *  - extractLe / extractBe for 16/32-bit values
 *  - loadLe / loadBe from explicit byte patterns (endianness-agnostic)
 *  - storeLe / storeBe produce explicit byte patterns
 *  - Round-trip: storeLe -> loadLe and storeBe -> loadBe for 16/64-bit values
 *  - 8-bit edge case behaves identically for LE/BE
 */

#include "src/utilities/helpers/inc/Bytes.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using ::testing::Eq;

namespace { // Helpers ---------------------------------------------------------

template <typename T> std::array<std::uint8_t, sizeof(T)> makeLeBytes(T v) {
  std::array<std::uint8_t, sizeof(T)> out{};
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    out[i] =
        static_cast<std::uint8_t>((static_cast<std::make_unsigned_t<T>>(v) >> (i * 8U)) & 0xFFU);
  }
  return out;
}

template <typename T> std::array<std::uint8_t, sizeof(T)> makeBeBytes(T v) {
  std::array<std::uint8_t, sizeof(T)> out{};
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    const std::size_t SHIFT = (sizeof(T) - 1U - i) * 8U; // const local -> UPPER_CASE
    out[i] = static_cast<std::uint8_t>((static_cast<std::make_unsigned_t<T>>(v) >> SHIFT) & 0xFFU);
  }
  return out;
}

} // namespace

// Fixture ---------------------------------------------------------------------

class ByteOpsTest : public ::testing::Test {};

// -----------------------------------------------------------------------------

/**
 * @test extractLe extracts the correct byte for 0x12345678.
 */
TEST_F(ByteOpsTest, ExtractLe32) {
  // Arrange
  const std::uint32_t VALUE = 0x12345678u;

  // Act & Assert (index 0 = LSB)
  EXPECT_EQ(apex::helpers::bytes::extractLe(VALUE, 0U), 0x78U);
  EXPECT_EQ(apex::helpers::bytes::extractLe(VALUE, 1U), 0x56U);
  EXPECT_EQ(apex::helpers::bytes::extractLe(VALUE, 2U), 0x34U);
  EXPECT_EQ(apex::helpers::bytes::extractLe(VALUE, 3U), 0x12U);
}

/**
 * @test extractBe extracts the correct byte for 0x12345678.
 */
TEST_F(ByteOpsTest, ExtractBe32) {
  // Arrange
  const std::uint32_t VALUE = 0x12345678u;

  // Act & Assert (index 0 = MSB)
  EXPECT_EQ(apex::helpers::bytes::extractBe(VALUE, 0U), 0x12U);
  EXPECT_EQ(apex::helpers::bytes::extractBe(VALUE, 1U), 0x34U);
  EXPECT_EQ(apex::helpers::bytes::extractBe(VALUE, 2U), 0x56U);
  EXPECT_EQ(apex::helpers::bytes::extractBe(VALUE, 3U), 0x78U);
}

/**
 * @test loadLe reads little-endian bytes into the expected 32-bit value.
 */
TEST_F(ByteOpsTest, LoadLe32) {
  // Arrange
  const std::uint32_t EXPECTED = 0x12345678u;
  const auto BYTES = makeLeBytes(EXPECTED);

  // Act
  const std::uint32_t LOADED = apex::helpers::bytes::loadLe<std::uint32_t>(BYTES.data());

  // Assert
  EXPECT_EQ(LOADED, EXPECTED);
}

/**
 * @test loadBe reads big-endian bytes into the expected 32-bit value.
 */
TEST_F(ByteOpsTest, LoadBe32) {
  // Arrange
  const std::uint32_t EXPECTED = 0x12345678u;
  const auto BYTES = makeBeBytes(EXPECTED);

  // Act
  const std::uint32_t LOADED = apex::helpers::bytes::loadBe<std::uint32_t>(BYTES.data());

  // Assert
  EXPECT_EQ(LOADED, EXPECTED);
}

/**
 * @test storeLe writes the expected little-endian byte pattern for 32-bit value.
 */
TEST_F(ByteOpsTest, StoreLe32) {
  // Arrange
  const std::uint32_t VALUE = 0x12345678u;
  std::array<std::uint8_t, sizeof(VALUE)> bytes{};
  const auto EXPECTED = makeLeBytes(VALUE);

  // Act
  apex::helpers::bytes::storeLe(VALUE, bytes.data());

  // Assert
  EXPECT_THAT(bytes, Eq(EXPECTED));
}

/**
 * @test storeBe writes the expected big-endian byte pattern for 32-bit value.
 */
TEST_F(ByteOpsTest, StoreBe32) {
  // Arrange
  const std::uint32_t VALUE = 0x12345678u;
  std::array<std::uint8_t, sizeof(VALUE)> bytes{};
  const auto EXPECTED = makeBeBytes(VALUE);

  // Act
  apex::helpers::bytes::storeBe(VALUE, bytes.data());

  // Assert
  EXPECT_THAT(bytes, Eq(EXPECTED));
}

/**
 * @test Round-trip LE: storeLe then loadLe returns the original 16-bit and 64-bit values.
 */
TEST_F(ByteOpsTest, RoundTripLe_16_64) {
  // Arrange
  const std::uint16_t V16 = 0xBEEFu;
  const std::uint64_t V64 = 0x0123'4567'89AB'CDEFull;

  std::array<std::uint8_t, sizeof(V16)> b16{};
  std::array<std::uint8_t, sizeof(V64)> b64{};

  // Act
  apex::helpers::bytes::storeLe(V16, b16.data());
  apex::helpers::bytes::storeLe(V64, b64.data());

  const std::uint16_t R16 = apex::helpers::bytes::loadLe<std::uint16_t>(b16.data());
  const std::uint64_t R64 = apex::helpers::bytes::loadLe<std::uint64_t>(b64.data());

  // Assert
  EXPECT_EQ(R16, V16);
  EXPECT_EQ(R64, V64);
}

/**
 * @test Round-trip BE: storeBe then loadBe returns the original 16-bit and 64-bit values.
 */
TEST_F(ByteOpsTest, RoundTripBe_16_64) {
  // Arrange
  const std::uint16_t V16 = 0xCAFEu;
  const std::uint64_t V64 = 0xFEDC'BA98'7654'3210ull;

  std::array<std::uint8_t, sizeof(V16)> b16{};
  std::array<std::uint8_t, sizeof(V64)> b64{};

  // Act
  apex::helpers::bytes::storeBe(V16, b16.data());
  apex::helpers::bytes::storeBe(V64, b64.data());

  const std::uint16_t R16 = apex::helpers::bytes::loadBe<std::uint16_t>(b16.data());
  const std::uint64_t R64 = apex::helpers::bytes::loadBe<std::uint64_t>(b64.data());

  // Assert
  EXPECT_EQ(R16, V16);
  EXPECT_EQ(R64, V64);
}

/**
 * @test 8-bit edge case: LE/BE loads and stores are identical.
 */
TEST_F(ByteOpsTest, EdgeCase_8Bit) {
  // Arrange
  const std::uint8_t V8 = 0xA5u;
  std::array<std::uint8_t, 1> bytes{};

  // Act & Assert: store/load LE
  apex::helpers::bytes::storeLe(V8, bytes.data());
  EXPECT_EQ(bytes[0], V8);
  EXPECT_EQ(apex::helpers::bytes::loadLe<std::uint8_t>(bytes.data()), V8);

  // Act & Assert: store/load BE
  apex::helpers::bytes::storeBe(V8, bytes.data());
  EXPECT_EQ(bytes[0], V8);
  EXPECT_EQ(apex::helpers::bytes::loadBe<std::uint8_t>(bytes.data()), V8);
}
