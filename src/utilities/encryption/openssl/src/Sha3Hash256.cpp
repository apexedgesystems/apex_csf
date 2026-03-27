/**
 * @file Sha3Hash256.cpp
 * @brief SHA3-256 hash implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Sha3Hash256.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

// Algorithm name for OpenSSL 3.x provider fetch
const char* Sha3Hash256::fetchHashName() noexcept { return "SHA3-256"; }

// EVP_MD* retrieval for OpenSSL 1.1.1 and as a fallback on 3.x
const EVP_MD* Sha3Hash256::fetchHashAlgorithm() noexcept { return EVP_sha3_256(); }

// Vector-based one-shot hash
Sha3Hash256::Status sha3Hash256(bytes_span message, std::vector<uint8_t>& outDigest) noexcept {
  Sha3Hash256 hasher;
  return hasher.hash(message, outDigest);
}

// Zero-allocation one-shot hash
Sha3Hash256::Status sha3Hash256(bytes_span message, uint8_t* outBuf, size_t& inoutLen) noexcept {
  Sha3Hash256 hasher;
  return hasher.hash(message, outBuf, inoutLen);
}

} // namespace apex::encryption
