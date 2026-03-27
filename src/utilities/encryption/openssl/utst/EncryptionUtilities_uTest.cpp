/**
 * @file EncryptionUtilities_uTest.cpp
 * @brief Unit tests for apex::encryption utility functions.
 *
 * Notes:
 *  - Tests cover random generation, hex encoding, constant-time comparison, and secure zeroing.
 */

#include "src/utilities/encryption/openssl/inc/EncryptionUtilities.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using apex::encryption::constantTimeCompare;
using apex::encryption::fromHex;
using apex::encryption::generateRandomBytes;
using apex::encryption::generateRandomVector;
using apex::encryption::secureZeroMemory;
using apex::encryption::toHex;

/* ----------------------------- API Tests ----------------------------- */

/** @test GenerateRandomBytes fills the buffer and returns success. */
TEST(EncryptionUtilitiesTest, GenerateRandomBytes) {
  std::vector<uint8_t> buf(32);
  const auto STATUS = generateRandomBytes(buf);
  EXPECT_EQ(STATUS, 0);
  bool allZero = std::all_of(buf.begin(), buf.end(), [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(allZero);
}

/** @test GenerateRandomVector returns correct length and non-empty for >0. */
TEST(EncryptionUtilitiesTest, GenerateRandomVector) {
  auto v0 = generateRandomVector(0);
  EXPECT_TRUE(v0.empty());

  auto v16 = generateRandomVector(16);
  EXPECT_EQ(v16.size(), 16U);
  auto v16b = generateRandomVector(16);
  EXPECT_EQ(v16b.size(), 16U);
  bool identical = std::equal(v16.begin(), v16.end(), v16b.begin());
  EXPECT_FALSE(identical);
}

/** @test toHex and fromHex perform a proper round-trip. */
TEST(EncryptionUtilitiesTest, HexRoundTrip) {
  std::vector<uint8_t> data = {0x00, 0xAB, 0x7F, 0x10, 0xFF};
  std::string hex = toHex(data);
  EXPECT_EQ(hex, "00ab7f10ff");

  std::vector<uint8_t> parsed;
  const auto OK = fromHex(hex, parsed);
  EXPECT_TRUE(OK);
  EXPECT_EQ(parsed, data);
}

/** @test fromHex rejects odd length and invalid characters. */
TEST(EncryptionUtilitiesTest, FromHexInvalid) {
  std::vector<uint8_t> out;
  EXPECT_FALSE(fromHex("abc", out));
  EXPECT_FALSE(fromHex("zz", out));
  EXPECT_TRUE(out.empty());
}

/** @test constantTimeCompare returns true for equal buffers, false otherwise. */
TEST(EncryptionUtilitiesTest, ConstantTimeCompare) {
  std::vector<uint8_t> a = {1, 2, 3, 4};
  std::vector<uint8_t> b = a;
  EXPECT_TRUE(constantTimeCompare(a, b));

  b[2] = 0xFF;
  EXPECT_FALSE(constantTimeCompare(a, b));

  std::vector<uint8_t> c = {1, 2, 3};
  EXPECT_FALSE(constantTimeCompare(a, c));
}

/** @test secureZeroMemory wipes the buffer to all zeros. */
TEST(EncryptionUtilitiesTest, SecureZeroMemory) {
  std::vector<uint8_t> buf = {0x11, 0x22, 0x33, 0x44};
  secureZeroMemory(buf.data(), buf.size());
  for (uint8_t b : buf) {
    EXPECT_EQ(b, 0);
  }

  secureZeroMemory(buf.data(), 0);
  EXPECT_FALSE(buf.empty());
}
