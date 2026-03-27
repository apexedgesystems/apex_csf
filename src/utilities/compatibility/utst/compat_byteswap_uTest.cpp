/**
 * @file compat_byteswap_uTest.cpp
 * @brief Unit tests for compat_byteswap.hpp (C++23 std::byteswap shim).
 */

#include <cstdint>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

#include "src/utilities/compatibility/inc/compat_byteswap.hpp"

using apex::compat::byteswap;
using apex::compat::byteswapIeee;

/**
 * @test Byteswap_1Byte
 * @brief Verifies byteswap on 1-byte types is identity.
 */
TEST(CompatByteswap, Byteswap_1Byte) {
  EXPECT_EQ(byteswap(std::uint8_t{0x00}), std::uint8_t{0x00});
  EXPECT_EQ(byteswap(std::uint8_t{0xFF}), std::uint8_t{0xFF});
  EXPECT_EQ(byteswap(std::uint8_t{0xAB}), std::uint8_t{0xAB});

  EXPECT_EQ(byteswap(std::int8_t{0}), std::int8_t{0});
  EXPECT_EQ(byteswap(std::int8_t{-1}), std::int8_t{-1});
  EXPECT_EQ(byteswap(std::int8_t{42}), std::int8_t{42});
}

/**
 * @test Byteswap_2Byte
 * @brief Verifies byteswap correctly swaps 2-byte integers.
 */
TEST(CompatByteswap, Byteswap_2Byte) {
  EXPECT_EQ(byteswap(std::uint16_t{0x0000}), std::uint16_t{0x0000});
  EXPECT_EQ(byteswap(std::uint16_t{0xFFFF}), std::uint16_t{0xFFFF});
  EXPECT_EQ(byteswap(std::uint16_t{0x1234}), std::uint16_t{0x3412});
  EXPECT_EQ(byteswap(std::uint16_t{0xABCD}), std::uint16_t{0xCDAB});

  // Signed types
  EXPECT_EQ(byteswap(std::int16_t{0x0102}), std::int16_t{0x0201});
}

/**
 * @test Byteswap_4Byte
 * @brief Verifies byteswap correctly swaps 4-byte integers.
 */
TEST(CompatByteswap, Byteswap_4Byte) {
  EXPECT_EQ(byteswap(std::uint32_t{0x00000000}), std::uint32_t{0x00000000});
  EXPECT_EQ(byteswap(std::uint32_t{0xFFFFFFFF}), std::uint32_t{0xFFFFFFFF});
  EXPECT_EQ(byteswap(std::uint32_t{0x12345678}), std::uint32_t{0x78563412});
  EXPECT_EQ(byteswap(std::uint32_t{0xDEADBEEF}), std::uint32_t{0xEFBEADDE});

  // Signed types
  EXPECT_EQ(byteswap(std::int32_t{0x01020304}), std::int32_t{0x04030201});
}

/**
 * @test Byteswap_8Byte
 * @brief Verifies byteswap correctly swaps 8-byte integers.
 */
TEST(CompatByteswap, Byteswap_8Byte) {
  EXPECT_EQ(byteswap(std::uint64_t{0x0000000000000000ULL}), std::uint64_t{0x0000000000000000ULL});
  EXPECT_EQ(byteswap(std::uint64_t{0xFFFFFFFFFFFFFFFFULL}), std::uint64_t{0xFFFFFFFFFFFFFFFFULL});
  EXPECT_EQ(byteswap(std::uint64_t{0x0102030405060708ULL}), std::uint64_t{0x0807060504030201ULL});
  EXPECT_EQ(byteswap(std::uint64_t{0xDEADBEEFCAFEBABEULL}), std::uint64_t{0xBEBAFECAEFBEADDEULL});

  // Signed types
  EXPECT_EQ(byteswap(std::int64_t{0x0102030405060708LL}), std::int64_t{0x0807060504030201LL});
}

