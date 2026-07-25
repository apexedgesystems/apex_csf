/**
 * @file Aes256Gcm_uTest.cpp
 * @brief Host-run equivalence tests for the demo's C AES-256-GCM.
 *
 * The demo carries a local C implementation (inc/aes256gcm.h) because the
 * C28x toolchain is C++03-only and cannot consume encryption_mcu. These
 * tests compile that C header on the host and lock it to the shared
 * implementation two ways:
 *   - the shared NIST known-answer table (Aes256GcmKatVectors.hpp) pins
 *     both implementations to the same expected bytes;
 *   - a sweep across plaintext/AAD length combinations asserts
 *     byte-identical ciphertext and tag against apex::encryption::mcu and
 *     cross-decrypts each side's output with the other.
 *
 * The C implementation stores one byte per uint16_t word (native on C28x
 * where CHAR_BIT is 16); the same masking arithmetic holds on the host, so
 * bytes are widened at the call boundary and narrowed for comparison.
 */

// The C++ header parses first: the C header defines byte-length macros
// (AES256_KEY_LEN and friends) with the same names the C++ header declares
// as typed constants.
#include "src/utilities/encryption/mcu/inc/Aes256GcmMcu.hpp"
#include "src/utilities/encryption/mcu/utst/Aes256GcmKatVectors.hpp"

// The demo header is C compiled here as C++; its C-style casts are correct
// for its own language.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "demos/c2000_encryptor_demo/inc/aes256gcm.h"
#pragma GCC diagnostic pop

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace kat = apex::encryption::mcu::kat;

namespace {

/** Widen packed bytes to the C implementation's byte-per-word layout. */
std::vector<uint16_t> widen(const uint8_t* in, size_t n) {
  std::vector<uint16_t> out(n);
  for (size_t i = 0; i < n; ++i) {
    out[i] = in[i];
  }
  return out;
}

/** Narrow byte-per-word output back to packed bytes. */
std::vector<uint8_t> narrow(const uint16_t* in, size_t n) {
  std::vector<uint8_t> out(n);
  for (size_t i = 0; i < n; ++i) {
    out[i] = static_cast<uint8_t>(in[i] & 0xFFU);
  }
  return out;
}

} // namespace

/* ------------------------- Known-Answer Tests ------------------------- */

/** @test The C implementation reproduces every shared NIST vector. */
TEST(C2000Aes256Gcm_Kat, EncryptMatchesNistVectors) {
  for (int k = 0; k < kat::AES256_GCM_KAT_COUNT; ++k) {
    const kat::Aes256GcmKat& KAT = kat::AES256_GCM_KATS[k];
    SCOPED_TRACE(KAT.name);

    const std::vector<uint16_t> KEY = widen(KAT.key, 32);
    const std::vector<uint16_t> NONCE = widen(KAT.nonce, 12);
    const std::vector<uint16_t> PT = widen(KAT.pt, KAT.ptLen);
    const std::vector<uint16_t> AAD = widen(KAT.aad, KAT.aadLen);

    uint16_t ct[64];
    uint16_t tag[16];
    const int RC =
        aes256gcm_encrypt(KEY.data(), NONCE.data(), KAT.ptLen > 0 ? PT.data() : 0, KAT.ptLen,
                          KAT.aadLen > 0 ? AAD.data() : 0, KAT.aadLen, KAT.ptLen > 0 ? ct : 0, tag);

    EXPECT_EQ(AES_GCM_OK, RC);
    if (KAT.ptLen > 0) {
      EXPECT_EQ(0, std::memcmp(narrow(ct, KAT.ptLen).data(), KAT.ct, KAT.ptLen));
    }
    EXPECT_EQ(0, std::memcmp(narrow(tag, 16).data(), KAT.tag, 16));
  }
}

