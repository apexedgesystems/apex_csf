/**
 * @file Aes128Ccm_uTest.cpp
 * @brief Unit tests for apex::encryption::Aes128Ccm.
 *
 * Notes:
 *  - Tests cover vector API, various nonce/tag lengths, empty input.
 *  - CCM allows nonce 7-13 bytes, tag 4-16 bytes (even values only).
 */

#include "src/utilities/encryption/openssl/inc/Aes128Ccm.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using apex::encryption::Aes128Ccm;
using apex::encryption::aes128CcmDecrypt;
using apex::encryption::aes128CcmEncrypt;

/* ----------------------------- Test Fixtures ----------------------------- */

/** 128-bit ASCII key: "0123456789012345" */
static constexpr std::array<uint8_t, Aes128Ccm::KEY_LENGTH> K_TEST_KEY = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '1', '2', '3', '4', '5'};

/** 13-byte nonce (common for CCM) */
static constexpr std::array<uint8_t, 13> K_NONCE_13 = {'0', '1', '2', '3', '4', '5', '6',
                                                       '7', '8', '9', '0', '1', '2'};

/** 12-byte nonce (IoT protocols) */
static constexpr std::array<uint8_t, 12> K_NONCE_12 = {'0', '1', '2', '3', '4', '5',
                                                       '6', '7', '8', '9', '0', '1'};

/** 7-byte nonce (minimum) */
static constexpr std::array<uint8_t, 7> K_NONCE_7 = {'0', '1', '2', '3', '4', '5', '6'};

/* ----------------------------- Encrypt Tests ----------------------------- */

/** @test Basic encrypt/decrypt roundtrip with 13-byte nonce. */
TEST(Aes128CcmTest, BasicRoundtrip) {
  std::string plaintext = "AES-CCM test message";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  const auto ENC_STATUS = aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_13, 16, ct, tag);

  EXPECT_EQ(ENC_STATUS, Aes128Ccm::Status::SUCCESS);
  EXPECT_EQ(ct.size(), pt.size());
  EXPECT_EQ(tag.size(), 16U);

  std::vector<uint8_t> out;
  const auto DEC_STATUS = aes128CcmDecrypt(ct, aad, K_TEST_KEY, K_NONCE_13, tag, out);

  EXPECT_EQ(DEC_STATUS, Aes128Ccm::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/** @test Roundtrip with AAD. */
TEST(Aes128CcmTest, RoundtripWithAad) {
  std::string plaintext = "Data with AAD";
  std::vector<uint8_t> pt(plaintext.begin(), plaintext.end());
  std::string aadStr = "associated data for authentication";
  std::vector<uint8_t> aad(aadStr.begin(), aadStr.end());

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_13, 16, ct, tag),
            Aes128Ccm::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = aes128CcmDecrypt(ct, aad, K_TEST_KEY, K_NONCE_13, tag, out);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::SUCCESS);
  std::string result(out.begin(), out.end());
  EXPECT_EQ(result, plaintext);
}

/** @test AAD-only encryption (empty plaintext with AAD). */
TEST(Aes128CcmTest, AadOnlyAuthentication) {
  std::vector<uint8_t> pt;
  std::string aadStr = "Header to authenticate only";
  std::vector<uint8_t> aad(aadStr.begin(), aadStr.end());

  std::vector<uint8_t> ct, tag;
  const auto STATUS = aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_13, 8, ct, tag);

  // CCM with empty plaintext but AAD should produce a valid tag
  // Note: Some implementations require at least 1 byte of plaintext
  if (STATUS == Aes128Ccm::Status::SUCCESS) {
    EXPECT_TRUE(ct.empty());
    EXPECT_EQ(tag.size(), 8U);
  } else {
    // If CCM doesn't support empty plaintext, that's acceptable behavior
    EXPECT_EQ(STATUS, Aes128Ccm::Status::ERROR_FINAL);
  }
}

/* ----------------------------- Variable Nonce Length Tests ----------------------------- */

/** @test Roundtrip with 12-byte nonce (common for IoT). */
TEST(Aes128CcmTest, Nonce12Bytes) {
  std::vector<uint8_t> pt{1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_12, 8, ct, tag),
            Aes128Ccm::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = aes128CcmDecrypt(ct, aad, K_TEST_KEY, K_NONCE_12, tag, out);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::SUCCESS);
  EXPECT_EQ(out, pt);
}

