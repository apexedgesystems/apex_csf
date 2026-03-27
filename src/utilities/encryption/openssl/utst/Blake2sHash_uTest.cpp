/**
 * @file Blake2sHash_uTest.cpp
 * @brief Unit tests for apex::encryption::Blake2sHash.
 *
 * Notes:
 *  - OpenSSL EVP (EVP_blake2s256) is used as oracle to validate digest bytes and length.
 *  - Tests cover vector API, buffer API, empty input, and error conditions.
 */

#include "src/utilities/encryption/openssl/inc/Blake2sHash.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <openssl/evp.h>
#include <string>
#include <vector>

using apex::encryption::Blake2sHash;
using apex::encryption::blake2sHash;

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL reference digest for a classic pangram. */
TEST(Blake2sHashTest, BasicVectorHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  const size_t digestLen = static_cast<size_t>(EVP_MD_size(EVP_blake2s256()));
  std::vector<uint8_t> expected(digestLen);
  unsigned int outLen = 0;
  ASSERT_EQ(EVP_Digest(message.data(), message.size(), expected.data(), &outLen, EVP_blake2s256(),
                       nullptr),
            1);
  ASSERT_EQ(outLen, digestLen);

  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = blake2sHash(message, out);

  EXPECT_EQ(STATUS, Blake2sHash::Status::SUCCESS);
  EXPECT_EQ(out.size(), digestLen);
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes as OpenSSL and correct digest length. */
TEST(Blake2sHashTest, BasicBufferHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  const size_t digestLen = static_cast<size_t>(EVP_MD_size(EVP_blake2s256()));
  std::vector<uint8_t> expected(digestLen);
  unsigned int outLen = 0;
  ASSERT_EQ(EVP_Digest(message.data(), message.size(), expected.data(), &outLen, EVP_blake2s256(),
                       nullptr),
            1);
  ASSERT_EQ(outLen, digestLen);

  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = blake2sHash(message, buf, len);

  EXPECT_EQ(STATUS, Blake2sHash::Status::SUCCESS);
  EXPECT_EQ(len, digestLen);
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/** @test Empty input is supported and matches OpenSSL for both APIs. */
TEST(Blake2sHashTest, EmptyInput) {
  std::vector<uint8_t> message;

  const size_t digestLen = static_cast<size_t>(EVP_MD_size(EVP_blake2s256()));
  std::vector<uint8_t> expected(digestLen);
  unsigned int outLen = 0;
  ASSERT_EQ(EVP_Digest(nullptr, 0, expected.data(), &outLen, EVP_blake2s256(), nullptr), 1);
  ASSERT_EQ(outLen, digestLen);

  // Vector API
  std::vector<uint8_t> outVec;
  outVec.reserve(EVP_MAX_MD_SIZE);
  const auto ST_VEC = blake2sHash(message, outVec);
  EXPECT_EQ(ST_VEC, Blake2sHash::Status::SUCCESS);
  EXPECT_TRUE(std::equal(outVec.begin(), outVec.end(), expected.begin()));

  // Buffer API
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto ST_BUF = blake2sHash(message, buf, len);
  EXPECT_EQ(ST_BUF, Blake2sHash::Status::SUCCESS);
  EXPECT_EQ(len, digestLen);
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Undersized caller buffer reports ERROR_OUTPUT_TOO_SMALL and sets required size. */
TEST(Blake2sHashTest, ShortBuffer) {
  std::string msg = "abc";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> full;
  full.reserve(EVP_MAX_MD_SIZE);
  ASSERT_EQ(blake2sHash(message, full), Blake2sHash::Status::SUCCESS);

  constexpr size_t SMALL = 10;
  uint8_t smallBuf[SMALL] = {};
  size_t len = SMALL;

  const auto STATUS = blake2sHash(message, smallBuf, len);

  EXPECT_EQ(STATUS, Blake2sHash::Status::ERROR_OUTPUT_TOO_SMALL);
  EXPECT_EQ(len, full.size());
  for (size_t i = 0; i < SMALL; ++i) {
    EXPECT_EQ(smallBuf[i], 0U);
  }
}
