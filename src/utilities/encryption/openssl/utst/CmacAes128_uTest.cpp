/**
 * @file CmacAes128_uTest.cpp
 * @brief Unit tests for apex::encryption::CmacAes128.
 *
 * Notes:
 *  - OpenSSL EVP_MAC("CMAC") with OSSL_MAC_PARAM_CIPHER="AES-128-CBC" as oracle.
 *  - Tests cover vector API, buffer API, and key validation.
 */

#include "src/utilities/encryption/openssl/inc/CmacAes128.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <string>
#include <vector>

using apex::encryption::CmacAes128;

/* ----------------------------- File Helpers ----------------------------- */

/// Compute reference CMAC with OpenSSL EVP_MAC.
static uint8_t computeExpectedCmac(const std::vector<uint8_t>& msg,
                                   const std::array<uint8_t, CmacAes128::KEY_LENGTH>& key,
                                   std::array<uint8_t, CmacAes128::DIGEST_LENGTH>& outTag) {
  const EVP_MAC* alg = EVP_MAC_fetch(nullptr, "CMAC", nullptr);
  if (!alg)
    return 1;

  EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(const_cast<EVP_MAC*>(alg));
  EVP_MAC_free(const_cast<EVP_MAC*>(alg));
  if (!ctx)
    return 2;

  OSSL_PARAM params[2];
  params[0] =
      OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_CIPHER, const_cast<char*>("AES-128-CBC"), 0);
  params[1] = OSSL_PARAM_construct_end();

  if (EVP_MAC_init(ctx, key.data(), key.size(), params) <= 0) {
    EVP_MAC_CTX_free(ctx);
    return 3;
  }
  if (!msg.empty()) {
    if (EVP_MAC_update(ctx, msg.data(), msg.size()) <= 0) {
      EVP_MAC_CTX_free(ctx);
      return 4;
    }
  }

  size_t len = outTag.size();
  if (EVP_MAC_final(ctx, outTag.data(), &len, len) <= 0) {
    EVP_MAC_CTX_free(ctx);
    return 5;
  }
  EVP_MAC_CTX_free(ctx);
  return 0;
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Vector API matches OpenSSL CMAC for a classic pangram. */
TEST(CmacAes128Test, BasicVectorCmac) {
  std::array<uint8_t, CmacAes128::KEY_LENGTH> key{};
  for (size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<uint8_t>(i);

  std::string msgStr = "The quick brown fox jumps over the lazy dog";
  std::vector<uint8_t> message(msgStr.begin(), msgStr.end());

  std::array<uint8_t, CmacAes128::DIGEST_LENGTH> expected{};
  ASSERT_EQ(0, computeExpectedCmac(message, key, expected));

  CmacAes128 cm;
  cm.setKey(key);
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = cm.mac(message, out);

  EXPECT_EQ(STATUS, CmacAes128::Status::SUCCESS);
  EXPECT_EQ(out.size(), expected.size());
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API returns same bytes and correct length for empty message. */
TEST(CmacAes128Test, BasicBufferCmac) {
  std::array<uint8_t, CmacAes128::KEY_LENGTH> key{};
  for (size_t i = 0; i < key.size(); ++i)
    key[i] = static_cast<uint8_t>(i);

  std::vector<uint8_t> emptyMsg;

  std::array<uint8_t, CmacAes128::DIGEST_LENGTH> expected{};
  ASSERT_EQ(0, computeExpectedCmac(emptyMsg, key, expected));

  CmacAes128 cm;
  cm.setKey(key);

  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = cm.mac(emptyMsg, buf, len);

  EXPECT_EQ(STATUS, CmacAes128::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid key length returns ERROR_INVALID_KEY (no computation performed). */
TEST(CmacAes128Test, InvalidKey) {
  std::array<uint8_t, CmacAes128::KEY_LENGTH / 2> shortKey{};

  CmacAes128 cm;
  cm.setKey(std::vector<uint8_t>(shortKey.begin(), shortKey.end()));
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = cm.mac(std::vector<uint8_t>{}, out);

  EXPECT_EQ(STATUS, CmacAes128::Status::ERROR_INVALID_KEY);
}
