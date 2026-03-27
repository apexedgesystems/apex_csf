/**
 * @file Md5Hash_uTest.cpp
 * @brief Unit tests for apex::encryption::Md5Hash.
 *
 * Notes:
 *  - OpenSSL EVP (EVP_md5) is used as oracle to validate digest bytes and length.
 *  - Tests cover vector API, buffer API, empty input, and error conditions.
 */

#include "src/utilities/encryption/openssl/inc/Md5Hash.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <string>
#include <vector>

using apex::encryption::Md5Hash;
using apex::encryption::md5Hash;

/* ----------------------------- File Helpers ----------------------------- */

/// Compute the reference MD5 digest via OpenSSL EVP.
static void computeExpectedMd5(const std::vector<uint8_t>& message, uint8_t* out) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  ASSERT_NE(ctx, nullptr);

  EXPECT_EQ(EVP_DigestInit_ex(ctx, EVP_md5(), nullptr), 1);
  EXPECT_EQ(EVP_DigestUpdate(ctx, message.empty() ? nullptr : message.data(), message.size()), 1);

  unsigned int outLen = 0;
  EXPECT_EQ(EVP_DigestFinal_ex(ctx, out, &outLen), 1);
  EXPECT_EQ(outLen, static_cast<unsigned int>(MD5_DIGEST_LENGTH));

  EVP_MD_CTX_free(ctx);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL reference digest for a classic pangram. */
TEST(Md5HashTest, BasicVectorHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> expected(MD5_DIGEST_LENGTH);
  computeExpectedMd5(message, expected.data());

  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = md5Hash(message, out);

  EXPECT_EQ(STATUS, Md5Hash::Status::SUCCESS);
  EXPECT_EQ(out.size(), static_cast<size_t>(MD5_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes as OpenSSL and correct digest length. */
TEST(Md5HashTest, BasicBufferHash) {
  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> expected(MD5_DIGEST_LENGTH);
  computeExpectedMd5(message, expected.data());

  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;

  const auto STATUS = md5Hash(message, buf, len);
  EXPECT_EQ(STATUS, Md5Hash::Status::SUCCESS);
  EXPECT_EQ(len, static_cast<size_t>(MD5_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/** @test Empty input is supported and matches OpenSSL for both APIs. */
TEST(Md5HashTest, EmptyInput) {
  std::vector<uint8_t> message;

  std::vector<uint8_t> expected(MD5_DIGEST_LENGTH);
  computeExpectedMd5(message, expected.data());

  // Vector API
  std::vector<uint8_t> outVec;
  outVec.reserve(EVP_MAX_MD_SIZE);
  const auto ST_VEC = md5Hash(message, outVec);
  EXPECT_EQ(ST_VEC, Md5Hash::Status::SUCCESS);
  EXPECT_EQ(outVec.size(), static_cast<size_t>(MD5_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(outVec.begin(), outVec.end(), expected.begin()));

  // Buffer API
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto ST_BUF = md5Hash(message, buf, len);
  EXPECT_EQ(ST_BUF, Md5Hash::Status::SUCCESS);
  EXPECT_EQ(len, static_cast<size_t>(MD5_DIGEST_LENGTH));
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Undersized caller buffer reports ERROR_OUTPUT_TOO_SMALL and sets required size. */
TEST(Md5HashTest, ShortBuffer) {
  std::string msg = "abc";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> full;
  full.reserve(EVP_MAX_MD_SIZE);
  ASSERT_EQ(md5Hash(message, full), Md5Hash::Status::SUCCESS);
  ASSERT_EQ(full.size(), static_cast<size_t>(MD5_DIGEST_LENGTH));

  constexpr size_t SMALL = 5;
  uint8_t smallBuf[SMALL] = {};
  size_t len = SMALL;

  const auto STATUS = md5Hash(message, smallBuf, len);

  EXPECT_EQ(STATUS, Md5Hash::Status::ERROR_OUTPUT_TOO_SMALL);
  EXPECT_EQ(len, full.size());
  for (size_t i = 0; i < SMALL; ++i) {
    EXPECT_EQ(smallBuf[i], 0U);
  }
}
