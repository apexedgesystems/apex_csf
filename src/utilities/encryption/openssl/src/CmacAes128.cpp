/**
 * @file CmacAes128.cpp
 * @brief CMAC-AES-128 message authentication code implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/CmacAes128.hpp"

#include <openssl/core_names.h> // OSSL_MAC_PARAM_CIPHER
#include <openssl/evp.h>        // EVP_MAC_fetch

namespace apex::encryption {

// Descriptor and parameters for CMAC-AES128
const EVP_MAC* CmacAes128::fetchMacAlgorithm() noexcept {
  return EVP_MAC_fetch(nullptr, "CMAC", nullptr);
}

const char* CmacAes128::fetchParamName() noexcept { return OSSL_MAC_PARAM_CIPHER; }

const char* CmacAes128::fetchParamValue() noexcept { return "AES-128-CBC"; }

// Vector API
CmacAes128::Status cmacAes128(bytes_span message, std::vector<uint8_t>& out) noexcept {
  CmacAes128 mac;
  return mac.mac(message, out);
}

// Buffer API
CmacAes128::Status cmacAes128(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept {
  CmacAes128 mac;
  return mac.mac(message, outBuf, outLen);
}

} // namespace apex::encryption
