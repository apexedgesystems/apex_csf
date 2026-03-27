/**
 * @file Sha256Hash.cpp
 * @brief SHA-256 hash implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Sha256Hash.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

// Algorithm name for OpenSSL 3.x provider fetch
const char* Sha256Hash::fetchHashName() noexcept { return "SHA256"; }

// EVP_MD* retrieval for OpenSSL 1.1.1 and as a fallback on 3.x
const EVP_MD* Sha256Hash::fetchHashAlgorithm() noexcept { return EVP_sha256(); }

// Vector-based one-shot hash
Sha256Hash::Status sha256Hash(bytes_span message, std::vector<uint8_t>& outDigest) noexcept {
  Sha256Hash hasher;
  return hasher.hash(message, outDigest);
}

// Zero-allocation one-shot hash
Sha256Hash::Status sha256Hash(bytes_span message, uint8_t* outBuf, size_t& inoutLen) noexcept {
  Sha256Hash hasher;
  return hasher.hash(message, outBuf, inoutLen);
}

} // namespace apex::encryption
