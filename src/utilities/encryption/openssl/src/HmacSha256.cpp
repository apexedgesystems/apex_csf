/**
 * @file HmacSha256.cpp
 * @brief HMAC-SHA-256 message authentication code implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/HmacSha256.hpp"

#include <openssl/core_names.h> // OSSL_MAC_PARAM_DIGEST
#include <openssl/evp.h>        // EVP_MAC_fetch

namespace apex::encryption {

// Descriptor and parameters for HMAC-SHA256
const EVP_MAC* HmacSha256::fetchMacAlgorithm() noexcept {
  return EVP_MAC_fetch(nullptr, "HMAC", nullptr);
}

const char* HmacSha256::fetchParamName() noexcept { return OSSL_MAC_PARAM_DIGEST; }

const char* HmacSha256::fetchParamValue() noexcept { return "SHA256"; }

// Vector API
HmacSha256::Status hmacSha256(bytes_span message, std::vector<uint8_t>& out) noexcept {
  HmacSha256 mac;
  return mac.mac(message, out);
}

// Buffer API
HmacSha256::Status hmacSha256(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept {
  HmacSha256 mac;
  return mac.mac(message, outBuf, outLen);
}

} // namespace apex::encryption
