/**
 * @file Sha3Hash256_uTest.cpp
 * @brief Unit tests for apex::encryption::Sha3Hash256.
 *
 * Notes:
 *  - OpenSSL EVP (EVP_sha3_256) is used as oracle to validate digest bytes and length.
 *  - Tests cover vector API, buffer API, empty input, and error conditions.
 */

#include "src/utilities/encryption/openssl/inc/Sha3Hash256.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <openssl/evp.h>
#include <string>
#include <vector>

using apex::encryption::Sha3Hash256;
using apex::encryption::sha3Hash256;

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL reference digest for a classic pangram. */
TEST(Sha3Hash256Test, BasicVectorHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  const size_t digestLen = static_cast<size_t>(EVP_MD_size(EVP_sha3_256()));
  std::vector<uint8_t> expected(digestLen);
  unsigned int outLen = 0;
  EVP_Digest(message.data(), message.size(), expected.data(), &outLen, EVP_sha3_256(), nullptr);
  ASSERT_EQ(outLen, digestLen);

  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = sha3Hash256(message, out);

  EXPECT_EQ(STATUS, Sha3Hash256::Status::SUCCESS);
  EXPECT_EQ(out.size(), digestLen);
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes as OpenSSL and correct digest length. */
TEST(Sha3Hash256Test, BasicBufferHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  const size_t digestLen = static_cast<size_t>(EVP_MD_size(EVP_sha3_256()));
  std::vector<uint8_t> expected(digestLen);
  unsigned int outLen = 0;
  EVP_Digest(message.data(), message.size(), expected.data(), &outLen, EVP_sha3_256(), nullptr);
  ASSERT_EQ(outLen, digestLen);

  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = sha3Hash256(message, buf, len);

  EXPECT_EQ(STATUS, Sha3Hash256::Status::SUCCESS);
  EXPECT_EQ(len, digestLen);
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/** @test Empty input is supported and matches OpenSSL for both APIs. */
TEST(Sha3Hash256Test, EmptyInput) {
  std::vector<uint8_t> message;

  const size_t digestLen = static_cast<size_t>(EVP_MD_size(EVP_sha3_256()));
  std::vector<uint8_t> expected(digestLen);
  unsigned int outLen = 0;
  EVP_Digest(nullptr, 0, expected.data(), &outLen, EVP_sha3_256(), nullptr);
  ASSERT_EQ(outLen, digestLen);

  // Vector API
  std::vector<uint8_t> outVec;
  outVec.reserve(EVP_MAX_MD_SIZE);
  const auto ST_VEC = sha3Hash256(message, outVec);
  EXPECT_EQ(ST_VEC, Sha3Hash256::Status::SUCCESS);
  EXPECT_EQ(outVec.size(), digestLen);
  EXPECT_TRUE(std::equal(outVec.begin(), outVec.end(), expected.begin()));

  // Buffer API
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto ST_BUF = sha3Hash256(message, buf, len);
  EXPECT_EQ(ST_BUF, Sha3Hash256::Status::SUCCESS);
  EXPECT_EQ(len, digestLen);
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Undersized caller buffer yields ERROR_OUTPUT_TOO_SMALL and required size. */
TEST(Sha3Hash256Test, ShortBuffer) {
  std::string msg = "abc";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> full;
  full.reserve(EVP_MAX_MD_SIZE);
  ASSERT_EQ(sha3Hash256(message, full), Sha3Hash256::Status::SUCCESS);

  constexpr size_t SMALL = 8;
  uint8_t smallBuf[SMALL] = {};
  size_t len = SMALL;

  const auto STATUS = sha3Hash256(message, smallBuf, len);

  EXPECT_EQ(STATUS, Sha3Hash256::Status::ERROR_OUTPUT_TOO_SMALL);
  EXPECT_EQ(len, full.size());
}
