/**
 * @file ChaCha20Poly1305_uTest.cpp
 * @brief Unit tests for apex::encryption::ChaCha20Poly1305.
 *
 * Notes:
 *  - Uses OpenSSL EVP (EVP_chacha20_poly1305) as oracle for expected output.
 *  - Tests cover vector API, buffer API, empty input, key/nonce validation.
 */

#include "src/utilities/encryption/openssl/inc/ChaCha20Poly1305.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <string>
#include <vector>

#include "compat_span.hpp"

using apex::encryption::ChaCha20Poly1305;
using apex::encryption::chacha20Poly1305Decrypt;
using apex::encryption::chacha20Poly1305Encrypt;

/* ----------------------------- Test Fixtures ----------------------------- */

/** 256-bit ASCII key: "01234567890123456789012345678901" */
static constexpr std::array<uint8_t, ChaCha20Poly1305::KEY_LENGTH> K_TEST_KEY = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'};

/** 96-bit ASCII nonce: "012345678901" */
static constexpr std::array<uint8_t, ChaCha20Poly1305::NONCE_LENGTH> K_TEST_NONCE = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1'};

/// Helper: compute reference ChaCha20-Poly1305 ciphertext + tag via OpenSSL EVP.
static void computeRefChaCha(apex::compat::bytes_span plaintext, apex::compat::bytes_span aad,
                             const std::array<uint8_t, ChaCha20Poly1305::KEY_LENGTH>& key,
                             const std::array<uint8_t, ChaCha20Poly1305::NONCE_LENGTH>& nonce,
                             std::vector<uint8_t>& out, std::vector<uint8_t>& tag) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  ASSERT_NE(ctx, nullptr);

  EXPECT_EQ(EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, key.data(), nonce.data()), 1);

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

  tag.resize(ChaCha20Poly1305::TAG_LENGTH);
  EXPECT_EQ(
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, ChaCha20Poly1305::TAG_LENGTH, tag.data()), 1);

  EVP_CIPHER_CTX_free(ctx);
  out.resize(static_cast<size_t>(outl));
}

/* ----------------------------- Encrypt Tests ----------------------------- */

/** @test Vector encrypt matches OpenSSL reference output (ct + tag). */
TEST(ChaCha20Poly1305Test, BasicVectorEncrypt) {
  std::string plaintext = "ChaCha20-Poly1305 test message";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::string aadStr = "header";
  std::vector<uint8_t> aad(aadStr.begin(), aadStr.end());

  std::vector<uint8_t> expectedCt, expectedTag;
  computeRefChaCha(pt, aad, K_TEST_KEY, K_TEST_NONCE, expectedCt, expectedTag);

  std::vector<uint8_t> ct, tag;
  const auto STATUS = chacha20Poly1305Encrypt(pt, aad, K_TEST_KEY, K_TEST_NONCE, ct, tag);

  EXPECT_EQ(STATUS, ChaCha20Poly1305::Status::SUCCESS);
  EXPECT_EQ(ct.size(), expectedCt.size());
  EXPECT_TRUE(std::equal(ct.begin(), ct.end(), expectedCt.begin()));
  EXPECT_EQ(tag.size(), expectedTag.size());
  EXPECT_TRUE(std::equal(tag.begin(), tag.end(), expectedTag.begin()));
}

/** @test Empty plaintext yields empty ciphertext and a valid tag. */
TEST(ChaCha20Poly1305Test, EmptyPlaintext) {
  std::vector<uint8_t> pt;
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  const auto STATUS = chacha20Poly1305Encrypt(pt, aad, K_TEST_KEY, K_TEST_NONCE, ct, tag);

  EXPECT_EQ(STATUS, ChaCha20Poly1305::Status::SUCCESS);
  EXPECT_TRUE(ct.empty());
  EXPECT_EQ(tag.size(), ChaCha20Poly1305::TAG_LENGTH);
}

/* ----------------------------- Decrypt Tests ----------------------------- */

/** @test Vector decrypt recovers original plaintext. */
TEST(ChaCha20Poly1305Test, BasicVectorDecrypt) {
  std::string plaintext = "Secure message for ARM!";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(chacha20Poly1305Encrypt(pt, aad, K_TEST_KEY, K_TEST_NONCE, ct, tag),
            ChaCha20Poly1305::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = chacha20Poly1305Decrypt(ct, aad, K_TEST_KEY, K_TEST_NONCE, out, tag);

  EXPECT_EQ(STATUS, ChaCha20Poly1305::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/** @test Roundtrip with AAD. */
TEST(ChaCha20Poly1305Test, RoundtripWithAad) {
  std::string plaintext = "Data with AAD";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::string aadStr = "associated data";
  std::vector<uint8_t> aad(aadStr.begin(), aadStr.end());

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(chacha20Poly1305Encrypt(pt, aad, K_TEST_KEY, K_TEST_NONCE, ct, tag),
            ChaCha20Poly1305::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = chacha20Poly1305Decrypt(ct, aad, K_TEST_KEY, K_TEST_NONCE, out, tag);

  EXPECT_EQ(STATUS, ChaCha20Poly1305::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

// Note: Invalid key/nonce length tests are not needed because the template
// functions use static_assert to enforce correct sizes at compile time.

/** @test Tampered ciphertext/tag triggers ERROR_AUTH on decrypt. */
TEST(ChaCha20Poly1305Test, InvalidTag) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> aad;
  std::vector<uint8_t> ct, tag;

  ASSERT_EQ(chacha20Poly1305Encrypt(pt, aad, K_TEST_KEY, K_TEST_NONCE, ct, tag),
            ChaCha20Poly1305::Status::SUCCESS);

  ct[0] ^= 0xFF;
  tag[0] ^= 0xFF;

  std::vector<uint8_t> out;
  const auto STATUS = chacha20Poly1305Decrypt(ct, aad, K_TEST_KEY, K_TEST_NONCE, out, tag);

  EXPECT_EQ(STATUS, ChaCha20Poly1305::Status::ERROR_AUTH);
}
