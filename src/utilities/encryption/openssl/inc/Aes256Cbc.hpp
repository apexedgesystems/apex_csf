#ifndef APEX_UTILITIES_ENCRYPTION_AES256_CBC_HPP
#define APEX_UTILITIES_ENCRYPTION_AES256_CBC_HPP
/**
 * @file Aes256Cbc.hpp
 * @brief AES-256-CBC cipher adapter using OpenSSL EVP with a C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/CipherBase.hpp"

#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

/**
 * @brief AES-256-CBC block cipher using OpenSSL EVP.
 *
 * Inherits from CipherBase<Aes256Cbc> to provide:
 *  - one-shot encrypt/decrypt into a std::vector
 *  - one-shot encrypt/decrypt into a caller buffer (zero allocations)
 *
 * @note Key size = 32 bytes, IV size = 16 bytes. Block size = 16 bytes.
 */
class Aes256Cbc : public CipherBase<Aes256Cbc> {
public:
  /** 256-bit key length in bytes. */
  static constexpr std::size_t KEY_LENGTH = 32;

  /** 128-bit IV length in bytes. */
  static constexpr std::size_t IV_LENGTH = 16;

  /** AES block size in bytes. */
  static constexpr std::size_t BLOCK_SIZE = 16;

  /**
   * @brief Provider-friendly cipher name for OpenSSL 3.x.
   * @return C-string "AES-256-CBC".
   */
  static const char* fetchCipherName() noexcept;

  /**
   * @brief Pointer-based descriptor for OpenSSL 1.1.1 (and fallback on 3.x).
   * @return const EVP_CIPHER* Pointer to EVP_aes_256_cbc().
   */
  static const EVP_CIPHER* fetchCipherAlgorithm() noexcept;

  /**
   * @brief No extra OSSL_PARAM is required for AES-256-CBC.
   * @return nullptr
   */
  static const char* fetchParamName() noexcept { return nullptr; }

  /**
   * @brief No extra OSSL_PARAM value is required for AES-256-CBC.
   * @return nullptr
   */
  static const char* fetchParamValue() noexcept { return nullptr; }
};

/**
 * @brief One-shot AES-256-CBC encryption into a std::vector.
 * @param message Plaintext bytes.
 * @param key     32-byte key (Aes256Cbc::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Cbc::IV_LENGTH).
 * @param[out] out Vector resized to the ciphertext length (includes padding).
 * @return Aes256Cbc::Status Status code.
 */
Aes256Cbc::Status aes256CbcEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept;

/**
 * @brief One-shot AES-256-CBC decryption into a std::vector.
 * @param message Ciphertext bytes.
 * @param key     32-byte key (Aes256Cbc::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Cbc::IV_LENGTH).
 * @param[out] out Vector resized to recovered plaintext length.
 * @return Aes256Cbc::Status Status code.
 */
Aes256Cbc::Status aes256CbcDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept;

/**
 * @brief One-shot AES-256-CBC encryption into a caller buffer (zero allocations).
 * @param message Plaintext bytes.
 * @param key     32-byte key (Aes256Cbc::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Cbc::IV_LENGTH).
 * @param[out] outBuf Output buffer; capacity ≥ message.size() + BLOCK_SIZE.
 * @param[in,out] outLen On input: buffer capacity; on output: ciphertext length.
 * @return Aes256Cbc::Status Status code.
 */
Aes256Cbc::Status aes256CbcEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept;

/**
 * @brief One-shot AES-256-CBC decryption into a caller buffer (zero allocations).
 * @param message Ciphertext bytes.
 * @param key     32-byte key (Aes256Cbc::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Cbc::IV_LENGTH).
 * @param[out] outBuf Output buffer; capacity ≥ message.size().
 * @param[in,out] outLen On input: buffer capacity; on output: plaintext length.
 * @return Aes256Cbc::Status Status code.
 */
Aes256Cbc::Status aes256CbcDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_AES256_CBC_HPP
