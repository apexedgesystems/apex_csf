/**
 * @file Blake2sHash.cpp
 * @brief BLAKE2s-256 hash implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Blake2sHash.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

// Algorithm name for OpenSSL 3.x provider fetch
const char* Blake2sHash::fetchHashName() noexcept { return "BLAKE2S-256"; }

// EVP_MD* retrieval for OpenSSL 1.1.1 and as a fallback on 3.x
const EVP_MD* Blake2sHash::fetchHashAlgorithm() noexcept { return EVP_blake2s256(); }

// Vector-based one-shot hash
Blake2sHash::Status blake2sHash(bytes_span message, std::vector<uint8_t>& outDigest) noexcept {
  Blake2sHash hasher;
  return hasher.hash(message, outDigest);
}

// Zero-allocation one-shot hash
Blake2sHash::Status blake2sHash(bytes_span message, uint8_t* outBuf, size_t& inoutLen) noexcept {
  Blake2sHash hasher;
  return hasher.hash(message, outBuf, inoutLen);
}

} // namespace apex::encryption
