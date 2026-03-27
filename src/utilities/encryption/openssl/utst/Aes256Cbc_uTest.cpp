/**
 * @file Aes256Cbc_uTest.cpp
 * @brief Unit tests for apex::encryption::Aes256Cbc.
 *
 * Notes:
 *  - Tests cover vector API, buffer API, key/IV validation, and roundtrip.
 */

#include "src/utilities/encryption/openssl/inc/Aes256Cbc.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using apex::encryption::Aes256Cbc;
using apex::encryption::aes256CbcDecrypt;
using apex::encryption::aes256CbcEncrypt;

/* ----------------------------- Test Fixtures ----------------------------- */

/** 256-bit ASCII key: "01234567890123456789012345678901" */
static constexpr std::array<uint8_t, Aes256Cbc::KEY_LENGTH> K_TEST_KEY = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'};

/** 128-bit ASCII IV: "0123456789012345" */
static constexpr std::array<uint8_t, Aes256Cbc::IV_LENGTH> K_TEST_IV = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5'};

/* ----------------------------- Encrypt Tests ----------------------------- */

/** @test BasicVectorEncrypt verifies AES-256-CBC encrypts the sample text to the known ciphertext.
 */
TEST(Aes256CbcTest, BasicVectorEncrypt) {
  std::string plaintext = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::vector<uint8_t> expected = {0xe0, 0x6f, 0x63, 0xa7, 0x11, 0xe8, 0xb7, 0xaa, 0x9f, 0x94,
                                   0x40, 0x10, 0x7d, 0x46, 0x80, 0xa1, 0x17, 0x99, 0x43, 0x80,
                                   0xea, 0x31, 0xd2, 0xa2, 0x99, 0xb9, 0x53, 0x02, 0xd4, 0x39,
                                   0xb9, 0x70, 0x2c, 0x8e, 0x65, 0xa9, 0x92, 0x36, 0xec, 0x92,
                                   0x07, 0x04, 0x91, 0x5c, 0xf1, 0xa9, 0x8a, 0x44};

  std::vector<uint8_t> ct;
  ct.reserve(pt.size() + Aes256Cbc::BLOCK_SIZE);
  const auto STATUS = aes256CbcEncrypt(pt, K_TEST_KEY, K_TEST_IV, ct);

  EXPECT_EQ(STATUS, Aes256Cbc::Status::SUCCESS);
  EXPECT_EQ(ct.size(), expected.size());
  EXPECT_TRUE(std::equal(ct.begin(), ct.end(), expected.begin()));
}

/** @test BasicBufferEncrypt verifies the zero-allocation encrypt API produces the same output. */
TEST(Aes256CbcTest, BasicBufferEncrypt) {
  std::string plaintext = "Hello, AES-CBC buffer test!";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());

  std::vector<uint8_t> expected;
  expected.reserve(pt.size() + Aes256Cbc::BLOCK_SIZE);
  ASSERT_EQ(aes256CbcEncrypt(pt, K_TEST_KEY, K_TEST_IV, expected), Aes256Cbc::Status::SUCCESS);

  uint8_t buf[128] = {0};
  size_t len = sizeof(buf);
  const auto STATUS = aes256CbcEncrypt(pt, K_TEST_KEY, K_TEST_IV, buf, len);

  EXPECT_EQ(STATUS, Aes256Cbc::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/** @test EmptyPlaintext ensures encrypting an empty vector still produces a single block (padding).
 */
TEST(Aes256CbcTest, EmptyPlaintext) {
  std::vector<uint8_t> pt;

  // Vector API
  std::vector<uint8_t> ct;
  const auto ST_VEC = aes256CbcEncrypt(pt, K_TEST_KEY, K_TEST_IV, ct);

  EXPECT_EQ(ST_VEC, Aes256Cbc::Status::SUCCESS);
  EXPECT_EQ(ct.size(), Aes256Cbc::BLOCK_SIZE);

  // Buffer API
  uint8_t buf[64] = {};
  size_t len = sizeof(buf);
  const auto ST_BUF = aes256CbcEncrypt(pt, K_TEST_KEY, K_TEST_IV, buf, len);

  EXPECT_EQ(ST_BUF, Aes256Cbc::Status::SUCCESS);
  EXPECT_EQ(len, Aes256Cbc::BLOCK_SIZE);
}

/* ----------------------------- Decrypt Tests ----------------------------- */

/** @test BasicVectorDecrypt verifies AES-256-CBC decrypts back to the original plaintext. */
TEST(Aes256CbcTest, BasicVectorDecrypt) {
  std::vector<uint8_t> ct = {0xe0, 0x6f, 0x63, 0xa7, 0x11, 0xe8, 0xb7, 0xaa, 0x9f, 0x94,
                             0x40, 0x10, 0x7d, 0x46, 0x80, 0xa1, 0x17, 0x99, 0x43, 0x80,
                             0xea, 0x31, 0xd2, 0xa2, 0x99, 0xb9, 0x53, 0x02, 0xd4, 0x39,
                             0xb9, 0x70, 0x2c, 0x8e, 0x65, 0xa9, 0x92, 0x36, 0xec, 0x92,
                             0x07, 0x04, 0x91, 0x5c, 0xf1, 0xa9, 0x8a, 0x44};
  std::string expected = "The quick brown fox jumps over the lazy dog";

  std::vector<uint8_t> pt;
  pt.reserve(ct.size());
  const auto STATUS = aes256CbcDecrypt(ct, K_TEST_KEY, K_TEST_IV, pt);

  EXPECT_EQ(STATUS, Aes256Cbc::Status::SUCCESS);
  std::string result(pt.begin(), pt.end());
  EXPECT_EQ(result, expected);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test InvalidKey (too short) triggers ERROR_INVALID_KEY. */
TEST(Aes256CbcTest, InvalidKey) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, Aes256Cbc::KEY_LENGTH / 2> shortKey{};
  std::vector<uint8_t> ct;

  const auto STATUS = aes256CbcEncrypt(pt, shortKey, K_TEST_IV, ct);

  EXPECT_EQ(STATUS, Aes256Cbc::Status::ERROR_INVALID_KEY);
}

/** @test InvalidIv (too short) triggers ERROR_INVALID_IV. */
TEST(Aes256CbcTest, InvalidIv) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, Aes256Cbc::IV_LENGTH / 2> shortIv{};
  std::vector<uint8_t> ct;

  const auto STATUS = aes256CbcEncrypt(pt, K_TEST_KEY, shortIv, ct);

  EXPECT_EQ(STATUS, Aes256Cbc::Status::ERROR_INVALID_IV);
}
