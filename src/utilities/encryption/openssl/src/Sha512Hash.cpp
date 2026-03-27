/**
 * @file Sha512Hash.cpp
 * @brief SHA-512 hash implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Sha512Hash.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

// Algorithm name for OpenSSL 3.x provider fetch
const char* Sha512Hash::fetchHashName() noexcept { return "SHA512"; }

// EVP_MD* retrieval for OpenSSL 1.1.1 and as a fallback on 3.x
const EVP_MD* Sha512Hash::fetchHashAlgorithm() noexcept { return EVP_sha512(); }

// Vector-based one-shot hash
Sha512Hash::Status sha512Hash(bytes_span message, std::vector<uint8_t>& outDigest) noexcept {
  Sha512Hash hasher;
  return hasher.hash(message, outDigest);
}

// Zero-allocation one-shot hash
Sha512Hash::Status sha512Hash(bytes_span message, uint8_t* outBuf, size_t& inoutLen) noexcept {
  Sha512Hash hasher;
  return hasher.hash(message, outBuf, inoutLen);
}

} // namespace apex::encryption
