#ifndef APEX_UTILITIES_ENCRYPTION_LITE_AES256_GCM_LITE_HPP
#define APEX_UTILITIES_ENCRYPTION_LITE_AES256_GCM_LITE_HPP
/**
 * @file Aes256GcmLite.hpp
 * @brief Standalone AES-256-GCM authenticated encryption for bare-metal targets.
 *
 * Software AES-256-GCM (AEAD) without OpenSSL or any OS dependencies.
 * Header-only, zero heap allocation, suitable for Cortex-M4 and similar.
 *
 * Implementation uses direct S-box lookups and byte-level operations for clarity
 * and correctness over raw throughput. At 80 MHz on Cortex-M4, this yields
 * ~2 MB/s -- fast enough for UART-rate encryption.
 *
 * References:
 *   - FIPS 197: Advanced Encryption Standard (AES)
 *   - NIST SP 800-38D: Galois/Counter Mode (GCM)
 *
 * @note NOT RT-safe in the real-time sense (variable-time S-box lookups).
 *       For side-channel resistance, use hardware AES peripheral.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#endif

namespace apex {
namespace encryption {
namespace lite {

/* ----------------------------- Constants ----------------------------- */

static constexpr size_t AES256_KEY_LEN = 32;
static constexpr size_t GCM_NONCE_LEN = 12;
static constexpr size_t GCM_TAG_LEN = 16;
static constexpr size_t AES_BLOCK_SIZE = 16;

/* ----------------------------- GcmStatus ----------------------------- */

/**
 * @enum GcmStatus
 * @brief Status codes for AES-256-GCM operations.
 */
enum class GcmStatus : uint8_t { OK = 0, ERROR_NULL_POINTER, ERROR_AUTH_FAILED };

/* ----------------------------- GcmResult ----------------------------- */

/**
 * @struct GcmResult
 * @brief Result of an AES-256-GCM encrypt or decrypt operation.
 */
struct GcmResult {
  GcmStatus status;
  uint32_t bytesWritten;
};

