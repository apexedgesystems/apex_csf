/**
 * @file aes256gcm.h
 * @brief Standalone AES-256-GCM in C for C2000 (16-bit CHAR_BIT safe).
 *
 * Direct C port of Aes256GcmLite.hpp with explicit 0xFF masking on all
 * byte operations for C28x compatibility (CHAR_BIT=16).
 *
 * References:
 *   - FIPS 197: Advanced Encryption Standard (AES)
 *   - NIST SP 800-38D: Galois/Counter Mode (GCM)
 */

#ifndef C2000_ENCRYPTOR_AES256GCM_H
#define C2000_ENCRYPTOR_AES256GCM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define AES256_KEY_LEN 32
#define GCM_NONCE_LEN 12
#define GCM_TAG_LEN 16
#define AES_BLOCK_SIZE 16

#define AES_GCM_OK 0
#define AES_GCM_ERR_NULL 1
#define AES_GCM_ERR_AUTH 2

/* Mask to 8 bits -- critical on C28x where CHAR_BIT=16 */
#define B(x) ((uint16_t)(x) & 0xFFu)

/* ----------------------------- S-Box ----------------------------- */

static const uint16_t SBOX[256] = {
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
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16};

/* ----------------------------- AES Primitives ----------------------------- */

static uint16_t xtime(uint16_t a) { return B((B(a) << 1) ^ (B(-(B(a) >> 7)) & 0x1B)); }

static void subBytes(uint16_t* s) {
  int i;
  for (i = 0; i < 16; i++)
    s[i] = SBOX[B(s[i])];
}

static void shiftRows(uint16_t* s) {
  uint16_t t;
  t = s[1];
  s[1] = s[5];
  s[5] = s[9];
  s[9] = s[13];
  s[13] = t;
  t = s[2];
  s[2] = s[10];
  s[10] = t;
  t = s[6];
  s[6] = s[14];
  s[14] = t;
  t = s[15];
  s[15] = s[11];
  s[11] = s[7];
  s[7] = s[3];
  s[3] = t;
}

static void mixColumns(uint16_t* s) {
  int col;
  for (col = 0; col < 4; col++) {
    int b = col * 4;
    uint16_t s0 = B(s[b]), s1 = B(s[b + 1]), s2 = B(s[b + 2]), s3 = B(s[b + 3]);
    s[b] = B(xtime(s0) ^ xtime(s1) ^ s1 ^ s2 ^ s3);
    s[b + 1] = B(s0 ^ xtime(s1) ^ xtime(s2) ^ s2 ^ s3);
    s[b + 2] = B(s0 ^ s1 ^ xtime(s2) ^ xtime(s3) ^ s3);
    s[b + 3] = B(xtime(s0) ^ s0 ^ s1 ^ s2 ^ xtime(s3));
  }
}

static void addRoundKey(uint16_t* state, const uint32_t* rk, int round) {
  int col;
  for (col = 0; col < 4; col++) {
    uint32_t w = rk[round * 4 + col];
    state[4 * col] = B(state[4 * col] ^ B(w >> 24));
    state[4 * col + 1] = B(state[4 * col + 1] ^ B(w >> 16));
    state[4 * col + 2] = B(state[4 * col + 2] ^ B(w >> 8));
    state[4 * col + 3] = B(state[4 * col + 3] ^ B(w));
  }
}

/* ----------------------------- Key Schedule ----------------------------- */

static void aes256KeyExpand(const uint16_t key[32], uint32_t rk[60]) {
  static const uint16_t RCON[8] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40};
  int i;
  for (i = 0; i < 8; i++) {
    rk[i] = ((uint32_t)B(key[4 * i]) << 24) | ((uint32_t)B(key[4 * i + 1]) << 16) |
            ((uint32_t)B(key[4 * i + 2]) << 8) | (uint32_t)B(key[4 * i + 3]);
  }
  for (i = 8; i < 60; i++) {
    uint32_t temp = rk[i - 1];
    if (i % 8 == 0) {
      temp = ((uint32_t)SBOX[B(temp >> 16)] << 24) | ((uint32_t)SBOX[B(temp >> 8)] << 16) |
             ((uint32_t)SBOX[B(temp)] << 8) | (uint32_t)SBOX[B(temp >> 24)];
      temp ^= (uint32_t)RCON[i / 8] << 24;
    } else if (i % 8 == 4) {
      temp = ((uint32_t)SBOX[B(temp >> 24)] << 24) | ((uint32_t)SBOX[B(temp >> 16)] << 16) |
             ((uint32_t)SBOX[B(temp >> 8)] << 8) | (uint32_t)SBOX[B(temp)];
    }
    rk[i] = rk[i - 8] ^ temp;
  }
}

