#ifndef APEX_UTILITIES_ENCRYPTION_AES128_CCM_HPP
#define APEX_UTILITIES_ENCRYPTION_AES128_CCM_HPP
/**
 * @file Aes128Ccm.hpp
 * @brief AES-128-CCM AEAD cipher using OpenSSL EVP.
 *
 * AES-CCM (Counter with CBC-MAC) is required for IoT protocols:
 *  - Bluetooth Low Energy (BLE)
 *  - Zigbee
 *  - IEEE 802.15.4
 *  - Matter/Thread
 *
 * Note: CCM requires knowing the plaintext length before encryption starts,
 * which OpenSSL handles internally. The nonce length affects security margins.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

using bytes_span = compat::bytes_span;

/**
 * @brief AES-128-CCM AEAD cipher.
 *
 * Properties:
 *  - Key: 128 bits (16 bytes)
 *  - Nonce: 7-13 bytes (13 recommended for BLE, 7 for Zigbee)
 *  - Tag: 4-16 bytes (16 recommended, 4 minimum for constrained)
 *
 * Default configuration uses 13-byte nonce and 16-byte tag (BLE-compatible).
 */
class Aes128Ccm {
public:
  static constexpr std::size_t KEY_LENGTH = 16;           ///< 128-bit key
  static constexpr std::size_t DEFAULT_NONCE_LENGTH = 13; ///< BLE default
  static constexpr std::size_t DEFAULT_TAG_LENGTH = 16;   ///< Full tag

  /// Status codes for CCM operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,
    ERROR_INVALID_KEY = 1,
    ERROR_INVALID_NONCE = 2,
    ERROR_INVALID_TAG_LENGTH = 3,
    ERROR_OUTPUT_TOO_SMALL = 4,
    ERROR_INIT = 5,
    ERROR_AAD = 6,
    ERROR_UPDATE = 7,
    ERROR_FINAL = 8,
    ERROR_AUTH = 9,
    ERROR_NULL_ALGORITHM = 10,
    ERROR_UNKNOWN = 255
  };

  Aes128Ccm();
  ~Aes128Ccm();

  // Non-copyable, movable
  Aes128Ccm(const Aes128Ccm&) = delete;
  Aes128Ccm& operator=(const Aes128Ccm&) = delete;
  Aes128Ccm(Aes128Ccm&&) noexcept;
  Aes128Ccm& operator=(Aes128Ccm&&) noexcept;

  /**
   * @brief Encrypt with CCM (vector API).
   * @param plaintext Data to encrypt.
   * @param aad Additional authenticated data.
   * @param key 16-byte key.
   * @param nonce 7-13 byte nonce.
   * @param tagLength Desired tag length (4-16 bytes).
   * @param[out] outCipher Ciphertext output.
   * @param[out] outTag Authentication tag.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
   */
  [[nodiscard]] Status encrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                               bytes_span nonce, std::size_t tagLength,
                               std::vector<std::uint8_t>& outCipher,
                               std::vector<std::uint8_t>& outTag) noexcept;

  /**
   * @brief Decrypt with CCM (vector API).
   * @param ciphertext Data to decrypt.
   * @param aad Additional authenticated data.
   * @param key 16-byte key.
   * @param nonce 7-13 byte nonce.
   * @param tag Authentication tag to verify.
   * @param[out] outPlain Plaintext output.
   * @return Status code (ERROR_AUTH if verification fails).
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
   */
  [[nodiscard]] Status decrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                               bytes_span nonce, bytes_span tag,
                               std::vector<std::uint8_t>& outPlain) noexcept;

private:
  EVP_CIPHER_CTX* ctx_;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief One-shot AES-128-CCM encrypt (vector API).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
 */
Aes128Ccm::Status aes128CcmEncrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                                   bytes_span nonce, std::size_t tagLength,
                                   std::vector<std::uint8_t>& outCipher,
                                   std::vector<std::uint8_t>& outTag) noexcept;

/**
 * @brief One-shot AES-128-CCM decrypt (vector API).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
 */
Aes128Ccm::Status aes128CcmDecrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                                   bytes_span nonce, bytes_span tag,
                                   std::vector<std::uint8_t>& outPlain) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_AES128_CCM_HPP
