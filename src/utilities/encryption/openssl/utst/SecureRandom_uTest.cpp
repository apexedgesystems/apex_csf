/**
 * @file SecureRandom_uTest.cpp
 * @brief Unit tests for apex::encryption::SecureRandom.
 *
 * Notes:
 *  - Tests verify functionality, not randomness quality (OpenSSL handles that).
 *  - Tests cover fill, generate, randomUint32/64, and edge cases.
 */

#include "src/utilities/encryption/openssl/inc/SecureRandom.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

using apex::encryption::generateIv;
using apex::encryption::generateKey;
using apex::encryption::SecureRandom;

/* ----------------------------- Basic Tests ----------------------------- */

/** @test fill() populates buffer with random bytes. */
TEST(SecureRandomTest, FillBuffer) {
  std::array<uint8_t, 32> buf{};
  const auto STATUS = SecureRandom::fill(buf.data(), buf.size());

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  // Verify buffer was modified (extremely unlikely to be all zeros)
  bool allZero = std::all_of(buf.begin(), buf.end(), [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(allZero);
}

/** @test generate() with vector resizes and fills. */
TEST(SecureRandomTest, GenerateVector) {
  std::vector<uint8_t> buf;
  const auto STATUS = SecureRandom::generate(64, buf);

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  EXPECT_EQ(buf.size(), 64U);
  // Verify buffer was modified
  bool allZero = std::all_of(buf.begin(), buf.end(), [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(allZero);
}

/** @test generate() with fixed-size array. */
TEST(SecureRandomTest, GenerateArray) {
  std::array<uint8_t, 16> buf{};
  const auto STATUS = SecureRandom::generate(buf);

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  // Verify buffer was modified
  bool allZero = std::all_of(buf.begin(), buf.end(), [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(allZero);
}

/** @test randomUint32() generates a value. */
TEST(SecureRandomTest, RandomUint32) {
  uint32_t val1 = 0;
  uint32_t val2 = 0;

  const auto STATUS1 = SecureRandom::randomUint32(val1);
  const auto STATUS2 = SecureRandom::randomUint32(val2);

  EXPECT_EQ(STATUS1, SecureRandom::Status::SUCCESS);
  EXPECT_EQ(STATUS2, SecureRandom::Status::SUCCESS);
  // Extremely unlikely to get same value twice
  EXPECT_NE(val1, val2);
}

/** @test randomUint64() generates a value. */
TEST(SecureRandomTest, RandomUint64) {
  uint64_t val1 = 0;
  uint64_t val2 = 0;

  const auto STATUS1 = SecureRandom::randomUint64(val1);
  const auto STATUS2 = SecureRandom::randomUint64(val2);

  EXPECT_EQ(STATUS1, SecureRandom::Status::SUCCESS);
  EXPECT_EQ(STATUS2, SecureRandom::Status::SUCCESS);
  // Extremely unlikely to get same value twice
  EXPECT_NE(val1, val2);
}

/** @test isSeeded() returns true (OpenSSL always seeds from OS). */
TEST(SecureRandomTest, IsSeeded) { EXPECT_TRUE(SecureRandom::isSeeded()); }

/* ----------------------------- Edge Cases ----------------------------- */

/** @test fill() with zero size succeeds. */
TEST(SecureRandomTest, FillZeroSize) {
  std::array<uint8_t, 4> buf{0xAA, 0xBB, 0xCC, 0xDD};
  const auto STATUS = SecureRandom::fill(buf.data(), 0);

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  // Buffer should be unchanged
  EXPECT_EQ(buf[0], 0xAA);
  EXPECT_EQ(buf[1], 0xBB);
}

/** @test generate() with zero size succeeds. */
TEST(SecureRandomTest, GenerateZeroSize) {
  std::vector<uint8_t> buf;
  const auto STATUS = SecureRandom::generate(0, buf);

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  EXPECT_TRUE(buf.empty());
}

/** @test fill() with nullptr returns error (non-zero size). */
TEST(SecureRandomTest, FillNullptr) {
  const auto STATUS = SecureRandom::fill(nullptr, 32);
  EXPECT_EQ(STATUS, SecureRandom::Status::ERROR_INVALID_SIZE);
}

/* ----------------------------- Convenience API Tests ----------------------------- */

/** @test generateIv() creates random IV. */
TEST(SecureRandomTest, GenerateIv) {
  std::array<uint8_t, 12> iv{};
  const auto STATUS = generateIv(iv);

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  bool allZero = std::all_of(iv.begin(), iv.end(), [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(allZero);
}

/** @test generateKey() creates random key. */
TEST(SecureRandomTest, GenerateKey) {
  std::array<uint8_t, 32> key{};
  const auto STATUS = generateKey(key);

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  bool allZero = std::all_of(key.begin(), key.end(), [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(allZero);
}

/* ----------------------------- Distribution Tests ----------------------------- */

/** @test Multiple calls produce different results. */
TEST(SecureRandomTest, NonDeterministic) {
  std::array<uint8_t, 32> buf1{};
  std::array<uint8_t, 32> buf2{};

  EXPECT_EQ(SecureRandom::fill(buf1.data(), buf1.size()), SecureRandom::Status::SUCCESS);
  EXPECT_EQ(SecureRandom::fill(buf2.data(), buf2.size()), SecureRandom::Status::SUCCESS);

  // Extremely unlikely to be equal
  EXPECT_NE(buf1, buf2);
}

/** @test Large buffer generation succeeds. */
TEST(SecureRandomTest, LargeBuffer) {
  std::vector<uint8_t> buf;
  const auto STATUS = SecureRandom::generate(4096, buf);

  EXPECT_EQ(STATUS, SecureRandom::Status::SUCCESS);
  EXPECT_EQ(buf.size(), 4096U);
}

/** @test Standard IV sizes work. */
TEST(SecureRandomTest, StandardIvSizes) {
  std::array<uint8_t, 12> gcmIv{}; // GCM standard
  std::array<uint8_t, 16> cbcIv{}; // CBC standard
  std::array<uint8_t, 8> des3Iv{}; // 3DES

  EXPECT_EQ(generateIv(gcmIv), SecureRandom::Status::SUCCESS);
  EXPECT_EQ(generateIv(cbcIv), SecureRandom::Status::SUCCESS);
  EXPECT_EQ(generateIv(des3Iv), SecureRandom::Status::SUCCESS);
}

/** @test Standard key sizes work. */
TEST(SecureRandomTest, StandardKeySizes) {
  std::array<uint8_t, 16> aes128{};
  std::array<uint8_t, 24> aes192{};
  std::array<uint8_t, 32> aes256{};

  EXPECT_EQ(generateKey(aes128), SecureRandom::Status::SUCCESS);
  EXPECT_EQ(generateKey(aes192), SecureRandom::Status::SUCCESS);
  EXPECT_EQ(generateKey(aes256), SecureRandom::Status::SUCCESS);
}