/* ----------------------------- Block Encrypt ----------------------------- */

static void aesEncryptBlock(const uint32_t rk[60], const uint16_t in[16], uint16_t out[16]) {
  uint16_t state[16];
  int i, round;
  for (i = 0; i < 16; i++)
    state[i] = B(in[i]);
  addRoundKey(state, rk, 0);
  for (round = 1; round <= 13; round++) {
    subBytes(state);
    shiftRows(state);
    mixColumns(state);
    addRoundKey(state, rk, round);
  }
  subBytes(state);
  shiftRows(state);
  addRoundKey(state, rk, 14);
  for (i = 0; i < 16; i++)
    out[i] = B(state[i]);
}

/* ----------------------------- GCM Helpers ----------------------------- */

static void xorBlock(uint16_t* a, const uint16_t* b) {
  int i;
  for (i = 0; i < 16; i++)
    a[i] = B(a[i] ^ b[i]);
}

static void incr32(uint16_t* block) {
  int i;
  for (i = 15; i >= 12; i--) {
    block[i] = B(block[i] + 1);
    if (block[i] != 0)
      break;
  }
}

static void gfMul128(uint16_t X[16], const uint16_t H[16]) {
  uint16_t Z[16], V[16];
  int i, bit, j;
  for (i = 0; i < 16; i++) {
    Z[i] = 0;
    V[i] = B(H[i]);
  }
  for (i = 0; i < 16; i++) {
    for (bit = 7; bit >= 0; bit--) {
      if ((B(X[i]) >> bit) & 1)
        xorBlock(Z, V);
      {
        uint16_t lsb = B(V[15]) & 1;
        for (j = 15; j > 0; j--)
          V[j] = B((B(V[j]) >> 1) | (B(V[j - 1]) << 7));
        V[0] = B(B(V[0]) >> 1);
        if (lsb)
          V[0] = B(V[0] ^ 0xE1);
      }
    }
  }
  for (i = 0; i < 16; i++)
    X[i] = B(Z[i]);
}

static void ghashUpdate(uint16_t Y[16], const uint16_t H[16], const uint16_t block[16]) {
  xorBlock(Y, block);
  gfMul128(Y, H);
}

static void ghash(const uint16_t H[16], const uint16_t* aad, size_t aadLen, const uint16_t* ct,
                  size_t ctLen, uint16_t Y[16]) {
  uint16_t block[16];
  size_t pos;
  uint32_t aadBits, ctBits;
  int i;

  for (i = 0; i < 16; i++)
    Y[i] = 0;

  pos = 0;
  while (pos + 16 <= aadLen) {
    ghashUpdate(Y, H, aad + pos);
    pos += 16;
  }
  if (pos < aadLen) {
    for (i = 0; i < 16; i++)
      block[i] = 0;
    for (i = 0; i < (int)(aadLen - pos); i++)
      block[i] = B(aad[pos + i]);
    ghashUpdate(Y, H, block);
  }

  pos = 0;
  while (pos + 16 <= ctLen) {
    ghashUpdate(Y, H, ct + pos);
    pos += 16;
  }
  if (pos < ctLen) {
    for (i = 0; i < 16; i++)
      block[i] = 0;
    for (i = 0; i < (int)(ctLen - pos); i++)
      block[i] = B(ct[pos + i]);
    ghashUpdate(Y, H, block);
  }

  for (i = 0; i < 16; i++)
    block[i] = 0;
  aadBits = (uint32_t)aadLen * 8;
  ctBits = (uint32_t)ctLen * 8;
  block[4] = B(aadBits >> 24);
  block[5] = B(aadBits >> 16);
  block[6] = B(aadBits >> 8);
  block[7] = B(aadBits);
  block[12] = B(ctBits >> 24);
  block[13] = B(ctBits >> 16);
  block[14] = B(ctBits >> 8);
  block[15] = B(ctBits);
  ghashUpdate(Y, H, block);
}