namespace detail {

/* ----------------------------- AES S-Box ----------------------------- */

/**
 * FIPS 197, Figure 7 -- AES forward substitution box.
 *
 * On AVR, PROGMEM stores this 256-byte table in flash instead of SRAM.
 * All lookups go through sbox() which uses pgm_read_byte() on AVR.
 */
// clang-format off
#ifdef __AVR__
const uint8_t SBOX[256] PROGMEM = {
#else
constexpr uint8_t SBOX[256] = {
#endif
  0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
  0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
  0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
  0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
  0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
  0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
  0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
  0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
  0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
  0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
  0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
  0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
  0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
  0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
  0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
  0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};
// clang-format on

/**
 * @brief Read one S-box byte. Uses pgm_read_byte() on AVR (PROGMEM table),
 *        direct array access on all other platforms.
 */
inline uint8_t sbox(uint8_t i) {
#ifdef __AVR__
  return pgm_read_byte(&SBOX[i]);
#else
  return SBOX[i];
#endif
}

/* ----------------------------- AES Primitives ----------------------------- */

/** Multiply by 2 in GF(2^8) with AES reduction polynomial. */
inline constexpr uint8_t xtime(uint8_t a) {
  return static_cast<uint8_t>((a << 1) ^ (static_cast<uint8_t>(-(a >> 7)) & 0x1B));
}

/** Apply S-box substitution to all 16 state bytes. */
inline void subBytes(uint8_t* state) {
  for (int i = 0; i < 16; ++i) {
    state[i] = sbox(state[i]);
  }
}

/**
 * Cyclic left-shift each row of the state.
 * State layout (column-major): state[row + 4*col].
 *   Row 0: no shift
 *   Row 1: shift left by 1
 *   Row 2: shift left by 2
 *   Row 3: shift left by 3
 */
inline void shiftRows(uint8_t* s) {
  uint8_t t;

  // Row 1: shift left by 1
  t = s[1];
  s[1] = s[5];
  s[5] = s[9];
  s[9] = s[13];
  s[13] = t;

  // Row 2: shift left by 2
  t = s[2];
  s[2] = s[10];
  s[10] = t;
  t = s[6];
  s[6] = s[14];
  s[14] = t;

  // Row 3: shift left by 3 (= right by 1)
  t = s[15];
  s[15] = s[11];
  s[11] = s[7];
  s[7] = s[3];
  s[3] = t;
}

/**
 * MixColumns: multiply each column by the fixed polynomial in GF(2^8).
 * Operates on one column at a time (4 bytes at indices 4*col .. 4*col+3).
 */
inline void mixColumns(uint8_t* s) {
  for (int col = 0; col < 4; ++col) {
    const int BASE = col * 4;
    const uint8_t S0 = s[BASE];
    const uint8_t S1 = s[BASE + 1];
    const uint8_t S2 = s[BASE + 2];
    const uint8_t S3 = s[BASE + 3];

    s[BASE] = xtime(S0) ^ (xtime(S1) ^ S1) ^ S2 ^ S3;
    s[BASE + 1] = S0 ^ xtime(S1) ^ (xtime(S2) ^ S2) ^ S3;
    s[BASE + 2] = S0 ^ S1 ^ xtime(S2) ^ (xtime(S3) ^ S3);
    s[BASE + 3] = (xtime(S0) ^ S0) ^ S1 ^ S2 ^ xtime(S3);
  }
}

/** XOR 16-byte round key into state. Round key words are big-endian. */
inline void addRoundKey(uint8_t* state, const uint32_t* rk, int round) {
  for (int col = 0; col < 4; ++col) {
    const uint32_t W = rk[round * 4 + col];
    state[4 * col] ^= static_cast<uint8_t>(W >> 24);
    state[4 * col + 1] ^= static_cast<uint8_t>(W >> 16);
    state[4 * col + 2] ^= static_cast<uint8_t>(W >> 8);
    state[4 * col + 3] ^= static_cast<uint8_t>(W);
  }
}

/* ----------------------------- AES Key Schedule ----------------------------- */

/**
 * Expand a 256-bit key into 60 round-key words (15 round keys x 4 words).
 * FIPS 197, Section 5.2.
 */
inline void aes256KeyExpand(const uint8_t key[32], uint32_t rk[60]) {
  for (int i = 0; i < 8; ++i) {
    rk[i] = (static_cast<uint32_t>(key[4 * i]) << 24) |
            (static_cast<uint32_t>(key[4 * i + 1]) << 16) |
            (static_cast<uint32_t>(key[4 * i + 2]) << 8) | static_cast<uint32_t>(key[4 * i + 3]);
  }

  // Round constants (RCON[0] unused, indices 1-7 for AES-256)
  static constexpr uint8_t RCON[8] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40};

  for (int i = 8; i < 60; ++i) {
    uint32_t temp = rk[i - 1];
    if (i % 8 == 0) {
      // RotWord: [b0,b1,b2,b3] -> [b1,b2,b3,b0], then SubWord, then XOR RCON
      temp = (static_cast<uint32_t>(sbox((temp >> 16) & 0xFF)) << 24) |
             (static_cast<uint32_t>(sbox((temp >> 8) & 0xFF)) << 16) |
             (static_cast<uint32_t>(sbox(temp & 0xFF)) << 8) |
             static_cast<uint32_t>(sbox((temp >> 24) & 0xFF));
      temp ^= static_cast<uint32_t>(RCON[i / 8]) << 24;
    } else if (i % 8 == 4) {
      // SubWord only (AES-256 extra step)
      temp = (static_cast<uint32_t>(sbox((temp >> 24) & 0xFF)) << 24) |
             (static_cast<uint32_t>(sbox((temp >> 16) & 0xFF)) << 16) |
             (static_cast<uint32_t>(sbox((temp >> 8) & 0xFF)) << 8) |
             static_cast<uint32_t>(sbox(temp & 0xFF));
    }
    rk[i] = rk[i - 8] ^ temp;
  }
}

/* ----------------------------- AES Block Encrypt ----------------------------- */

/**
 * Encrypt a single 128-bit block with AES-256 (14 rounds).
 * FIPS 197, Section 5.1.
 */
inline void aesEncryptBlock(const uint32_t rk[60], const uint8_t in[16], uint8_t out[16]) {
  uint8_t state[16];
  memcpy(state, in, 16);

  addRoundKey(state, rk, 0);

  for (int round = 1; round <= 13; ++round) {
    subBytes(state);
    shiftRows(state);
    mixColumns(state);
    addRoundKey(state, rk, round);
  }

  // Final round (no MixColumns)
  subBytes(state);
  shiftRows(state);
  addRoundKey(state, rk, 14);

  memcpy(out, state, 16);
}

/* ----------------------------- GCM Helpers ----------------------------- */

/** XOR 16 bytes: a[i] ^= b[i]. */
inline void xorBlock(uint8_t* a, const uint8_t* b) {
  for (int i = 0; i < 16; ++i) {
    a[i] ^= b[i];
  }
}

/**
 * Increment the rightmost 32 bits of a 128-bit counter block (big-endian).
 * NIST SP 800-38D, Section 6.2.
 */
inline void incr32(uint8_t* block) {
  for (int i = 15; i >= 12; --i) {
    if (++block[i] != 0) {
      break;
    }
  }
}

/**
 * GF(2^128) multiplication: X = X * H.
 *
 * Standard GHASH multiply over GF(2^128) with reduction polynomial
 * R = x^128 + x^7 + x^2 + x + 1 (represented as 0xE1 in high byte).
 *
 * Processes X one bit at a time (128 iterations).
 */
inline void gfMul128(uint8_t X[16], const uint8_t H[16]) {
  uint8_t Z[16];
  memset(Z, 0, 16);
  uint8_t V[16];
  memcpy(V, H, 16);

  for (int i = 0; i < 16; ++i) {
    for (int bit = 7; bit >= 0; --bit) {
      if ((X[i] >> bit) & 1) {
        xorBlock(Z, V);
      }
      const uint8_t LSB = V[15] & 1;
      for (int j = 15; j > 0; --j) {
        V[j] = static_cast<uint8_t>((V[j] >> 1) | (V[j - 1] << 7));
      }
      V[0] >>= 1;
      if (LSB) {
        V[0] ^= 0xE1;
      }
    }
  }

  memcpy(X, Z, 16);
}

/**
 * GHASH: incremental update.
 * Y = (Y ^ block) * H.
 */
inline void ghashUpdate(uint8_t Y[16], const uint8_t H[16], const uint8_t block[16]) {
  xorBlock(Y, block);
  gfMul128(Y, H);
}

/**
 * GHASH over AAD and ciphertext with length encoding.
 * NIST SP 800-38D, Section 6.4.
 *
 * Processes: A || 0^v || C || 0^u || [len(A)]_64 || [len(C)]_64
 */
inline void ghash(const uint8_t H[16], const uint8_t* aad, size_t aadLen, const uint8_t* ct,
                  size_t ctLen, uint8_t Y[16]) {
  memset(Y, 0, 16);
  uint8_t block[16];

  // Process AAD in 16-byte blocks (pad last block with zeros)
  size_t pos = 0;
  while (pos + 16 <= aadLen) {
    ghashUpdate(Y, H, aad + pos);
    pos += 16;
  }
  if (pos < aadLen) {
    memset(block, 0, 16);
    memcpy(block, aad + pos, aadLen - pos);
    ghashUpdate(Y, H, block);
  }

  // Process ciphertext in 16-byte blocks (pad last block with zeros)
  pos = 0;
  while (pos + 16 <= ctLen) {
    ghashUpdate(Y, H, ct + pos);
    pos += 16;
  }
  if (pos < ctLen) {
    memset(block, 0, 16);
    memcpy(block, ct + pos, ctLen - pos);
    ghashUpdate(Y, H, block);
  }

  // Length block: [len(A) in bits]_64 || [len(C) in bits]_64 (big-endian)
  memset(block, 0, 16);
  const uint64_t AAD_BITS = static_cast<uint64_t>(aadLen) * 8;
  const uint64_t CT_BITS = static_cast<uint64_t>(ctLen) * 8;
  block[0] = static_cast<uint8_t>(AAD_BITS >> 56);
  block[1] = static_cast<uint8_t>(AAD_BITS >> 48);
  block[2] = static_cast<uint8_t>(AAD_BITS >> 40);
  block[3] = static_cast<uint8_t>(AAD_BITS >> 32);
  block[4] = static_cast<uint8_t>(AAD_BITS >> 24);
  block[5] = static_cast<uint8_t>(AAD_BITS >> 16);
  block[6] = static_cast<uint8_t>(AAD_BITS >> 8);
  block[7] = static_cast<uint8_t>(AAD_BITS);
  block[8] = static_cast<uint8_t>(CT_BITS >> 56);
  block[9] = static_cast<uint8_t>(CT_BITS >> 48);
  block[10] = static_cast<uint8_t>(CT_BITS >> 40);
  block[11] = static_cast<uint8_t>(CT_BITS >> 32);
  block[12] = static_cast<uint8_t>(CT_BITS >> 24);
  block[13] = static_cast<uint8_t>(CT_BITS >> 16);
  block[14] = static_cast<uint8_t>(CT_BITS >> 8);
  block[15] = static_cast<uint8_t>(CT_BITS);
  ghashUpdate(Y, H, block);
}

/**
 * GCTR: Galois Counter mode encryption/decryption.
 * NIST SP 800-38D, Section 6.5.
 *
 * Encrypts (or decrypts) data by XORing with AES-encrypted counter blocks.
 * The counter block (icb) is modified in place.
 */
inline void gctr(const uint32_t rk[60], uint8_t icb[16], const uint8_t* in, size_t len,
                 uint8_t* out) {
  uint8_t keystream[16];
  size_t pos = 0;

  while (pos + 16 <= len) {
    aesEncryptBlock(rk, icb, keystream);
    for (int i = 0; i < 16; ++i) {
      out[pos + i] = in[pos + i] ^ keystream[i];
    }
    incr32(icb);
    pos += 16;
  }

  // Partial last block
  if (pos < len) {
    aesEncryptBlock(rk, icb, keystream);
    const size_t REMAINING = len - pos;
    for (size_t i = 0; i < REMAINING; ++i) {
      out[pos + i] = in[pos + i] ^ keystream[i];
    }
    incr32(icb);
  }
}

} // namespace detail

