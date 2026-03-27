/**
 * @file Poly1305.cpp
 * @brief Poly1305 message authentication code implementation using OpenSSL EVP API.
 */

#include "src/utilities/encryption/openssl/inc/Poly1305.hpp"

#include <openssl/evp.h> // EVP_MAC_fetch

namespace apex::encryption {

// Descriptor and parameters for Poly1305
const EVP_MAC* Poly1305::fetchMacAlgorithm() noexcept {
  return EVP_MAC_fetch(nullptr, "POLY1305", nullptr);
}

const char* Poly1305::fetchParamName() noexcept {
  return nullptr; // no OSSL_PARAM required
}

const char* Poly1305::fetchParamValue() noexcept {
  return nullptr; // no OSSL_PARAM required
}

// Vector API
Poly1305::Status poly1305(bytes_span message, std::vector<uint8_t>& out) noexcept {
  Poly1305 mac;
  return mac.mac(message, out);
}

// Buffer API
Poly1305::Status poly1305(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept {
  Poly1305 mac;
  return mac.mac(message, outBuf, outLen);
}

} // namespace apex::encryption
