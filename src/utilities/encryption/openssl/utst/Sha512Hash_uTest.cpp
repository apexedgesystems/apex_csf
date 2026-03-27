/**
 * @file Sha512Hash_uTest.cpp
 * @brief Unit tests for apex::encryption::Sha512Hash.
 *
 * Notes:
 *  - OpenSSL SHA512() is used as oracle to validate digest bytes and length.
 *  - Tests cover vector API, buffer API, empty input, and error conditions.
 */

#include "src/utilities/encryption/openssl/inc/Sha512Hash.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <string>
#include <vector>

using apex::encryption::Sha512Hash;
using apex::encryption::sha512Hash;

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL reference digest for a classic pangram. */
TEST(Sha512HashTest, BasicVectorHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> expected(SHA512_DIGEST_LENGTH);
  SHA512(message.data(), message.size(), expected.data());

  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = sha512Hash(message, out);

  EXPECT_EQ(STATUS, Sha512Hash::Status::SUCCESS);
  EXPECT_EQ(out.size(), static_cast<size_t>(SHA512_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes as OpenSSL and correct digest length. */
TEST(Sha512HashTest, BasicBufferHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> expected(SHA512_DIGEST_LENGTH);
  SHA512(message.data(), message.size(), expected.data());

  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = sha512Hash(message, buf, len);

  EXPECT_EQ(STATUS, Sha512Hash::Status::SUCCESS);
  EXPECT_EQ(len, static_cast<size_t>(SHA512_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/** @test Empty input is supported and matches OpenSSL for both APIs. */
TEST(Sha512HashTest, EmptyInput) {
  std::vector<uint8_t> message;

  std::vector<uint8_t> expected(SHA512_DIGEST_LENGTH);
  SHA512(nullptr, 0, expected.data());

  // Vector API
  std::vector<uint8_t> outVec;
  outVec.reserve(EVP_MAX_MD_SIZE);
  const auto ST_VEC = sha512Hash(message, outVec);
  EXPECT_EQ(ST_VEC, Sha512Hash::Status::SUCCESS);
  EXPECT_EQ(outVec.size(), expected.size());
  EXPECT_TRUE(std::equal(outVec.begin(), outVec.end(), expected.begin()));

  // Buffer API
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto ST_BUF = sha512Hash(message, buf, len);
  EXPECT_EQ(ST_BUF, Sha512Hash::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Undersized caller buffer reports ERROR_OUTPUT_TOO_SMALL and sets required size. */
TEST(Sha512HashTest, ShortBuffer) {
  std::string msg = "abc";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> full;
  full.reserve(EVP_MAX_MD_SIZE);
  ASSERT_EQ(sha512Hash(message, full), Sha512Hash::Status::SUCCESS);

  constexpr size_t SMALL = 16;
  uint8_t smallBuf[SMALL] = {};
  size_t len = SMALL;

  const auto STATUS = sha512Hash(message, smallBuf, len);

  EXPECT_EQ(STATUS, Sha512Hash::Status::ERROR_OUTPUT_TOO_SMALL);
  EXPECT_EQ(len, full.size());
  for (size_t i = 0; i < SMALL; ++i) {
    EXPECT_EQ(smallBuf[i], 0U);
  }
}