/* ----------------------------- API ----------------------------- */

/**
 * @brief AES-256-GCM authenticated encryption.
 *
 * Encrypts plaintext and produces a 16-byte authentication tag.
 * The tag covers both the AAD and the ciphertext.
 *
 * @param key        32-byte AES-256 key.
 * @param nonce      12-byte GCM nonce/IV.
 * @param plaintext  Input data (may be nullptr if ptLen == 0).
 * @param ptLen      Plaintext length in bytes.
 * @param aad        Additional authenticated data (may be nullptr if aadLen == 0).
 * @param aadLen     AAD length in bytes.
 * @param ciphertext Output buffer (must be >= ptLen bytes).
 * @param tag        Output 16-byte authentication tag.
 * @return GcmResult with status and bytes written.
 */
inline GcmResult aes256GcmEncrypt(const uint8_t* key, const uint8_t* nonce,
                                  const uint8_t* plaintext, uint32_t ptLen, const uint8_t* aad,
                                  uint32_t aadLen, uint8_t* ciphertext, uint8_t tag[16]) {
  if (key == nullptr || nonce == nullptr || tag == nullptr) {
    return {GcmStatus::ERROR_NULL_POINTER, 0};
  }
  if (ptLen > 0 && (plaintext == nullptr || ciphertext == nullptr)) {
    return {GcmStatus::ERROR_NULL_POINTER, 0};
  }

  // Key expansion
  uint32_t rk[60];
  detail::aes256KeyExpand(key, rk);

  // H = AES_K(0^128). 'work' serves as zeros input, then is reused below.
  uint8_t H[16];
  uint8_t work[16];
  memset(work, 0, 16);
  detail::aesEncryptBlock(rk, work, H);

  // J0 = nonce || 0x00000001 (for 96-bit nonce)
  uint8_t J0[16];
  memcpy(J0, nonce, 12);
  J0[12] = 0x00;
  J0[13] = 0x00;
  J0[14] = 0x00;
  J0[15] = 0x01;

  // Encrypt: C = GCTR_K(inc32(J0), P). Reuse work as counter block.
  memcpy(work, J0, 16);
  detail::incr32(work);
  detail::gctr(rk, work, plaintext, ptLen, ciphertext);

  // GHASH over AAD and ciphertext. Reuse work for S output.
  detail::ghash(H, aad, aadLen, ciphertext, ptLen, work);

  // Tag = AES_K(J0) ^ S. Write AES result directly into tag, then XOR with S.
  detail::aesEncryptBlock(rk, J0, tag);
  for (int i = 0; i < 16; ++i) {
    tag[i] ^= work[i];
  }

  return {GcmStatus::OK, ptLen};
}

