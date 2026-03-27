/**
 * @file getEndianness_uTest.cpp
 * @brief Unit tests for compile-time endianness helpers.
 *
 * Coverage:
 *  - Exact literal mapping for big/little
 *  - Fallback mapping for unknown values → "native-endian"
 *  - Parameterless overload returns host-native endianness
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <gtest/gtest.h>

/**
 * @test Maps apex::compat::endian::big to "big-endian".
 */
TEST(GetEndiannessTest, MapsBigEndianLiteral) {
  EXPECT_STREQ(apex::helpers::bytes::endianName(apex::compat::endian::big), "big-endian");
}

/**
 * @test Maps apex::compat::endian::little to "little-endian".
 */
TEST(GetEndiannessTest, MapsLittleEndianLiteral) {
  EXPECT_STREQ(apex::helpers::bytes::endianName(apex::compat::endian::little), "little-endian");
}

/**
 * @test Unknown/unsupported values map to "native-endian".
 */
TEST(GetEndiannessTest, UnknownValueMapsToNative) {
  // Deliberately pass a nonstandard enumerator.
  const auto BOGUS = static_cast<apex::compat::endian>(0x42);
  EXPECT_STREQ(apex::helpers::bytes::endianName(BOGUS), "native-endian");
}

/**
 * @test Parameterless overload reflects the platform’s native endianness.
 */
TEST(GetEndiannessTest, NativeOverloadMatchesSystem) {
  const char* expected =
      (apex::compat::NATIVE_ENDIAN == apex::compat::endian::big) ? "big-endian" : "little-endian";
  EXPECT_STREQ(apex::helpers::bytes::nativeEndianName(), expected);
}