/** @test The C implementation authenticates and decrypts every shared NIST vector. */
TEST(C2000Aes256Gcm_Kat, DecryptMatchesNistVectors) {
  for (int k = 0; k < kat::AES256_GCM_KAT_COUNT; ++k) {
    const kat::Aes256GcmKat& KAT = kat::AES256_GCM_KATS[k];
    SCOPED_TRACE(KAT.name);

    const std::vector<uint16_t> KEY = widen(KAT.key, 32);
    const std::vector<uint16_t> NONCE = widen(KAT.nonce, 12);
    const std::vector<uint16_t> CT = widen(KAT.ct, KAT.ptLen);
    const std::vector<uint16_t> AAD = widen(KAT.aad, KAT.aadLen);
    const std::vector<uint16_t> TAG = widen(KAT.tag, 16);

    uint16_t pt[64];
    const int RC = aes256gcm_decrypt(KEY.data(), NONCE.data(), KAT.ptLen > 0 ? CT.data() : 0,
                                     KAT.ptLen, KAT.aadLen > 0 ? AAD.data() : 0, KAT.aadLen,
                                     TAG.data(), KAT.ptLen > 0 ? pt : 0);

    EXPECT_EQ(AES_GCM_OK, RC);
    if (KAT.ptLen > 0) {
      EXPECT_EQ(0, std::memcmp(narrow(pt, KAT.ptLen).data(), KAT.pt, KAT.ptLen));
    }
  }
}

/** @test A bit-flipped tag fails authentication in the C implementation. */
TEST(C2000Aes256Gcm_AuthFailure, ModifiedTag) {
  const kat::Aes256GcmKat& KAT = kat::AES256_GCM_KATS[1]; // single-block case

  const std::vector<uint16_t> KEY = widen(KAT.key, 32);
  const std::vector<uint16_t> NONCE = widen(KAT.nonce, 12);
  const std::vector<uint16_t> CT = widen(KAT.ct, KAT.ptLen);
  std::vector<uint16_t> tag = widen(KAT.tag, 16);
  tag[0] ^= 0x01U;

  uint16_t pt[16];
  const int RC =
      aes256gcm_decrypt(KEY.data(), NONCE.data(), CT.data(), KAT.ptLen, 0, 0, tag.data(), pt);

  EXPECT_EQ(AES_GCM_ERR_AUTH, RC);
  for (uint32_t i = 0; i < KAT.ptLen; ++i) {
    EXPECT_EQ(0U, pt[i]);
  }
}

/** @test A bit-flipped ciphertext byte fails authentication in the C implementation. */
TEST(C2000Aes256Gcm_AuthFailure, ModifiedCiphertext) {
  const kat::Aes256GcmKat& KAT = kat::AES256_GCM_KATS[1]; // single-block case

  const std::vector<uint16_t> KEY = widen(KAT.key, 32);
  const std::vector<uint16_t> NONCE = widen(KAT.nonce, 12);
  std::vector<uint16_t> ct = widen(KAT.ct, KAT.ptLen);
  ct[8] ^= 0x01U;
  const std::vector<uint16_t> TAG = widen(KAT.tag, 16);

  uint16_t pt[16];
  const int RC =
      aes256gcm_decrypt(KEY.data(), NONCE.data(), ct.data(), KAT.ptLen, 0, 0, TAG.data(), pt);

  EXPECT_EQ(AES_GCM_ERR_AUTH, RC);
}

/* ---------------------- Cross-Implementation Sweep ---------------------- */

/**
 * @test The C and C++ implementations produce byte-identical ciphertext and
 * tag across sub-block, block-aligned, multi-block, and partial-block
 * plaintext lengths with and without AAD, and each decrypts the other's
 * output.
 */
