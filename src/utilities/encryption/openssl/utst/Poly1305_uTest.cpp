/**
 * @file Poly1305_uTest.cpp
 * @brief Unit tests for apex::encryption::Poly1305.
 *
 * Notes:
 *  - OpenSSL EVP_MAC("POLY1305") is used as oracle to validate tag bytes and length.
 *  - Tests cover vector API, buffer API, and key validation.
 */

#include "src/utilities/encryption/openssl/inc/Poly1305.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <string>
#include <vector>

using apex::encryption::Poly1305;

/* ----------------------------- Test Fixtures ----------------------------- */

/// RFC 7539 test vector key.
static constexpr std::array<uint8_t, Poly1305::KEY_LENGTH> K_RFC_KEY = {
    0x85, 0xd6, 0xbe, 0x78, 0x57, 0x55, 0x6d, 0x33, 0x7f, 0x44, 0x52, 0xfe, 0x42, 0xd5, 0x06, 0x7c,
    0x30, 0x3c, 0x1f, 0x64, 0x42, 0xaa, 0x90, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const std::string K_RFC_MSG = "Cryptographic Forum Research Group";

/* ----------------------------- File Helpers ----------------------------- */

/// Compute reference Poly1305 with OpenSSL EVP_MAC.
static uint8_t computeExpectedPoly1305(const std::vector<uint8_t>& msg,
                                       const std::array<uint8_t, Poly1305::KEY_LENGTH>& key,
                                       std::array<uint8_t, Poly1305::DIGEST_LENGTH>& outTag) {
  const EVP_MAC* macAlg = EVP_MAC_fetch(nullptr, "POLY1305", nullptr);
  if (!macAlg)
    return 1;

  EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(const_cast<EVP_MAC*>(macAlg));
  EVP_MAC_free(const_cast<EVP_MAC*>(macAlg));
  if (!ctx)
    return 2;

  if (EVP_MAC_init(ctx, key.data(), key.size(), nullptr) <= 0) {
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

/** @test Vector API reproduces the RFC 7539 Poly1305 tag. */
TEST(Poly1305Test, VectorApi) {
  std::vector<uint8_t> message(K_RFC_MSG.begin(), K_RFC_MSG.end());
  std::array<uint8_t, Poly1305::DIGEST_LENGTH> expected{};
  ASSERT_EQ(0, computeExpectedPoly1305(message, K_RFC_KEY, expected));

  Poly1305 mac;
  mac.setKey(K_RFC_KEY);
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = mac.mac(message, out);

  EXPECT_EQ(STATUS, Poly1305::Status::SUCCESS);
  EXPECT_EQ(out.size(), expected.size());
  EXPECT_TRUE(std::equal(out.begin(), out.end(), expected.begin()));
}

/** @test Buffer API writes the RFC tag correctly into a caller buffer. */
TEST(Poly1305Test, BufferApi) {
  std::vector<uint8_t> message(K_RFC_MSG.begin(), K_RFC_MSG.end());
  std::array<uint8_t, Poly1305::DIGEST_LENGTH> expected{};
  ASSERT_EQ(0, computeExpectedPoly1305(message, K_RFC_KEY, expected));

  Poly1305 mac;
  mac.setKey(K_RFC_KEY);
  uint8_t buf[EVP_MAX_MD_SIZE] = {};
  size_t len = EVP_MAX_MD_SIZE;
  const auto STATUS = mac.mac(message, buf, len);

  EXPECT_EQ(STATUS, Poly1305::Status::SUCCESS);
  EXPECT_EQ(len, expected.size());
  EXPECT_TRUE(std::equal(buf, buf + len, expected.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid key length returns ERROR_INVALID_KEY (no computation performed). */
TEST(Poly1305Test, InvalidKey) {
  std::array<uint8_t, Poly1305::KEY_LENGTH / 2> shortKey{};

  Poly1305 mac;
  mac.setKey(std::vector<uint8_t>(shortKey.begin(), shortKey.end()));
  std::vector<uint8_t> out;
  out.reserve(EVP_MAX_MD_SIZE);
  const auto STATUS = mac.mac(std::vector<uint8_t>{}, out);

  EXPECT_EQ(STATUS, Poly1305::Status::ERROR_INVALID_KEY);
}
