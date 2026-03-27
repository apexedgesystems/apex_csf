/**
 * @file Aes256Gcm_uTest.cpp
 * @brief Unit tests for apex::encryption::Aes256Gcm.
 *
 * Notes:
 *  - Uses OpenSSL EVP (EVP_aes_256_gcm) as oracle for expected ciphertext/tag.
 *  - Tests cover vector API, buffer API, empty input, key/IV validation, auth.
 */

#include "src/utilities/encryption/openssl/inc/Aes256Gcm.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <string>
#include <vector>

#include "compat_span.hpp"

using apex::encryption::Aes256Gcm;
using apex::encryption::aes256GcmDecrypt;
using apex::encryption::aes256GcmEncrypt;

/* ----------------------------- Test Fixtures ----------------------------- */

/** 256-bit ASCII key: "01234567890123456789012345678901" */
static constexpr std::array<uint8_t, Aes256Gcm::KEY_LENGTH> K_TEST_KEY = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'};

/** 96-bit ASCII IV: "012345678901" */
static constexpr std::array<uint8_t, Aes256Gcm::IV_LENGTH> K_TEST_IV = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'};

/// Helper: compute reference AES-256-GCM ciphertext + tag via OpenSSL EVP.
static void computeRefGcm(apex::compat::bytes_span plaintext, apex::compat::bytes_span aad,
                          const std::array<uint8_t, Aes256Gcm::KEY_LENGTH>& key,
                          const std::array<uint8_t, Aes256Gcm::IV_LENGTH>& iv,
                          std::vector<uint8_t>& out, std::vector<uint8_t>& tag) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  ASSERT_NE(ctx, nullptr);

  EXPECT_EQ(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), iv.data()), 1);

  int len = 0;
  if (!aad.empty()) {
    EXPECT_EQ(EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())), 1);
  }

  out.resize(plaintext.size());
  int outl = 0;
  EXPECT_EQ(EVP_EncryptUpdate(ctx, out.data(), &outl, plaintext.data(),
                              static_cast<int>(plaintext.size())),
            1);

  EXPECT_EQ(EVP_EncryptFinal_ex(ctx, nullptr, &len), 1);

  tag.resize(Aes256Gcm::TAG_LENGTH);
  EXPECT_EQ(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, Aes256Gcm::TAG_LENGTH, tag.data()), 1);

  EVP_CIPHER_CTX_free(ctx);
  out.resize(static_cast<size_t>(outl));
}

/* ----------------------------- Encrypt Tests ----------------------------- */

/** @test Vector encrypt matches OpenSSL reference output (ct + tag). */
TEST(Aes256GcmTest, BasicVectorEncrypt) {
  std::string plaintext = "The quick brown fox jumps";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::string aadStr = "header";
  std::vector<uint8_t> aad(aadStr.begin(), aadStr.end());

  std::vector<uint8_t> expectedCt, expectedTag;
  computeRefGcm(pt, aad, K_TEST_KEY, K_TEST_IV, expectedCt, expectedTag);

  std::vector<uint8_t> ct, tag;
  const auto STATUS = aes256GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag);

  EXPECT_EQ(STATUS, Aes256Gcm::Status::SUCCESS);
  EXPECT_EQ(ct.size(), expectedCt.size());
  EXPECT_TRUE(std::equal(ct.begin(), ct.end(), expectedCt.begin()));
  EXPECT_EQ(tag.size(), expectedTag.size());
  EXPECT_TRUE(std::equal(tag.begin(), tag.end(), expectedTag.begin()));
}

/** @test Buffer encrypt matches vector API output. */
TEST(Aes256GcmTest, BasicBufferEncrypt) {
  std::string plaintext = "Buffer API test";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());

  std::vector<uint8_t> expectedCt, expectedTag;
  computeRefGcm(pt, {}, K_TEST_KEY, K_TEST_IV, expectedCt, expectedTag);

  uint8_t buf[128] = {};
  size_t len = sizeof(buf);
  uint8_t tagBuf[Aes256Gcm::TAG_LENGTH] = {};
  size_t tagLen = sizeof(tagBuf);

  const auto STATUS = aes256GcmEncrypt(pt, apex::compat::bytes_span{}, K_TEST_KEY, K_TEST_IV, buf,
                                       len, tagBuf, tagLen);

  EXPECT_EQ(STATUS, Aes256Gcm::Status::SUCCESS);
  EXPECT_EQ(len, expectedCt.size());
  EXPECT_EQ(tagLen, expectedTag.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expectedCt.begin()));
  EXPECT_TRUE(std::equal(tagBuf, tagBuf + tagLen, expectedTag.begin()));
}

/** @test Empty plaintext yields empty ciphertext and a valid tag. */
TEST(Aes256GcmTest, EmptyPlaintext) {
  std::vector<uint8_t> pt;
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  const auto ST_VEC = aes256GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag);

  EXPECT_EQ(ST_VEC, Aes256Gcm::Status::SUCCESS);
  EXPECT_TRUE(ct.empty());
  EXPECT_EQ(tag.size(), Aes256Gcm::TAG_LENGTH);

  uint8_t buf[16] = {};
  size_t len = sizeof(buf);
  uint8_t tagBuf[Aes256Gcm::TAG_LENGTH] = {};
  size_t tagLen = sizeof(tagBuf);

  const auto ST_BUF = aes256GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, buf, len, tagBuf, tagLen);

  EXPECT_EQ(ST_BUF, Aes256Gcm::Status::SUCCESS);
  EXPECT_EQ(len, 0U);
  EXPECT_EQ(tagLen, Aes256Gcm::TAG_LENGTH);
}

/* ----------------------------- Decrypt Tests ----------------------------- */

/** @test Vector decrypt recovers original plaintext. */
TEST(Aes256GcmTest, BasicVectorDecrypt) {
  std::string plaintext = "Space comms!";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(aes256GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag), Aes256Gcm::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = aes256GcmDecrypt(ct, aad, K_TEST_KEY, K_TEST_IV, out, tag);

  EXPECT_EQ(STATUS, Aes256Gcm::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid key length returns ERROR_INVALID_KEY. */
TEST(Aes256GcmTest, InvalidKey) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, Aes256Gcm::KEY_LENGTH / 2> shortKey{};
  std::vector<uint8_t> ct, tag;

  const auto STATUS = aes256GcmEncrypt(pt, {}, shortKey, K_TEST_IV, ct, tag);

  EXPECT_EQ(STATUS, Aes256Gcm::Status::ERROR_INVALID_KEY);
}

/** @test Invalid IV length returns ERROR_INVALID_IV. */
TEST(Aes256GcmTest, InvalidIv) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, Aes256Gcm::IV_LENGTH / 2> shortIv{};
  std::vector<uint8_t> ct, tag;

  const auto STATUS = aes256GcmEncrypt(pt, {}, K_TEST_KEY, shortIv, ct, tag);

  EXPECT_EQ(STATUS, Aes256Gcm::Status::ERROR_INVALID_IV);
}

/** @test Tampered ciphertext/tag triggers ERROR_AUTH on decrypt. */
TEST(Aes256GcmTest, InvalidTag) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> aad;
  std::vector<uint8_t> ct, tag;

  ASSERT_EQ(aes256GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag), Aes256Gcm::Status::SUCCESS);

  ct[0] ^= 0xFF;
  tag[0] ^= 0xFF;

  std::vector<uint8_t> out;
  const auto STATUS = aes256GcmDecrypt(ct, aad, K_TEST_KEY, K_TEST_IV, out, tag);

  EXPECT_EQ(STATUS, Aes256Gcm::Status::ERROR_AUTH);
}