TEST(C2000Aes256Gcm_CrossImpl, MatchesEncryptionMcuAcrossLengths) {
  const size_t PT_LENS[] = {0, 1, 15, 16, 17, 31, 32, 33, 48, 60, 63, 64, 80};
  const size_t AAD_LENS[] = {0, 1, 16, 20};
  const size_t MAX_LEN = 80;

  uint8_t key[32];
  for (int i = 0; i < 32; ++i) {
    key[i] = static_cast<uint8_t>(i * 11 + 1);
  }
  uint8_t nonce[12];
  for (int i = 0; i < 12; ++i) {
    nonce[i] = static_cast<uint8_t>(i * 29 + 5);
  }
  uint8_t ptBytes[MAX_LEN];
  for (size_t i = 0; i < MAX_LEN; ++i) {
    ptBytes[i] = static_cast<uint8_t>(i * 31 + 7);
  }
  uint8_t aadBytes[20];
  for (int i = 0; i < 20; ++i) {
    aadBytes[i] = static_cast<uint8_t>(i * 17 + 3);
  }

  const std::vector<uint16_t> KEY_W = widen(key, 32);
  const std::vector<uint16_t> NONCE_W = widen(nonce, 12);

  for (size_t p = 0; p < sizeof(PT_LENS) / sizeof(PT_LENS[0]); ++p) {
    for (size_t a = 0; a < sizeof(AAD_LENS) / sizeof(AAD_LENS[0]); ++a) {
      const size_t PT_LEN = PT_LENS[p];
      const size_t AAD_LEN = AAD_LENS[a];
      SCOPED_TRACE(testing::Message() << "ptLen=" << PT_LEN << " aadLen=" << AAD_LEN);

      // C++ implementation
      uint8_t ctCpp[MAX_LEN];
      uint8_t tagCpp[16];
      const apex::encryption::mcu::GcmResult ENC = apex::encryption::mcu::aes256GcmEncrypt(
          key, nonce, PT_LEN > 0 ? ptBytes : nullptr, static_cast<uint32_t>(PT_LEN),
          AAD_LEN > 0 ? aadBytes : nullptr, static_cast<uint32_t>(AAD_LEN),
          PT_LEN > 0 ? ctCpp : nullptr, tagCpp);
      ASSERT_EQ(apex::encryption::mcu::GcmStatus::OK, ENC.status);

      // C implementation
      const std::vector<uint16_t> PT_W = widen(ptBytes, PT_LEN);
      const std::vector<uint16_t> AAD_W = widen(aadBytes, AAD_LEN);
      uint16_t ctC[MAX_LEN];
      uint16_t tagC[16];
      const int RC =
          aes256gcm_encrypt(KEY_W.data(), NONCE_W.data(), PT_LEN > 0 ? PT_W.data() : 0,
                            static_cast<uint32_t>(PT_LEN), AAD_LEN > 0 ? AAD_W.data() : 0,
                            static_cast<uint32_t>(AAD_LEN), PT_LEN > 0 ? ctC : 0, tagC);
      ASSERT_EQ(AES_GCM_OK, RC);

      // Identical ciphertext and tag
      if (PT_LEN > 0) {
        EXPECT_EQ(0, std::memcmp(ctCpp, narrow(ctC, PT_LEN).data(), PT_LEN));
      }
      EXPECT_EQ(0, std::memcmp(tagCpp, narrow(tagC, 16).data(), 16));

      // C++ decrypts the C output
      uint8_t ptOut[MAX_LEN];
      const apex::encryption::mcu::GcmResult DEC = apex::encryption::mcu::aes256GcmDecrypt(
          key, nonce, PT_LEN > 0 ? narrow(ctC, PT_LEN).data() : nullptr,
          static_cast<uint32_t>(PT_LEN), AAD_LEN > 0 ? aadBytes : nullptr,
          static_cast<uint32_t>(AAD_LEN), narrow(tagC, 16).data(), PT_LEN > 0 ? ptOut : nullptr);
      ASSERT_EQ(apex::encryption::mcu::GcmStatus::OK, DEC.status);
      if (PT_LEN > 0) {
        EXPECT_EQ(0, std::memcmp(ptOut, ptBytes, PT_LEN));
      }

      // C decrypts the C++ output
      const std::vector<uint16_t> CT_CPP_W = widen(ctCpp, PT_LEN);
      const std::vector<uint16_t> TAG_CPP_W = widen(tagCpp, 16);
      uint16_t ptOutC[MAX_LEN];
      const int RC_DEC = aes256gcm_decrypt(
          KEY_W.data(), NONCE_W.data(), PT_LEN > 0 ? CT_CPP_W.data() : 0,
          static_cast<uint32_t>(PT_LEN), AAD_LEN > 0 ? AAD_W.data() : 0,
          static_cast<uint32_t>(AAD_LEN), TAG_CPP_W.data(), PT_LEN > 0 ? ptOutC : 0);
      ASSERT_EQ(AES_GCM_OK, RC_DEC);
      if (PT_LEN > 0) {
        EXPECT_EQ(0, std::memcmp(narrow(ptOutC, PT_LEN).data(), ptBytes, PT_LEN));
      }
    }
  }
}