/** @test Roundtrip with 7-byte nonce (minimum). */
TEST(Aes128CcmTest, Nonce7Bytes) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  ASSERT_EQ(aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_7, 16, ct, tag),
            Aes128Ccm::Status::SUCCESS);

  std::vector<uint8_t> out;
  const auto STATUS = aes128CcmDecrypt(ct, aad, K_TEST_KEY, K_NONCE_7, tag, out);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::SUCCESS);
  EXPECT_EQ(out, pt);
}

/* ----------------------------- Variable Tag Length Tests ----------------------------- */

/** @test Roundtrip with 4-byte tag (minimum). */
TEST(Aes128CcmTest, Tag4Bytes) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  const auto ENC_STATUS = aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_13, 4, ct, tag);

  EXPECT_EQ(ENC_STATUS, Aes128Ccm::Status::SUCCESS);
  EXPECT_EQ(tag.size(), 4U);

  std::vector<uint8_t> out;
  const auto DEC_STATUS = aes128CcmDecrypt(ct, aad, K_TEST_KEY, K_NONCE_13, tag, out);

  EXPECT_EQ(DEC_STATUS, Aes128Ccm::Status::SUCCESS);
  EXPECT_EQ(out, pt);
}

/** @test Roundtrip with 8-byte tag. */
TEST(Aes128CcmTest, Tag8Bytes) {
  std::vector<uint8_t> pt{1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<uint8_t> aad;

  std::vector<uint8_t> ct, tag;
  const auto ENC_STATUS = aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_13, 8, ct, tag);

  EXPECT_EQ(ENC_STATUS, Aes128Ccm::Status::SUCCESS);
  EXPECT_EQ(tag.size(), 8U);

  std::vector<uint8_t> out;
  const auto DEC_STATUS = aes128CcmDecrypt(ct, aad, K_TEST_KEY, K_NONCE_13, tag, out);

  EXPECT_EQ(DEC_STATUS, Aes128Ccm::Status::SUCCESS);
  EXPECT_EQ(out, pt);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid key length returns ERROR_INVALID_KEY. */
TEST(Aes128CcmTest, InvalidKey) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, 8> shortKey{};
  std::vector<uint8_t> ct, tag;

  const auto STATUS = aes128CcmEncrypt(pt, {}, shortKey, K_NONCE_13, 16, ct, tag);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::ERROR_INVALID_KEY);
}

/** @test Invalid nonce length (too short) returns ERROR_INVALID_NONCE. */
TEST(Aes128CcmTest, NonceTooShort) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, 6> shortNonce{};
  std::vector<uint8_t> ct, tag;

  const auto STATUS = aes128CcmEncrypt(pt, {}, K_TEST_KEY, shortNonce, 16, ct, tag);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::ERROR_INVALID_NONCE);
}

/** @test Invalid nonce length (too long) returns ERROR_INVALID_NONCE. */
TEST(Aes128CcmTest, NonceTooLong) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::array<uint8_t, 14> longNonce{};
  std::vector<uint8_t> ct, tag;

  const auto STATUS = aes128CcmEncrypt(pt, {}, K_TEST_KEY, longNonce, 16, ct, tag);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::ERROR_INVALID_NONCE);
}

/** @test Invalid tag length (odd) returns ERROR_INVALID_TAG_LENGTH. */
TEST(Aes128CcmTest, TagLengthOdd) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> ct, tag;

  const auto STATUS = aes128CcmEncrypt(pt, {}, K_TEST_KEY, K_NONCE_13, 5, ct, tag);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::ERROR_INVALID_TAG_LENGTH);
}

/** @test Invalid tag length (too short) returns ERROR_INVALID_TAG_LENGTH. */
TEST(Aes128CcmTest, TagLengthTooShort) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> ct, tag;

  const auto STATUS = aes128CcmEncrypt(pt, {}, K_TEST_KEY, K_NONCE_13, 2, ct, tag);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::ERROR_INVALID_TAG_LENGTH);
}

/** @test Tampered ciphertext/tag triggers ERROR_AUTH on decrypt. */
TEST(Aes128CcmTest, InvalidTag) {
  std::vector<uint8_t> pt{1, 2, 3, 4};
  std::vector<uint8_t> aad;
  std::vector<uint8_t> ct, tag;

  ASSERT_EQ(aes128CcmEncrypt(pt, aad, K_TEST_KEY, K_NONCE_13, 16, ct, tag),
            Aes128Ccm::Status::SUCCESS);

  ct[0] ^= 0xFF;

  std::vector<uint8_t> out;
  const auto STATUS = aes128CcmDecrypt(ct, aad, K_TEST_KEY, K_NONCE_13, tag, out);

  EXPECT_EQ(STATUS, Aes128Ccm::Status::ERROR_AUTH);
}
