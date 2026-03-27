/**
 * @file Hkdf_uTest.cpp
 * @brief Unit tests for apex::encryption::Hkdf (RFC 5869).
 *
 * Notes:
 *  - Tests use official RFC 5869 test vectors for correctness.
 *  - Tests cover extract, expand, derive, and edge cases.
 */

#include "src/utilities/encryption/openssl/inc/Hkdf.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using apex::encryption::bytes_span;
using apex::encryption::Hkdf;
using apex::encryption::hkdfSha256;
using apex::encryption::hkdfSha512;

/* ----------------------------- Test Fixtures ----------------------------- */

/// RFC 5869 Test Case 1 IKM (input key material)
static const std::vector<uint8_t> K_RFC5869_IKM1 = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
                                                    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};

/// RFC 5869 Test Case 1 Salt
static const std::vector<uint8_t> K_RFC5869_SALT1 = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                                     0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};

/// RFC 5869 Test Case 1 Info
static const std::vector<uint8_t> K_RFC5869_INFO1 = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
                                                     0xf5, 0xf6, 0xf7, 0xf8, 0xf9};

/// RFC 5869 Test Case 1 Expected PRK
static const std::vector<uint8_t> K_RFC5869_PRK1 = {
    0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf, 0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63,
    0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31, 0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5};

/// RFC 5869 Test Case 1 Expected OKM (42 bytes)
static const std::vector<uint8_t> K_RFC5869_OKM1 = {
    0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36,
    0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56,
    0xec, 0xc4, 0xc5, 0xbf, 0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65};

/* ----------------------------- Basic Tests ----------------------------- */

/** @test HKDF-SHA256 extract produces correct PRK (RFC 5869 Test Case 1). */
TEST(HkdfTest, ExtractSha256) {
  std::vector<uint8_t> prk;
  const auto STATUS = Hkdf::extract(
      Hkdf::HashType::SHA256, bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
      bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()}, prk);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(prk.size(), 32U);
  EXPECT_EQ(prk, K_RFC5869_PRK1);
}

/** @test HKDF-SHA256 expand produces correct OKM (RFC 5869 Test Case 1). */
TEST(HkdfTest, ExpandSha256) {
  std::vector<uint8_t> okm;
  const auto STATUS =
      Hkdf::expand(Hkdf::HashType::SHA256, bytes_span{K_RFC5869_PRK1.data(), K_RFC5869_PRK1.size()},
                   bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, 42, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(okm.size(), 42U);
  EXPECT_EQ(okm, K_RFC5869_OKM1);
}

/** @test HKDF-SHA256 derive (extract+expand) produces correct OKM. */
TEST(HkdfTest, DeriveSha256) {
  std::vector<uint8_t> okm;
  const auto STATUS =
      Hkdf::derive(Hkdf::HashType::SHA256, bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                   bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()},
                   bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, 42, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(okm.size(), 42U);
  EXPECT_EQ(okm, K_RFC5869_OKM1);
}

/** @test Convenience hkdfSha256 function works. */
TEST(HkdfTest, ConvenienceSha256) {
  std::vector<uint8_t> okm;
  const auto STATUS =
      hkdfSha256(bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                 bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()},
                 bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, 42, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(okm, K_RFC5869_OKM1);
}

/* ----------------------------- SHA-512 Tests ----------------------------- */

/** @test HKDF-SHA512 derive produces 64-byte hash size. */
TEST(HkdfTest, DeriveSha512) {
  std::vector<uint8_t> ikm = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> salt = {0x05, 0x06, 0x07, 0x08};
  std::vector<uint8_t> info = {0x09, 0x0a, 0x0b, 0x0c};

  std::vector<uint8_t> okm;
  const auto STATUS =
      hkdfSha512(bytes_span{ikm.data(), ikm.size()}, bytes_span{salt.data(), salt.size()},
                 bytes_span{info.data(), info.size()}, 64, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(okm.size(), 64U);
}

/** @test HKDF-SHA512 hashSize returns 64. */
TEST(HkdfTest, HashSizeSha512) { EXPECT_EQ(Hkdf::hashSize(Hkdf::HashType::SHA512), 64U); }

/* ----------------------------- Edge Cases ----------------------------- */

/** @test Empty salt uses zero-filled salt (per RFC 5869). */
TEST(HkdfTest, EmptySalt) {
  std::vector<uint8_t> okm;
  const auto STATUS =
      Hkdf::derive(Hkdf::HashType::SHA256, bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                   bytes_span{}, // Empty salt
                   bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, 32, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(okm.size(), 32U);
}

/** @test Empty info is valid. */
TEST(HkdfTest, EmptyInfo) {
  std::vector<uint8_t> okm;
  const auto STATUS = Hkdf::derive(
      Hkdf::HashType::SHA256, bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
      bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()}, bytes_span{}, // Empty info
      32, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(okm.size(), 32U);
}

/** @test Zero-length output is valid. */
TEST(HkdfTest, ZeroLengthOutput) {
  std::vector<uint8_t> okm;
  const auto STATUS =
      Hkdf::derive(Hkdf::HashType::SHA256, bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                   bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()},
                   bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, 0, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_TRUE(okm.empty());
}

/** @test Buffer API works correctly. */
TEST(HkdfTest, BufferApi) {
  std::array<uint8_t, 42> buf{};
  std::size_t len = buf.size();

  const auto STATUS =
      Hkdf::derive(Hkdf::HashType::SHA256, bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                   bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()},
                   bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, buf.data(), len);

  EXPECT_EQ(STATUS, Hkdf::Status::SUCCESS);
  EXPECT_EQ(len, 42U);
  EXPECT_TRUE(std::equal(buf.begin(), buf.end(), K_RFC5869_OKM1.begin()));
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Output too long returns ERROR_OUTPUT_TOO_LONG. */
TEST(HkdfTest, OutputTooLong) {
  const std::size_t MAX_SHA256 = 255 * 32; // 8160 bytes
  std::vector<uint8_t> okm;

  const auto STATUS =
      Hkdf::derive(Hkdf::HashType::SHA256, bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                   bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()},
                   bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, MAX_SHA256 + 1, okm);

  EXPECT_EQ(STATUS, Hkdf::Status::ERROR_OUTPUT_TOO_LONG);
}

/** @test maxOutputLength returns correct value. */
TEST(HkdfTest, MaxOutputLength) {
  EXPECT_EQ(Hkdf::maxOutputLength(Hkdf::HashType::SHA256), 255 * 32U);
  EXPECT_EQ(Hkdf::maxOutputLength(Hkdf::HashType::SHA512), 255 * 64U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Same inputs produce same outputs. */
TEST(HkdfTest, Deterministic) {
  std::vector<uint8_t> okm1, okm2;

  EXPECT_EQ(Hkdf::derive(Hkdf::HashType::SHA256,
                         bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                         bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()},
                         bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, 42, okm1),
            Hkdf::Status::SUCCESS);

  EXPECT_EQ(Hkdf::derive(Hkdf::HashType::SHA256,
                         bytes_span{K_RFC5869_IKM1.data(), K_RFC5869_IKM1.size()},
                         bytes_span{K_RFC5869_SALT1.data(), K_RFC5869_SALT1.size()},
                         bytes_span{K_RFC5869_INFO1.data(), K_RFC5869_INFO1.size()}, 42, okm2),
            Hkdf::Status::SUCCESS);

  EXPECT_EQ(okm1, okm2);
}
