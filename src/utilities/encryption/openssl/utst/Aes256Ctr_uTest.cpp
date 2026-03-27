/**
 * @file Aes256Ctr_uTest.cpp
 * @brief Unit tests for apex::encryption::Aes256Ctr.
 *
 * Notes:
 *  - OpenSSL EVP (EVP_aes_256_ctr) is used as oracle to validate ciphertext.
 *  - Tests cover vector API, buffer API, key/IV validation, and roundtrip.
 */

#include "src/utilities/encryption/openssl/inc/Aes256Ctr.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <openssl/evp.h>
#include <string>
#include <vector>

#include "compat_span.hpp"

using apex::encryption::Aes256Ctr;
using apex::encryption::aes256CtrDecrypt;
using apex::encryption::aes256CtrEncrypt;

/* ----------------------------- Test Fixtures ----------------------------- */

/** 256-bit ASCII key: "01234567890123456789012345678901" */
static constexpr std::array<uint8_t, Aes256Ctr::KEY_LENGTH> K_TEST_KEY = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'};

/** 128-bit ASCII IV: "0123456789012345" */
static constexpr std::array<uint8_t, Aes256Ctr::IV_LENGTH> K_TEST_IV = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5'};

/* ----------------------------- File Helpers ----------------------------- */

/// Compute reference AES-256-CTR ciphertext via OpenSSL EVP.
static std::vector<uint8_t> computeRefCtr(apex::compat::bytes_span msg,
                                          const std::array<uint8_t, Aes256Ctr::KEY_LENGTH>& key,
                                          const std::array<uint8_t, Aes256Ctr::IV_LENGTH>& iv) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  EXPECT_NE(ctx, nullptr);
  if (!ctx)
    return {};

  EXPECT_EQ(EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key.data(), iv.data()), 1);

  std::vector<uint8_t> out(msg.size());
  int outl = 0;
  EXPECT_EQ(EVP_EncryptUpdate(ctx, out.data(), &outl, msg.data(), static_cast<int>(msg.size())), 1);

  int tail = 0;
  EXPECT_EQ(EVP_EncryptFinal_ex(ctx, out.data() + outl, &tail), 1);

  EVP_CIPHER_CTX_free(ctx);
  out.resize(static_cast<size_t>(outl + tail));
  return out;
}

/* ----------------------------- Encrypt Tests ----------------------------- */

/** @test Vector encrypt matches OpenSSL reference output. */
TEST(Aes256CtrTest, BasicVectorEncrypt) {
  std::string plaintext = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  auto expected = computeRefCtr(pt, K_TEST_KEY, K_TEST_IV);

  std::vector<uint8_t> ct;
  ct.reserve(pt.size());
  const auto STATUS = aes256CtrEncrypt(pt, K_TEST_KEY, K_TEST_IV, ct);

  EXPECT_EQ(STATUS, Aes256Ctr::Status::SUCCESS);
  EXPECT_EQ(ct.size(), expected.size());
  EXPECT_TRUE(std::equal(ct.begin(), ct.end(), expected.begin()));
}

/** @test Buffer encrypt matches vector API output. */
TEST(Aes256CtrTest, BasicBufferEncrypt) {
  std::string plaintext = "Buffer API test";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  auto expected = computeRefCtr(pt, K_TEST_KEY, K_TEST_IV);

  uint8_t buf[128] = {};
  size_t len = sizeof(buf);
  const auto STATUS = aes256CtrEncrypt(pt, K_TEST_KEY, K_TEST_IV, buf, len);

  EXPECT_EQ(STATUS, Aes256Ctr::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/** @test Empty plaintext produces zero-length ciphertext. */
TEST(Aes256CtrTest, EmptyPlaintext) {
  std::vector<uint8_t> pt;

  // Vector API
  std::vector<uint8_t> ct;
  const auto ST_VEC = aes256CtrEncrypt(pt, K_TEST_KEY, K_TEST_IV, ct);

  EXPECT_EQ(ST_VEC, Aes256Ctr::Status::SUCCESS);
  EXPECT_EQ(ct.size(), 0U);

  // Buffer API
  uint8_t buf[16] = {};
  size_t len = sizeof(buf);
  const auto ST_BUF = aes256CtrEncrypt(pt, K_TEST_KEY, K_TEST_IV, buf, len);

  EXPECT_EQ(ST_BUF, Aes256Ctr::Status::SUCCESS);
  EXPECT_EQ(len, 0U);
}

/* ----------------------------- Decrypt Tests ----------------------------- */

/** @test Vector decrypt recovers the original plaintext. */
TEST(Aes256CtrTest, BasicVectorDecrypt) {
  std::string plaintext = "Stream cipher test!";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  auto ct = computeRefCtr(pt, K_TEST_KEY, K_TEST_IV);

  std::vector<uint8_t> out;
  out.reserve(ct.size());
  const auto STATUS = aes256CtrDecrypt(ct, K_TEST_KEY, K_TEST_IV, out);

  EXPECT_EQ(STATUS, Aes256Ctr::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid key length returns ERROR_INVALID_KEY. */
TEST(Aes256CtrTest, InvalidKey) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, Aes256Ctr::KEY_LENGTH / 2> shortKey{};
  std::vector<uint8_t> out;

  const auto STATUS = aes256CtrEncrypt(pt, shortKey, K_TEST_IV, out);

  EXPECT_EQ(STATUS, Aes256Ctr::Status::ERROR_INVALID_KEY);
}

/** @test Invalid IV length returns ERROR_INVALID_IV. */
TEST(Aes256CtrTest, InvalidIv) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, Aes256Ctr::IV_LENGTH / 2> shortIv{};
  std::vector<uint8_t> out;

  const auto STATUS = aes256CtrEncrypt(pt, K_TEST_KEY, shortIv, out);

  EXPECT_EQ(STATUS, Aes256Ctr::Status::ERROR_INVALID_IV);
}
