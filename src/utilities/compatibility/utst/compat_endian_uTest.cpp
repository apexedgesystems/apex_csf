/**
 * @file compat_endian_uTest.cpp
 * @brief Unit tests for compat_endian.hpp (C++20 std::endian shim).
 */

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "src/utilities/compatibility/inc/compat_endian.hpp"

using apex::compat::endian;
using apex::compat::endianName;
using apex::compat::NATIVE_ENDIAN;

/**
 * @test Endian_EnumValues
 * @brief Verifies endian enum has the three required values.
 */
TEST(CompatEndian, Endian_EnumValues) {
  // Enum values should be distinct
  EXPECT_NE(static_cast<int>(endian::little), static_cast<int>(endian::big));

  // native must equal one of little or big on real hardware
  const bool nativeIsLittle = (NATIVE_ENDIAN == endian::little);
  const bool nativeIsBig = (NATIVE_ENDIAN == endian::big);
  EXPECT_TRUE(nativeIsLittle || nativeIsBig)
      << "NATIVE_ENDIAN must be either little or big on real hardware";
}

/**
 * @test Endian_NativeDetection
 * @brief Verifies NATIVE_ENDIAN matches runtime byte-order detection.
 */
TEST(CompatEndian, Endian_NativeDetection) {
  // Runtime endianness check using a union
  union {
    std::uint32_t u32;
    std::uint8_t bytes[4];
  } probe{};
  probe.u32 = 0x01020304;

  const bool runtimeIsLittle = (probe.bytes[0] == 0x04);
  const bool runtimeIsBig = (probe.bytes[0] == 0x01);

  if (runtimeIsLittle) {
    EXPECT_EQ(NATIVE_ENDIAN, endian::little);
  } else if (runtimeIsBig) {
    EXPECT_EQ(NATIVE_ENDIAN, endian::big);
  } else {
    FAIL() << "Unable to determine runtime endianness";
  }
}

/**
 * @test Endian_NameStrings
 * @brief Verifies endianName() returns correct strings for each enum value.
 *
 * Note: In C++20, endian::native equals endian::little or endian::big,
 * so endianName(endian::native) returns "little-endian" or "big-endian".
 * The "native-endian" string is only returned for values that are neither.
 */
TEST(CompatEndian, Endian_NameStrings) {
  EXPECT_STREQ(endianName(endian::little), "little-endian");
  EXPECT_STREQ(endianName(endian::big), "big-endian");

  // On C++20, native equals little or big, so endianName returns the concrete name
  const char* nativeName = endianName(endian::native);
  EXPECT_TRUE(std::strcmp(nativeName, "little-endian") == 0 ||
              std::strcmp(nativeName, "big-endian") == 0);
}

/**
 * @test Endian_NameConstexpr
 * @brief Verifies endianName() is usable in constexpr context.
 */
TEST(CompatEndian, Endian_NameConstexpr) {
  constexpr const char* littleName = endianName(endian::little);
  constexpr const char* bigName = endianName(endian::big);

  EXPECT_NE(littleName, nullptr);
  EXPECT_NE(bigName, nullptr);
}

/**
 * @test Endian_NativeEndianConstexpr
 * @brief Verifies NATIVE_ENDIAN is usable in constexpr context.
 */
TEST(CompatEndian, Endian_NativeEndianConstexpr) {
  constexpr endian native = NATIVE_ENDIAN;
  constexpr const char* name = endianName(native);

  EXPECT_NE(name, nullptr);
  // Name should be either "little-endian" or "big-endian"
  EXPECT_TRUE(std::string(name) == "little-endian" || std::string(name) == "big-endian");
}
