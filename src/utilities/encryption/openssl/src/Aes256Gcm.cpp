/**
 * @file Aes256Gcm.cpp
 * @brief AES-256-GCM authenticated cipher implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Aes256Gcm.hpp"

namespace apex::encryption {

// Pointer-based descriptor (works on OpenSSL 1.1.1 and as fallback on 3.x).
const EVP_CIPHER* Aes256Gcm::fetchCipherAlgorithm() noexcept { return EVP_aes_256_gcm(); }

// -------------------------
// Vector API wrappers
// -------------------------
Aes256Gcm::Status aes256GcmEncrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                                   bytes_span iv, std::vector<uint8_t>& out,
                                   std::vector<uint8_t>& tag) noexcept {
  Aes256Gcm ctx;
  ctx.setKey(key);
  ctx.setIv(iv);
  return ctx.encrypt(plaintext, aad, out, tag);
}

Aes256Gcm::Status aes256GcmDecrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                                   bytes_span iv, std::vector<uint8_t>& out,
                                   bytes_span tag) noexcept {
  Aes256Gcm ctx;
  ctx.setKey(key);
  ctx.setIv(iv);
  return ctx.decrypt(ciphertext, aad, out, tag);
}

// -------------------------
// Zero-allocation API wrappers
// -------------------------
Aes256Gcm::Status aes256GcmEncrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                                   bytes_span iv, uint8_t* outBuf, size_t& outLen, uint8_t* tagBuf,
                                   size_t& tagLen) noexcept {
  Aes256Gcm ctx;
  ctx.setKey(key);
  ctx.setIv(iv);
  return ctx.encrypt(plaintext, aad, outBuf, outLen, tagBuf, tagLen);
}

Aes256Gcm::Status aes256GcmDecrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                                   bytes_span iv, uint8_t* outBuf, size_t& outLen,
                                   const uint8_t* tagBuf, size_t tagLen) noexcept {
  Aes256Gcm ctx;
  ctx.setKey(key);
  ctx.setIv(iv);
  return ctx.decrypt(ciphertext, aad, outBuf, outLen, tagBuf, tagLen);
}

} // namespace apex::encryption