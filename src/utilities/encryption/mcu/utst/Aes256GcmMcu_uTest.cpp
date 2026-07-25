/**
 * @file Aes256GcmMcu_uTest.cpp
 * @brief Unit tests for standalone AES-256-GCM (encryption_mcu).
 *
 * Known-answer vectors come from the shared table in
 * Aes256GcmKatVectors.hpp (FIPS 197 + NIST SP 800-38D), which also pins
 * the sibling C implementation in the c2000 encryptor demo.
 */

#include "src/utilities/encryption/mcu/inc/Aes256GcmMcu.hpp"
#include "src/utilities/encryption/mcu/utst/Aes256GcmKatVectors.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using apex::encryption::mcu::aes256GcmDecrypt;
using apex::encryption::mcu::aes256GcmEncrypt;
using apex::encryption::mcu::GcmResult;
using apex::encryption::mcu::GcmStatus;

namespace kat = apex::encryption::mcu::kat;

/* ----------------------------- AES Block Tests ----------------------------- */

/** @test Verify AES-256 block encryption against FIPS 197 Appendix C.3. */
TEST(Aes256GcmMcu_AesBlock, FIPS197_AppendixC3) {
  uint32_t rk[60];
  apex::encryption::mcu::detail::aes256KeyExpand(kat::FIPS197_C3_KEY, rk);

  uint8_t ct[16];
  apex::encryption::mcu::detail::aesEncryptBlock(rk, kat::FIPS197_C3_PT, ct);

  EXPECT_EQ(0, std::memcmp(ct, kat::FIPS197_C3_CT, 16));
}

/* ----------------------------- GCM Known-Answer Tests ----------------------------- */

/** @test Encrypt matches every shared NIST vector (ciphertext and tag). */
TEST(Aes256GcmMcu_Kat, EncryptMatchesNistVectors) {
  for (int k = 0; k < kat::AES256_GCM_KAT_COUNT; ++k) {
    const kat::Aes256GcmKat& KAT = kat::AES256_GCM_KATS[k];
    SCOPED_TRACE(KAT.name);

    uint8_t ct[64];
    uint8_t tag[16];
    GcmResult r = aes256GcmEncrypt(KAT.key, KAT.nonce, KAT.pt, KAT.ptLen, KAT.aad, KAT.aadLen,
                                   KAT.ptLen > 0 ? ct : nullptr, tag);

    EXPECT_EQ(GcmStatus::OK, r.status);
    EXPECT_EQ(KAT.ptLen, r.bytesWritten);
    if (KAT.ptLen > 0) {
      EXPECT_EQ(0, std::memcmp(ct, KAT.ct, KAT.ptLen));
    }
    EXPECT_EQ(0, std::memcmp(tag, KAT.tag, 16));
  }
}

/** @test Decrypt of every shared NIST vector authenticates and roundtrips. */
TEST(Aes256GcmMcu_Kat, DecryptMatchesNistVectors) {
  for (int k = 0; k < kat::AES256_GCM_KAT_COUNT; ++k) {
    const kat::Aes256GcmKat& KAT = kat::AES256_GCM_KATS[k];
    SCOPED_TRACE(KAT.name);

    uint8_t pt[64];
    GcmResult r = aes256GcmDecrypt(KAT.key, KAT.nonce, KAT.ct, KAT.ptLen, KAT.aad, KAT.aadLen,
                                   KAT.tag, KAT.ptLen > 0 ? pt : nullptr);

    EXPECT_EQ(GcmStatus::OK, r.status);
    EXPECT_EQ(KAT.ptLen, r.bytesWritten);
    if (KAT.ptLen > 0) {
      EXPECT_EQ(0, std::memcmp(pt, KAT.pt, KAT.ptLen));
    }
  }
}

/* ----------------------------- Auth Failure Tests ----------------------------- */

/** @test Authentication fails when tag is modified. */
TEST(Aes256GcmMcu_AuthFailure, ModifiedTag) {
  const uint8_t KEY[32] = {0};
  const uint8_t NONCE[12] = {0};
  const uint8_t CT[16] = {0xCE, 0xA7, 0x40, 0x3D, 0x4D, 0x60, 0x6B, 0x6E,
                          0x07, 0x4E, 0xC5, 0xD3, 0xBA, 0xF3, 0x9D, 0x18};
  uint8_t badTag[16] = {0xD0, 0xD1, 0xC8, 0xA7, 0x99, 0x99, 0x6B, 0xF0,
                        0x26, 0x5B, 0x98, 0xB5, 0xD4, 0x8A, 0xB9, 0x19};
  badTag[0] ^= 0x01; // Flip one bit

  uint8_t pt[16];
  GcmResult r = aes256GcmDecrypt(KEY, NONCE, CT, 16, nullptr, 0, badTag, pt);

  EXPECT_EQ(GcmStatus::ERROR_AUTH_FAILED, r.status);
  EXPECT_EQ(0U, r.bytesWritten);

  // Plaintext buffer should be zeroed on auth failure
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(0, pt[i]);
  }
}

