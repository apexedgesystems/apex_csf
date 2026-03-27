/**
 * @file ChaCha20Poly1305.cpp
 * @brief ChaCha20-Poly1305 AEAD cipher implementation.
 */

#include "src/utilities/encryption/openssl/inc/ChaCha20Poly1305.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

const char* ChaCha20Poly1305::fetchCipherName() noexcept { return "ChaCha20-Poly1305"; }

const EVP_CIPHER* ChaCha20Poly1305::fetchCipherAlgorithm() noexcept {
  return EVP_chacha20_poly1305();
}

} // namespace apex::encryption
