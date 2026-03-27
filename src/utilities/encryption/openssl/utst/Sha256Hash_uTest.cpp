/**
 * @file Sha256Hash_uTest.cpp
 * @brief Unit tests for apex::encryption::Sha256Hash.
 *
 * Notes:
 *  - OpenSSL SHA256() is used as oracle to validate digest bytes and length.
 *  - Tests cover vector API, buffer API, empty input, and error conditions.
 */

#include "src/utilities/encryption/openssl/inc/Sha256Hash.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <string>
#include <vector>

using apex::encryption::Sha256Hash;
using apex::encryption::sha256Hash;

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL reference digest for a classic pangram. */
TEST(Sha256HashTest, BasicVectorHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> expected(SHA256_DIGEST_LENGTH);
  SHA256(message.data(), message.size(), expected.data());

  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = sha256Hash(message, out);

  EXPECT_EQ(STATUS, Sha256Hash::Status::SUCCESS);
  EXPECT_EQ(out.size(), static_cast<size_t>(SHA256_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes as OpenSSL and correct digest length. */
TEST(Sha256HashTest, BasicBufferHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> expected(SHA256_DIGEST_LENGTH);
  SHA256(message.data(), message.size(), expected.data());

  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = sha256Hash(message, buf, len);

  EXPECT_EQ(STATUS, Sha256Hash::Status::SUCCESS);
  EXPECT_EQ(len, static_cast<size_t>(SHA256_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/** @test Empty input is supported and matches OpenSSL for both APIs. */
TEST(Sha256HashTest, EmptyInput) {
  std::vector<uint8_t> message;

  std::vector<uint8_t> expected(SHA256_DIGEST_LENGTH);
  SHA256(nullptr, 0, expected.data());

  // Vector API
  std::vector<uint8_t> outVec;
  outVec.reserve(EVP_MAX_MD_SIZE);
  const auto ST_VEC = sha256Hash(message, outVec);
  EXPECT_EQ(ST_VEC, Sha256Hash::Status::SUCCESS);
  EXPECT_EQ(outVec.size(), expected.size());
  EXPECT_TRUE(std::equal(outVec.begin(), outVec.end(), expected.begin()));

  // Buffer API
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto ST_BUF = sha256Hash(message, buf, len);
  EXPECT_EQ(ST_BUF, Sha256Hash::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Undersized caller buffer reports ERROR_OUTPUT_TOO_SMALL and sets required size. */
TEST(Sha256HashTest, ShortBuffer) {
  std::string msg = "abc";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> full;
  full.reserve(EVP_MAX_MD_SIZE);
  ASSERT_EQ(sha256Hash(message, full), Sha256Hash::Status::SUCCESS);

  constexpr size_t SMALL = 10;
  uint8_t smallBuf[SMALL] = {};
  size_t len = SMALL;

  const auto STATUS = sha256Hash(message, smallBuf, len);

  EXPECT_EQ(STATUS, Sha256Hash::Status::ERROR_OUTPUT_TOO_SMALL);
  EXPECT_EQ(len, full.size());
  for (size_t i = 0; i < SMALL; ++i) {
    EXPECT_EQ(smallBuf[i], 0U);
  }
}
