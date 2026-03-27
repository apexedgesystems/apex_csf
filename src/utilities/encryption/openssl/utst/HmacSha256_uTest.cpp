/**
 * @file HmacSha256_uTest.cpp
 * @brief Unit tests for apex::encryption::HmacSha256.
 *
 * Notes:
 *  - OpenSSL HMAC(EVP_sha256()) is used as oracle to validate tag bytes and length.
 *  - Tests cover vector API, buffer API, and key validation.
 */

#include "src/utilities/encryption/openssl/inc/HmacSha256.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <string>
#include <vector>

using apex::encryption::HmacSha256;

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL HMAC for a classic pangram. */
TEST(HmacSha256Test, BasicVectorMac) {
  std::array<uint8_t, HmacSha256::KEY_LENGTH> key{};
  for (size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<uint8_t>('0' + (i % 10));

  std::string msgStr = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msgStr.begin(), msgStr.end());

  std::vector<uint8_t> expected(HmacSha256::DIGEST_LENGTH);
  unsigned int expectedLen = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), message.data(), message.size(),
       expected.data(), &expectedLen);
  ASSERT_EQ(expectedLen, HmacSha256::DIGEST_LENGTH);

  HmacSha256 mac;
  mac.setKey(key);
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = mac.mac(message, out);

  EXPECT_EQ(STATUS, HmacSha256::Status::SUCCESS);
  EXPECT_EQ(out.size(), expected.size());
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes as OpenSSL and correct tag length. */
TEST(HmacSha256Test, BasicBufferMac) {
  std::array<uint8_t, HmacSha256::KEY_LENGTH> key{};
  for (size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<uint8_t>('0' + (i % 10));

  std::string msgStr = "abc";
  std::vector<uint8_t> message(msgStr.begin(), msgStr.end());

  std::vector<uint8_t> expected(HmacSha256::DIGEST_LENGTH);
  unsigned int expectedLen = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), message.data(), message.size(),
       expected.data(), &expectedLen);
  ASSERT_EQ(expectedLen, HmacSha256::DIGEST_LENGTH);

  HmacSha256 mac;
  mac.setKey(key);
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = mac.mac(message, buf, len);

  EXPECT_EQ(STATUS, HmacSha256::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid key length returns ERROR_INVALID_KEY (no computation performed). */
TEST(HmacSha256Test, InvalidKey) {
  std::vector<uint8_t> shortKey(HmacSha256::KEY_LENGTH - 1, 0x00);

  std::string msgStr = "data";
  std::vector<uint8_t> message(msgStr.begin(), msgStr.end());

  HmacSha256 mac;
  mac.setKey(shortKey);
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = mac.mac(message, out);

  EXPECT_EQ(STATUS, HmacSha256::Status::ERROR_INVALID_KEY);
}
