/**
 * @file Aes128Gcm.cpp
 * @brief AES-128-GCM AEAD cipher implementation.
 */

#include "src/utilities/encryption/openssl/inc/Aes128Gcm.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

const char* Aes128Gcm::fetchCipherName() noexcept { return "AES-128-GCM"; }

const EVP_CIPHER* Aes128Gcm::fetchCipherAlgorithm() noexcept { return EVP_aes_128_gcm(); }

} // namespace apex::encryption
