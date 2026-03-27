/**
 * @file Aes128Gcm_uTest.cpp
 * @brief Unit tests for apex::encryption::Aes128Gcm.
 *
 * Notes:
 *  - Uses OpenSSL EVP (EVP_aes_128_gcm) as oracle for expected output.
 *  - Tests cover vector API, buffer API, empty input, key/IV validation.
 */

#include "src/utilities/encryption/openssl/inc/Aes128Gcm.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <string>
#include <vector>

#include "compat_span.hpp"

using apex::encryption::Aes128Gcm;
using apex::encryption::aes128GcmDecrypt;
using apex::encryption::aes128GcmEncrypt;

/* ----------------------------- Test Fixtures ----------------------------- */

/** 128-bit ASCII key: "0123456789012345" */
static constexpr std::array<uint8_t, Aes128Gcm::KEY_LENGTH> K_TEST_KEY = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5'};

/** 96-bit ASCII IV: "012345678901" */
static constexpr std::array<uint8_t, Aes128Gcm::IV_LENGTH> K_TEST_IV = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'};

/// Helper: compute reference AES-128-GCM ciphertext + tag via OpenSSL EVP.
static void computeRefGcm128(apex::compat::bytes_span plaintext, apex::compat::bytes_span aad,
                             const std::array<uint8_t, Aes128Gcm::KEY_LENGTH>& key,
                             const std::array<uint8_t, Aes128Gcm::IV_LENGTH>& iv,
                             std::vector<uint8_t>& out, std::vector<uint8_t>& tag) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  ASSERT_NE(ctx, nullptr);

  EXPECT_EQ(EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, key.data(), iv.data()), 1);

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

  tag.resize(Aes128Gcm::TAG_LENGTH);
  EXPECT_EQ(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, Aes128Gcm::TAG_LENGTH, tag.data()), 1);

  EVP_CIPHER_CTX_free(ctx);
  out.resize(static_cast<size_t>(outl));
}

/* ----------------------------- Encrypt Tests ----------------------------- */

/** @test Vector encrypt matches OpenSSL reference output (ct + tag). */
TEST(Aes128GcmTest, BasicVectorEncrypt) {
  std::string plaintext = "AES-128-GCM test message";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::string aadStr = "header";
  std::vector<uint8_t> aad(aadStr.begin(), aadStr.end());

  std::vector<uint8_t> expectedCt, expectedTag;
  computeRefGcm128(pt, aad, K_TEST_KEY, K_TEST_IV, expectedCt, expectedTag);

  std::vector<uint8_t> ct, tag;
  const auto STATUS = aes128GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag);

  EXPECT_EQ(STATUS, Aes128Gcm::Status::SUCCESS);
  EXPECT_EQ(ct.size(), expectedCt.size());
  EXPECT_TRUE(std::equal(ct.begin(), ct.end(), expectedCt.begin()));
  EXPECT_EQ(tag.size(), expectedTag.size());
  EXPECT_TRUE(std::equal(tag.begin(), tag.end(), expectedTag.begin()));
}

/** @test Empty plaintext yields empty ciphertext and a valid tag. */
TEST(Aes128GcmTest, EmptyPlaintext) {
  std::vector<uint8_t> pt;
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  const auto STATUS = aes128GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag);

  EXPECT_EQ(STATUS, Aes128Gcm::Status::SUCCESS);
  EXPECT_TRUE(ct.empty());
  EXPECT_EQ(tag.size(), Aes128Gcm::TAG_LENGTH);
}

/* ----------------------------- Decrypt Tests ----------------------------- */

/** @test Vector decrypt recovers original plaintext. */
TEST(Aes128GcmTest, BasicVectorDecrypt) {
  std::string plaintext = "Smaller key, same security for many use cases!";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(aes128GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag), Aes128Gcm::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = aes128GcmDecrypt(ct, aad, K_TEST_KEY, K_TEST_IV, out, tag);

  EXPECT_EQ(STATUS, Aes128Gcm::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/** @test Roundtrip with AAD. */
TEST(Aes128GcmTest, RoundtripWithAad) {
  std::string plaintext = "Data with AAD";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::string aadStr = "associated data";
  std::vector<uint8_t> aad(aadStr.begin(), aadStr.end());

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(aes128GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag), Aes128Gcm::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = aes128GcmDecrypt(ct, aad, K_TEST_KEY, K_TEST_IV, out, tag);

  EXPECT_EQ(STATUS, Aes128Gcm::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

// Note: Invalid key/IV length tests are not needed because the template
// functions use static_assert to enforce correct sizes at compile time.

/** @test Tampered ciphertext/tag triggers ERROR_AUTH on decrypt. */
TEST(Aes128GcmTest, InvalidTag) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> aad;
  std::vector<uint8_t> ct, tag;

  ASSERT_EQ(aes128GcmEncrypt(pt, aad, K_TEST_KEY, K_TEST_IV, ct, tag), Aes128Gcm::Status::SUCCESS);

  ct[0] ^= 0xFF;
  tag[0] ^= 0xFF;

  std::vector<uint8_t> out;
  const auto STATUS = aes128GcmDecrypt(ct, aad, K_TEST_KEY, K_TEST_IV, out, tag);

  EXPECT_EQ(STATUS, Aes128Gcm::Status::ERROR_AUTH);
}
