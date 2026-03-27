/**
 * @file Aes256Ctr.cpp
 * @brief AES-256-CTR cipher implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Aes256Ctr.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

// Provider name for OpenSSL 3.x fetch path
const char* Aes256Ctr::fetchCipherName() noexcept { return "AES-256-CTR"; }

// Pointer path for OpenSSL 1.1.1 and fallback on 3.x
const EVP_CIPHER* Aes256Ctr::fetchCipherAlgorithm() noexcept { return EVP_aes_256_ctr(); }

// Vector-based one-shot encrypt
Aes256Ctr::Status aes256CtrEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept {
  Aes256Ctr c;
  c.setKey(key);
  c.setIv(iv);
  return c.encrypt(message, out);
}

// Vector-based one-shot decrypt
Aes256Ctr::Status aes256CtrDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept {
  Aes256Ctr c;
  c.setKey(key);
  c.setIv(iv);
  return c.decrypt(message, out);
}

// Zero-allocation encrypt
Aes256Ctr::Status aes256CtrEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept {
  Aes256Ctr c;
  c.setKey(key);
  c.setIv(iv);
  return c.encrypt(message, outBuf, outLen);
}

// Zero-allocation decrypt
Aes256Ctr::Status aes256CtrDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept {
  Aes256Ctr c;
  c.setKey(key);
  c.setIv(iv);
  return c.decrypt(message, outBuf, outLen);
}

} // namespace apex::encryption