/** @test Authentication fails when ciphertext is modified. */
TEST(Aes256GcmMcu_AuthFailure, ModifiedCiphertext) {
  const uint8_t KEY[32] = {0};
  const uint8_t NONCE[12] = {0};
  uint8_t ct[16] = {0xCE, 0xA7, 0x40, 0x3D, 0x4D, 0x60, 0x6B, 0x6E,
                    0x07, 0x4E, 0xC5, 0xD3, 0xBA, 0xF3, 0x9D, 0x18};
  ct[8] ^= 0x01; // Flip one bit in ciphertext
  const uint8_t TAG[16] = {0xD0, 0xD1, 0xC8, 0xA7, 0x99, 0x99, 0x6B, 0xF0,
                           0x26, 0x5B, 0x98, 0xB5, 0xD4, 0x8A, 0xB9, 0x19};

  uint8_t pt[16];
  GcmResult r = aes256GcmDecrypt(KEY, NONCE, ct, 16, nullptr, 0, TAG, pt);

  EXPECT_EQ(GcmStatus::ERROR_AUTH_FAILED, r.status);
}

/* ----------------------------- Null Pointer Tests ----------------------------- */

/** @test Null key returns ERROR_NULL_POINTER. */
TEST(Aes256GcmMcu_NullPointer, NullKey) {
  const uint8_t NONCE[12] = {0};
  uint8_t tag[16];
  GcmResult r = aes256GcmEncrypt(nullptr, NONCE, nullptr, 0, nullptr, 0, nullptr, tag);
  EXPECT_EQ(GcmStatus::ERROR_NULL_POINTER, r.status);
}

/** @test Null nonce returns ERROR_NULL_POINTER. */
TEST(Aes256GcmMcu_NullPointer, NullNonce) {
  const uint8_t KEY[32] = {0};
  uint8_t tag[16];
  GcmResult r = aes256GcmEncrypt(KEY, nullptr, nullptr, 0, nullptr, 0, nullptr, tag);
  EXPECT_EQ(GcmStatus::ERROR_NULL_POINTER, r.status);
}

/* ----------------------------- Roundtrip Tests ----------------------------- */

/** @test Encrypt then decrypt with random-ish data verifies roundtrip. */
TEST(Aes256GcmMcu_Roundtrip, ArbitraryData) {
  uint8_t key[32];
  for (int i = 0; i < 32; ++i) {
    key[i] = static_cast<uint8_t>(i * 7 + 13);
  }

  const uint8_t NONCE[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                             0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

  uint8_t pt[100];
  for (int i = 0; i < 100; ++i) {
    pt[i] = static_cast<uint8_t>(i);
  }

  const uint8_t AAD[10] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33, 0x44};

  uint8_t ct[100];
  uint8_t tag[16];
  GcmResult encResult = aes256GcmEncrypt(key, NONCE, pt, 100, AAD, 10, ct, tag);
  ASSERT_EQ(GcmStatus::OK, encResult.status);
  ASSERT_EQ(100U, encResult.bytesWritten);

  // Ciphertext should differ from plaintext
  EXPECT_NE(0, std::memcmp(pt, ct, 100));

  uint8_t decrypted[100];
  GcmResult decResult = aes256GcmDecrypt(key, NONCE, ct, 100, AAD, 10, tag, decrypted);
  ASSERT_EQ(GcmStatus::OK, decResult.status);
  ASSERT_EQ(100U, decResult.bytesWritten);

  EXPECT_EQ(0, std::memcmp(pt, decrypted, 100));
}

/** @test Single-byte plaintext roundtrip. */
TEST(Aes256GcmMcu_Roundtrip, SingleByte) {
  const uint8_t KEY[32] = {0x42};
  const uint8_t NONCE[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                             0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
  const uint8_t PT[1] = {0xFF};

  uint8_t ct[1];
  uint8_t tag[16];
  GcmResult encResult = aes256GcmEncrypt(KEY, NONCE, PT, 1, nullptr, 0, ct, tag);
  ASSERT_EQ(GcmStatus::OK, encResult.status);

  uint8_t decrypted[1];
  GcmResult decResult = aes256GcmDecrypt(KEY, NONCE, ct, 1, nullptr, 0, tag, decrypted);
  ASSERT_EQ(GcmStatus::OK, decResult.status);
  EXPECT_EQ(PT[0], decrypted[0]);
}

/** @test Nonce uniqueness: same plaintext with different nonces produces different ciphertext. */
TEST(Aes256GcmMcu_Roundtrip, DifferentNonceDifferentCiphertext) {
  const uint8_t KEY[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
                           0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                           0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};
  const uint8_t PT[32] = {0};

  uint8_t nonce1[12] = {0};
  nonce1[11] = 0x01;
  uint8_t nonce2[12] = {0};
  nonce2[11] = 0x02;

  uint8_t ct1[32], ct2[32];
  uint8_t tag1[16], tag2[16];

  aes256GcmEncrypt(KEY, nonce1, PT, 32, nullptr, 0, ct1, tag1);
  aes256GcmEncrypt(KEY, nonce2, PT, 32, nullptr, 0, ct2, tag2);

  EXPECT_NE(0, std::memcmp(ct1, ct2, 32));
  EXPECT_NE(0, std::memcmp(tag1, tag2, 16));
}
