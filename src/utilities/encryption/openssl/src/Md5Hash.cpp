/**
 * @file Md5Hash.cpp
 * @brief MD5 hash implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Md5Hash.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

// Algorithm name for OpenSSL 3.x provider fetch
const char* Md5Hash::fetchHashName() noexcept { return "MD5"; }

// EVP_MD* retrieval for OpenSSL 1.1.1 and as a fallback on 3.x
const EVP_MD* Md5Hash::fetchHashAlgorithm() noexcept { return EVP_md5(); }

// Vector-based one-shot hash
Md5Hash::Status md5Hash(bytes_span message, std::vector<uint8_t>& outDigest) noexcept {
  Md5Hash hasher;
  return hasher.hash(message, outDigest);
}

// Zero-allocation one-shot hash
Md5Hash::Status md5Hash(bytes_span message, uint8_t* outBuf, size_t& inoutLen) noexcept {
  Md5Hash hasher;
  return hasher.hash(message, outBuf, inoutLen);
}

} // namespace apex::encryption