/**
 * @brief AES-256-GCM authenticated decryption.
 *
 * Decrypts ciphertext and verifies the authentication tag.
 * Returns ERROR_AUTH_FAILED if the tag does not match (plaintext buffer
 * is zeroed on auth failure to prevent use of unauthenticated data).
 *
 * @param key        32-byte AES-256 key.
 * @param nonce      12-byte GCM nonce/IV.
 * @param ciphertext Input ciphertext.
 * @param ctLen      Ciphertext length in bytes.
 * @param aad        Additional authenticated data (may be nullptr if aadLen == 0).
 * @param aadLen     AAD length in bytes.
 * @param tag        16-byte authentication tag to verify.
 * @param plaintext  Output buffer (must be >= ctLen bytes).
 * @return GcmResult with status and bytes written.
 */
inline GcmResult aes256GcmDecrypt(const uint8_t* key, const uint8_t* nonce,
                                  const uint8_t* ciphertext, uint32_t ctLen, const uint8_t* aad,
                                  uint32_t aadLen, const uint8_t tag[16], uint8_t* plaintext) {
  if (key == nullptr || nonce == nullptr || tag == nullptr) {
    return {GcmStatus::ERROR_NULL_POINTER, 0};
  }
  if (ctLen > 0 && (plaintext == nullptr || ciphertext == nullptr)) {
    return {GcmStatus::ERROR_NULL_POINTER, 0};
  }

  // Key expansion
  uint32_t rk[60];
  detail::aes256KeyExpand(key, rk);

  // H = AES_K(0^128). 'work' serves as zeros input, then is reused below.
  uint8_t H[16];
  uint8_t work[16];
  memset(work, 0, 16);
  detail::aesEncryptBlock(rk, work, H);

  // J0 = nonce || 0x00000001
  uint8_t J0[16];
  memcpy(J0, nonce, 12);
  J0[12] = 0x00;
  J0[13] = 0x00;
  J0[14] = 0x00;
  J0[15] = 0x01;

  // GHASH over AAD and ciphertext (authenticate before decrypting).
  // Reuse work for S output.
  detail::ghash(H, aad, aadLen, ciphertext, ctLen, work);

  // Compute expected tag: T' = AES_K(J0) ^ S.
  // H is dead after ghash, reuse for expected tag.
  detail::aesEncryptBlock(rk, J0, H);
  for (int i = 0; i < 16; ++i) {
    H[i] ^= work[i];
  }

  // Constant-time tag comparison (H holds expected tag)
  uint8_t diff = 0;
  for (int i = 0; i < 16; ++i) {
    diff |= H[i] ^ tag[i];
  }
  if (diff != 0) {
    if (ctLen > 0 && plaintext != nullptr) {
      memset(plaintext, 0, ctLen);
    }
    return {GcmStatus::ERROR_AUTH_FAILED, 0};
  }

  // Decrypt: P = GCTR_K(inc32(J0), C). Reuse work as counter block.
  memcpy(work, J0, 16);
  detail::incr32(work);
  detail::gctr(rk, work, ciphertext, ctLen, plaintext);

  return {GcmStatus::OK, ctLen};
}

} // namespace lite
} // namespace encryption
} // namespace apex

#endif // APEX_UTILITIES_ENCRYPTION_LITE_AES256_GCM_LITE_HPP
