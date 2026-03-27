#ifndef APEX_UTILITIES_ENCRYPTION_AES256_GCM_HPP
#define APEX_UTILITIES_ENCRYPTION_AES256_GCM_HPP
/**
 * @file Aes256Gcm.hpp
 * @brief AES-256-GCM AEAD adapter using OpenSSL EVP with a C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/AeadBase.hpp"

#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

/**
 * @brief AES-256-GCM (AEAD) using OpenSSL EVP.
 *
 * Inherits from AeadBase<Aes256Gcm> to provide:
 *  - one-shot encrypt/decrypt into std::vector (cipher/plain + tag)
 *  - one-shot encrypt/decrypt into caller buffers (zero allocations)
 *
 * @note Key = 32 bytes, IV/nonce = 12 bytes, Tag = 16 bytes.
 */
class Aes256Gcm : public AeadBase<Aes256Gcm> {
public:
  /** 256-bit key length in bytes. */
  static constexpr std::size_t KEY_LENGTH = 32;

  /** Recommended GCM IV/nonce length in bytes. */
  static constexpr std::size_t IV_LENGTH = 12;

  /** Authentication tag length in bytes. */
  static constexpr std::size_t TAG_LENGTH = 16;

  /**
   * @brief Provider-friendly cipher name for OpenSSL 3.x.
   * @return C-string "AES-256-GCM".
   */
  static const char* fetchCipherName() noexcept { return "AES-256-GCM"; }

  /**
   * @brief Pointer-based descriptor (OpenSSL 1.1.1 and fallback on 3.x).
   * @return const EVP_CIPHER* Pointer to EVP_aes_256_gcm().
   */
  static const EVP_CIPHER* fetchCipherAlgorithm() noexcept;

  /**
   * @brief No extra OSSL_PARAM is required for standard GCM.
   * @return nullptr
   */
  static const char* fetchParamName() noexcept { return nullptr; }

  /**
   * @brief No extra OSSL_PARAM value is required for standard GCM.
   * @return nullptr
   */
  static const char* fetchParamValue() noexcept { return nullptr; }
};

/**
 * @brief One-shot AES-256-GCM encrypt into vectors.
 * @param plaintext Bytes to encrypt.
 * @param aad       Additional authenticated data (may be empty).
 * @param key       32-byte key (Aes256Gcm::KEY_LENGTH).
 * @param iv        12-byte nonce (Aes256Gcm::IV_LENGTH).
 * @param[out] outCipher Resized to plaintext.size().
 * @param[out] outTag    Resized to TAG_LENGTH.
 * @return Aes256Gcm::Status Status code.
 */
Aes256Gcm::Status aes256GcmEncrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                                   bytes_span iv, std::vector<std::uint8_t>& outCipher,
                                   std::vector<std::uint8_t>& outTag) noexcept;

/**
 * @brief One-shot AES-256-GCM decrypt into a vector.
 * @param ciphertext Encrypted bytes.
 * @param aad        Additional authenticated data.
 * @param key        32-byte key (Aes256Gcm::KEY_LENGTH).
 * @param iv         12-byte nonce (Aes256Gcm::IV_LENGTH).
 * @param[out] outPlain Resized to ciphertext.size().
 * @param tag        Authentication tag (length must be TAG_LENGTH).
 * @return Aes256Gcm::Status Status code (ERROR_AUTH on tag mismatch).
 */
Aes256Gcm::Status aes256GcmDecrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                                   bytes_span iv, std::vector<std::uint8_t>& outPlain,
                                   bytes_span tag) noexcept;

/**
 * @brief One-shot AES-256-GCM encrypt into caller-provided buffers (zero allocations).
 * @param plaintext  Bytes to encrypt.
 * @param aad        Additional authenticated data.
 * @param key        32-byte key.
 * @param iv         12-byte nonce.
 * @param[out] outBuf  Ciphertext buffer; capacity ≥ plaintext.size().
 * @param[in,out] outLen On input: outBuf capacity; on output: ciphertext length.
 * @param[out] tagBuf  Tag buffer; capacity ≥ TAG_LENGTH.
 * @param[in,out] tagLen On input: tagBuf capacity; on output: tag length (== TAG_LENGTH).
 * @return Aes256Gcm::Status Status code.
 */
Aes256Gcm::Status aes256GcmEncrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                                   bytes_span iv, std::uint8_t* outBuf, std::size_t& outLen,
                                   std::uint8_t* tagBuf, std::size_t& tagLen) noexcept;

/**
 * @brief One-shot AES-256-GCM decrypt into a caller buffer (zero allocations).
 * @param ciphertext Encrypted bytes.
 * @param aad        Additional authenticated data.
 * @param key        32-byte key.
 * @param iv         12-byte nonce.
 * @param[out] outBuf  Plaintext buffer; capacity ≥ ciphertext.size().
 * @param[in,out] outLen On input: outBuf capacity; on output: plaintext length.
 * @param tag        Tag bytes.
 * @param tagLen     Length of @p tag (must be TAG_LENGTH).
 * @return Aes256Gcm::Status Status code (ERROR_AUTH on tag mismatch).
 */
Aes256Gcm::Status aes256GcmDecrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                                   bytes_span iv, std::uint8_t* outBuf, std::size_t& outLen,
                                   const std::uint8_t* tag, std::size_t tagLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_AES256_GCM_HPP
