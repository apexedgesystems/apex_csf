#ifndef APEX_UTILITIES_ENCRYPTION_CIPHER_BASE_HPP
#define APEX_UTILITIES_ENCRYPTION_CIPHER_BASE_HPP
/**
 * @file CipherBase.hpp
 * @brief CRTP base for symmetric encryption/decryption using OpenSSL EVP_CIPHER (C++17-compatible).
 */

#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <vector>

// Compatibility shims
#include "src/utilities/compatibility/inc/compat_attributes.hpp" // COMPAT_HOT
#include "src/utilities/compatibility/inc/compat_span.hpp"       // compat::bytes_span

namespace apex::encryption {

// Project-wide aliases
using bytes_span = compat::bytes_span;

/**
 * @brief CRTP base for symmetric ciphers using OpenSSL.
 *
 * Derived must provide:
 *  - static const EVP_CIPHER* fetchCipherAlgorithm() noexcept; // pointer path
 *  - static const char*       fetchCipherName() noexcept;       // optional (OpenSSL 3.x provider
 * name)
 *  - static const char*       fetchParamName() noexcept;        // optional (e.g., mode/tweak
 * param)
 *  - static const char*       fetchParamValue() noexcept;       // optional value for the above
 *  - static constexpr size_t  KEY_LENGTH;
 *  - static constexpr size_t  IV_LENGTH;
 *  - static constexpr size_t  BLOCK_SIZE;  // max per-update expansion; used for buffer sizing
 *
 * Provides:
 *  - Zero-allocation encrypt/decrypt (caller buffer)
 *  - Vector-based encrypt/decrypt (no allocs after reserve)
 *  - Typed status codes (no exceptions)
 *
 * @tparam Derived Concrete cipher type.
 */
template <typename Derived> class CipherBase {
public:
  /// Status codes for cipher operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,                ///< Operation succeeded.
    ERROR_INVALID_KEY = 1,      ///< Key length mismatch vs. Derived::KEY_LENGTH.
    ERROR_INVALID_IV = 2,       ///< IV length mismatch vs. Derived::IV_LENGTH.
    ERROR_INIT = 3,             ///< EVP_EncryptInit_ex/EVP_DecryptInit_ex failed.
    ERROR_UPDATE = 4,           ///< EVP_EncryptUpdate/EVP_DecryptUpdate failed.
    ERROR_FINAL = 5,            ///< EVP_EncryptFinal_ex/EVP_DecryptFinal_ex failed.
    ERROR_OUTPUT_TOO_SMALL = 6, ///< Caller buffer too small; outLen set to required.
    ERROR_NULL_ALGORITHM = 7,   ///< fetchCipherAlgorithm()/provider fetch returned null.
    ERROR_UNKNOWN = 255         ///< Unspecified error.
  };

  /**
   * @brief Set the secret key.
   * @param keySpan Key bytes (must be exactly Derived::KEY_LENGTH).
   * @note RT-safe: Stores non-owning view only.
   */
  inline void setKey(bytes_span keySpan) noexcept { key_ = keySpan; }

  /**
   * @brief Set the IV/nonce.
   * @param ivSpan IV bytes (must be exactly Derived::IV_LENGTH).
   * @note RT-safe: Stores non-owning view only.
   */
  inline void setIv(bytes_span ivSpan) noexcept { iv_ = ivSpan; }

  /**
   * @brief Encrypt into a caller-provided buffer (zero-allocation).
   * @param message Plaintext bytes.
   * @param[out] outBuf Output buffer (capacity >= message.size() + BLOCK_SIZE).
   * @param[in,out] outLen On input: buffer capacity; on output: ciphertext length.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
   */
  inline Status encrypt(bytes_span message, std::uint8_t* outBuf,
                        std::size_t& outLen) noexcept COMPAT_HOT;

  /**
   * @brief Decrypt into a caller-provided buffer (zero-allocation).
   * @param message Ciphertext bytes.
   * @param[out] outBuf Output buffer (capacity >= message.size()).
   * @param[in,out] outLen On input: buffer capacity; on output: plaintext length.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
   */
  inline Status decrypt(bytes_span message, std::uint8_t* outBuf,
                        std::size_t& outLen) noexcept COMPAT_HOT;

  /**
   * @brief Encrypt into a pre-allocated vector.
   * @param message Plaintext bytes.
   * @param[out] out Vector for ciphertext (reserve message.size() + BLOCK_SIZE once).
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no allocation after initial reserve.
   */
  inline Status encrypt(bytes_span message, std::vector<std::uint8_t>& out) noexcept COMPAT_HOT;

  /**
   * @brief Decrypt into a pre-allocated vector.
   * @param message Ciphertext bytes.
   * @param[out] out Vector for plaintext (reserve message.size() once).
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no allocation after initial reserve.
   */
  inline Status decrypt(bytes_span message, std::vector<std::uint8_t>& out) noexcept COMPAT_HOT;

protected:
  bytes_span key_; ///< Non-owning key view.
  bytes_span iv_;  ///< Non-owning IV/nonce view.

  /// Scratch space for small temporaries (block-aligned ops, final block, etc.).
  std::array<std::uint8_t, EVP_MAX_BLOCK_LENGTH * 2> buffer_{};
};

} // namespace apex::encryption

#include "src/utilities/encryption/openssl/src/CipherBase.tpp"
#endif // APEX_UTILITIES_ENCRYPTION_CIPHER_BASE_HPP
