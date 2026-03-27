#ifndef APEX_UTILITIES_ENCRYPTION_AES256_CTR_HPP
#define APEX_UTILITIES_ENCRYPTION_AES256_CTR_HPP
/**
 * @file Aes256Ctr.hpp
 * @brief AES-256-CTR cipher adapter using OpenSSL EVP with a C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/CipherBase.hpp"

#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

/**
 * @brief AES-256-CTR stream cipher using OpenSSL EVP.
 *
 * Inherits from CipherBase<Aes256Ctr> to provide:
 *  - one-shot encrypt/decrypt into a std::vector
 *  - one-shot encrypt/decrypt into a caller buffer (zero allocations)
 *
 * @note Key size = 32 bytes, IV size = 16 bytes. Block size = 16 bytes.
 */
class Aes256Ctr : public CipherBase<Aes256Ctr> {
public:
  /** 256-bit key length in bytes. */
  static constexpr std::size_t KEY_LENGTH = 32;

  /** 128-bit IV length in bytes. */
  static constexpr std::size_t IV_LENGTH = 16;

  /** AES block size in bytes. */
  static constexpr std::size_t BLOCK_SIZE = 16;

  /**
   * @brief Provider-friendly cipher name for OpenSSL 3.x.
   * @return C-string "AES-256-CTR".
   */
  static const char* fetchCipherName() noexcept;

  /**
   * @brief Pointer-based descriptor for OpenSSL 1.1.1 (and fallback on 3.x).
   * @return const EVP_CIPHER* Pointer to EVP_aes_256_ctr().
   */
  static const EVP_CIPHER* fetchCipherAlgorithm() noexcept;

  /**
   * @brief No extra OSSL_PARAM is required for CTR mode.
   * @return nullptr
   */
  static const char* fetchParamName() noexcept { return nullptr; }

  /**
   * @brief No extra OSSL_PARAM value is required for CTR mode.
   * @return nullptr
   */
  static const char* fetchParamValue() noexcept { return nullptr; }
};

/**
 * @brief One-shot AES-256-CTR encryption into a std::vector.
 * @param message Plaintext bytes.
 * @param key     32-byte key (Aes256Ctr::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Ctr::IV_LENGTH).
 * @param[out] out Vector resized to ciphertext length.
 * @return Aes256Ctr::Status Status code.
 */
Aes256Ctr::Status aes256CtrEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept;

/**
 * @brief One-shot AES-256-CTR decryption into a std::vector.
 * @param message Ciphertext bytes.
 * @param key     32-byte key (Aes256Ctr::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Ctr::IV_LENGTH).
 * @param[out] out Vector resized to plaintext length.
 * @return Aes256Ctr::Status Status code.
 */
Aes256Ctr::Status aes256CtrDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::vector<std::uint8_t>& out) noexcept;

/**
 * @brief One-shot AES-256-CTR encryption into a caller buffer (zero allocations).
 * @param message Plaintext bytes.
 * @param key     32-byte key (Aes256Ctr::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Ctr::IV_LENGTH).
 * @param[out] outBuf Output buffer; capacity ≥ message.size() + BLOCK_SIZE.
 * @param[in,out] outLen On input: buffer capacity; on output: ciphertext length.
 * @return Aes256Ctr::Status Status code.
 */
Aes256Ctr::Status aes256CtrEncrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept;

/**
 * @brief One-shot AES-256-CTR decryption into a caller buffer (zero allocations).
 * @param message Ciphertext bytes.
 * @param key     32-byte key (Aes256Ctr::KEY_LENGTH).
 * @param iv      16-byte IV  (Aes256Ctr::IV_LENGTH).
 * @param[out] outBuf Output buffer; capacity ≥ message.size().
 * @param[in,out] outLen On input: buffer capacity; on output: plaintext length.
 * @return Aes256Ctr::Status Status code.
 */
Aes256Ctr::Status aes256CtrDecrypt(bytes_span message, bytes_span key, bytes_span iv,
                                   std::uint8_t* outBuf, std::size_t& outLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_AES256_CTR_HPP
