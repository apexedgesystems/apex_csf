/**
 * @file HmacSha512.cpp
 * @brief HMAC-SHA-512 message authentication code implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/HmacSha512.hpp"

#include <openssl/core_names.h> // OSSL_MAC_PARAM_DIGEST
#include <openssl/evp.h>        // EVP_MAC_fetch

namespace apex::encryption {

// Descriptor and parameters for HMAC-SHA512
const EVP_MAC* HmacSha512::fetchMacAlgorithm() noexcept {
  return EVP_MAC_fetch(nullptr, "HMAC", nullptr);
}

const char* HmacSha512::fetchParamName() noexcept { return OSSL_MAC_PARAM_DIGEST; }

const char* HmacSha512::fetchParamValue() noexcept { return "SHA512"; }

// Vector API
HmacSha512::Status hmacSha512(bytes_span message, std::vector<uint8_t>& out) noexcept {
  HmacSha512 mac;
  return mac.mac(message, out);
}

// Buffer API
HmacSha512::Status hmacSha512(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept {
  HmacSha512 mac;
  return mac.mac(message, outBuf, outLen);
}

} // namespace apex::encryption