static void gctr(const uint32_t rk[60], uint16_t icb[16], const uint16_t* in, size_t len,
                 uint16_t* out) {
  uint16_t ks[16];
  size_t pos = 0, i;
  while (pos + 16 <= len) {
    aesEncryptBlock(rk, icb, ks);
    for (i = 0; i < 16; i++)
      out[pos + i] = B(in[pos + i] ^ ks[i]);
    incr32(icb);
    pos += 16;
  }
  if (pos < len) {
    size_t rem = len - pos;
    aesEncryptBlock(rk, icb, ks);
    for (i = 0; i < rem; i++)
      out[pos + i] = B(in[pos + i] ^ ks[i]);
    incr32(icb);
  }
}

/* ----------------------------- Public API ----------------------------- */

/**
 * @brief AES-256-GCM encrypt.
 *
 * All byte arrays use uint16_t (one byte per 16-bit word, upper 8 bits zero).
 * This is native to the C28x architecture.
 */
static int aes256gcm_encrypt(const uint16_t* key, const uint16_t* nonce, const uint16_t* pt,
                             uint32_t ptLen, const uint16_t* aad, uint32_t aadLen, uint16_t* ct,
                             uint16_t tag[16]) {
  uint32_t rk[60];
  uint16_t H[16], work[16], J0[16];
  int i;

  if (!key || !nonce || !tag)
    return AES_GCM_ERR_NULL;
  if (ptLen > 0 && (!pt || !ct))
    return AES_GCM_ERR_NULL;

  aes256KeyExpand(key, rk);
  for (i = 0; i < 16; i++)
    work[i] = 0;
  aesEncryptBlock(rk, work, H);

  for (i = 0; i < 12; i++)
    J0[i] = B(nonce[i]);
  J0[12] = 0;
  J0[13] = 0;
  J0[14] = 0;
  J0[15] = 1;

  for (i = 0; i < 16; i++)
    work[i] = J0[i];
  incr32(work);
  gctr(rk, work, pt, ptLen, ct);

  ghash(H, aad, aadLen, ct, ptLen, work);

  aesEncryptBlock(rk, J0, tag);
  for (i = 0; i < 16; i++)
    tag[i] = B(tag[i] ^ work[i]);

  return AES_GCM_OK;
}

/**
 * @brief AES-256-GCM decrypt + verify.
 */
static int aes256gcm_decrypt(const uint16_t* key, const uint16_t* nonce, const uint16_t* ct,
                             uint32_t ctLen, const uint16_t* aad, uint32_t aadLen,
                             const uint16_t tag[16], uint16_t* pt) {
  uint32_t rk[60];
  uint16_t H[16], work[16], J0[16], expected[16];
  uint16_t diff = 0;
  int i;

  if (!key || !nonce || !tag)
    return AES_GCM_ERR_NULL;
  if (ctLen > 0 && (!pt || !ct))
    return AES_GCM_ERR_NULL;

  aes256KeyExpand(key, rk);
  for (i = 0; i < 16; i++)
    work[i] = 0;
  aesEncryptBlock(rk, work, H);

  for (i = 0; i < 12; i++)
    J0[i] = B(nonce[i]);
  J0[12] = 0;
  J0[13] = 0;
  J0[14] = 0;
  J0[15] = 1;

  ghash(H, aad, aadLen, ct, ctLen, work);
  aesEncryptBlock(rk, J0, expected);
  for (i = 0; i < 16; i++)
    expected[i] = B(expected[i] ^ work[i]);

  for (i = 0; i < 16; i++)
    diff |= B(expected[i] ^ tag[i]);
  if (diff != 0) {
    if (ctLen > 0 && pt)
      for (i = 0; (uint32_t)i < ctLen; i++)
        pt[i] = 0;
    return AES_GCM_ERR_AUTH;
  }

  for (i = 0; i < 16; i++)
    work[i] = J0[i];
  incr32(work);
  gctr(rk, work, ct, ctLen, pt);

  return AES_GCM_OK;
}

#endif /* C2000_ENCRYPTOR_AES256GCM_H */
