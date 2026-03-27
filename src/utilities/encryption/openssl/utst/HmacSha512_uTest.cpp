/**
 * @file HmacSha512_uTest.cpp
 * @brief Unit tests for apex::encryption::HmacSha512.
 *
 * Notes:
 *  - OpenSSL HMAC(EVP_sha512()) is used as oracle to validate tag bytes and length.
 *  - Tests cover vector API, buffer API, and key validation.
 */

#include "src/utilities/encryption/openssl/inc/HmacSha512.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <string>
#include <vector>

using apex::encryption::HmacSha512;

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL HMAC for a classic pangram. */
TEST(HmacSha512Test, BasicVectorMac) {
  std::vector<uint8_t> key(HmacSha512::KEY_LENGTH);
  for (size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<uint8_t>('0' + (i % 10));

  std::string msg = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msg.begin(), msg.end());

  std::vector<uint8_t> expected(HmacSha512::DIGEST_LENGTH);
  unsigned int expLen = 0;
  HMAC(EVP_sha512(), key.data(), static_cast<int>(key.size()), message.data(), message.size(),
       expected.data(), &expLen);
  ASSERT_EQ(expLen, expected.size());

  HmacSha512 mac;
  mac.setKey(key);
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = mac.mac(message, out);

  EXPECT_EQ(STATUS, HmacSha512::Status::SUCCESS);
  EXPECT_EQ(out.size(), expected.size());
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes as OpenSSL and correct tag length. */
TEST(HmacSha512Test, BasicBufferMac) {
  std::vector<uint8_t> key(HmacSha512::KEY_LENGTH);
  for (size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<uint8_t>('A' + (i % 26));

  std::vector<uint8_t> message;

  std::vector<uint8_t> expected(HmacSha512::DIGEST_LENGTH);
  unsigned int expLen = 0;
  HMAC(EVP_sha512(), key.data(), static_cast<int>(key.size()), nullptr, 0, expected.data(),
       &expLen);
  ASSERT_EQ(expLen, expected.size());

  HmacSha512 mac;
  mac.setKey(key);
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = mac.mac(message, buf, len);

  EXPECT_EQ(STATUS, HmacSha512::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid key length returns ERROR_INVALID_KEY and does no work. */
TEST(HmacSha512Test, InvalidKey) {
  std::vector<uint8_t> key(8, 0x00);

  HmacSha512 mac;
  mac.setKey(key);
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = mac.mac(std::vector<uint8_t>{}, out);

  EXPECT_EQ(STATUS, HmacSha512::Status::ERROR_INVALID_KEY);
}