/**
 * @test Byteswap_Roundtrip
 * @brief Verifies double-swap returns original value.
 */
TEST(CompatByteswap, Byteswap_Roundtrip) {
  constexpr std::uint16_t val16 = 0x1234;
  constexpr std::uint32_t val32 = 0x12345678;
  constexpr std::uint64_t val64 = 0x123456789ABCDEF0ULL;

  EXPECT_EQ(byteswap(byteswap(val16)), val16);
  EXPECT_EQ(byteswap(byteswap(val32)), val32);
  EXPECT_EQ(byteswap(byteswap(val64)), val64);
}

/**
 * @test Byteswap_Constexpr
 * @brief Verifies byteswap is usable in constexpr context.
 */
TEST(CompatByteswap, Byteswap_Constexpr) {
  constexpr std::uint32_t original = 0x12345678;
  constexpr std::uint32_t swapped = byteswap(original);
  constexpr std::uint32_t roundtrip = byteswap(swapped);

  EXPECT_EQ(swapped, std::uint32_t{0x78563412});
  EXPECT_EQ(roundtrip, original);
}

/**
 * @test ByteswapIeee_Float
 * @brief Verifies byteswapIeee correctly swaps float bytes.
 */
TEST(CompatByteswap, ByteswapIeee_Float) {
  const float original = 1.0f;
  const float swapped = byteswapIeee(original);
  const float roundtrip = byteswapIeee(swapped);

  // Roundtrip should restore original
  EXPECT_EQ(std::memcmp(&original, &roundtrip, sizeof(float)), 0);

  // Swapped should differ (unless platform-specific edge case)
  std::uint32_t origBits = 0;
  std::uint32_t swapBits = 0;
  std::memcpy(&origBits, &original, sizeof(float));
  std::memcpy(&swapBits, &swapped, sizeof(float));
  EXPECT_EQ(swapBits, byteswap(origBits));
}

/**
 * @test ByteswapIeee_Double
 * @brief Verifies byteswapIeee correctly swaps double bytes.
 */
TEST(CompatByteswap, ByteswapIeee_Double) {
  const double original = 3.14159265358979323846;
  const double swapped = byteswapIeee(original);
  const double roundtrip = byteswapIeee(swapped);

  // Roundtrip should restore original
  EXPECT_EQ(std::memcmp(&original, &roundtrip, sizeof(double)), 0);

  // Verify bit-level swap matches integral byteswap
  std::uint64_t origBits = 0;
  std::uint64_t swapBits = 0;
  std::memcpy(&origBits, &original, sizeof(double));
  std::memcpy(&swapBits, &swapped, sizeof(double));
  EXPECT_EQ(swapBits, byteswap(origBits));
}

/**
 * @test ByteswapIeee_SpecialValues
 * @brief Verifies byteswapIeee handles special float values.
 */
TEST(CompatByteswap, ByteswapIeee_SpecialValues) {
  // Zero roundtrips correctly
  const float zeroF = 0.0f;
  const float zeroFRoundtrip = byteswapIeee(byteswapIeee(zeroF));
  EXPECT_EQ(std::memcmp(&zeroF, &zeroFRoundtrip, sizeof(float)), 0);

  const double zeroD = 0.0;
  const double zeroDRoundtrip = byteswapIeee(byteswapIeee(zeroD));
  EXPECT_EQ(std::memcmp(&zeroD, &zeroDRoundtrip, sizeof(double)), 0);

  // Infinity roundtrips correctly
  const float infF = std::numeric_limits<float>::infinity();
  const float infRoundtrip = byteswapIeee(byteswapIeee(infF));
  EXPECT_EQ(std::memcmp(&infF, &infRoundtrip, sizeof(float)), 0);

  // Negative values roundtrip correctly
  const double negD = -123.456;
  const double negRoundtrip = byteswapIeee(byteswapIeee(negD));
  EXPECT_EQ(std::memcmp(&negD, &negRoundtrip, sizeof(double)), 0);
}
