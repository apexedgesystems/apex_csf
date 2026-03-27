/**
 * @file Aes256Cbc.cpp
 * @brief AES-256-CBC cipher implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Aes256Cbc.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

// Provider name used on OpenSSL 3.x fetch path
const char* Aes256Cbc::fetchCipherName() noexcept { return "AES-256-CBC"; }

// Pointer path for OpenSSL 1.1.1 and fallback on 3.x
const EVP_CIPHER* Aes256Cbc::fetchCipherAlgorithm() noexcept { return EVP_aes_256_cbc(); }

// Vector-based one-shot encrypt
Aes256Cbc::Status aes256CbcEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept {
  Aes256Cbc c;
  c.setKey(key);
  c.setIv(iv);
  return c.encrypt(message, out);
}

// Vector-based one-shot decrypt
Aes256Cbc::Status aes256CbcDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept {
  Aes256Cbc c;
  c.setKey(key);
  c.setIv(iv);
  return c.decrypt(message, out);
}

// Zero-allocation encrypt
Aes256Cbc::Status aes256CbcEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept {
  Aes256Cbc c;
  c.setKey(key);
  c.setIv(iv);
  return c.encrypt(message, outBuf, outLen);
}

// Zero-allocation decrypt
Aes256Cbc::Status aes256CbcDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept {
  Aes256Cbc c;
  c.setKey(key);
  c.setIv(iv);
  return c.decrypt(message, outBuf, outLen);
}

} // namespace apex::encryption
