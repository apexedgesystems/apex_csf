#ifndef APEX_UTILITIES_ENCRYPTION_AEAD_BASE_HPP
#define APEX_UTILITIES_ENCRYPTION_AEAD_BASE_HPP
/**
 * @file AeadBase.hpp
 * @brief CRTP base for AEAD ciphers (e.g., GCM/CCM) using OpenSSL EVP_CIPHER (C++17-compatible).
 */

#include <cstdint>
#include <openssl/evp.h>
#include <vector>

// Compatibility shims
#include "src/utilities/compatibility/inc/compat_attributes.hpp" // COMPAT_HOT
#include "src/utilities/compatibility/inc/compat_span.hpp"       // compat::bytes_span

namespace apex::encryption {

// Project-wide alias
using bytes_span = compat::bytes_span;

/**
 * @brief CRTP base for AEAD ciphers using OpenSSL.
 *
 * Derived must provide:
 *  - static const EVP_CIPHER* fetchCipherAlgorithm() noexcept; // pointer path
 *  - static const char*       fetchCipherName() noexcept;       // optional (OpenSSL 3.x provider
 * name)
 *  - static const char*       fetchParamName() noexcept;        // optional (mode/tweak name)
 *  - static const char*       fetchParamValue() noexcept;       // optional value for the above
 *  - static constexpr size_t  KEY_LENGTH;
 *  - static constexpr size_t  IV_LENGTH;
 *  - static constexpr size_t  TAG_LENGTH;
 *
 * Provides:
 *  - Vector APIs (no allocs after reserve) for encrypt/decrypt (+ AAD & tag)
 *  - Zero-allocation buffer APIs
 *  - Typed status codes (no exceptions)
 *
 * @tparam Derived Concrete AEAD cipher type.
 */
template <typename Derived> class AeadBase {
public:
  /// Status codes for AEAD operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,                ///< Operation succeeded.
    ERROR_INVALID_KEY = 1,      ///< Key length mismatch vs. Derived::KEY_LENGTH.
    ERROR_INVALID_IV = 2,       ///< IV length mismatch vs. Derived::IV_LENGTH.
    ERROR_OUTPUT_TOO_SMALL = 3, ///< Caller buffer too small; outLen/tagLen updated with required.
    ERROR_INIT = 4,             ///< EVP_*Init_ex failed.
    ERROR_AAD = 5,              ///< AAD processing failed.
    ERROR_UPDATE = 6,           ///< Encrypt/Decrypt update failed.
    ERROR_FINAL = 7,            ///< Finalization failed (e.g., pad/tag stage).
    ERROR_AUTH = 8,             ///< Authentication/tag check failed on decrypt.
    ERROR_NULL_ALGORITHM = 9,   ///< fetchCipherAlgorithm()/provider fetch returned null.
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

  // -------- Vector APIs (convenience) --------

  /**
   * @brief AEAD encrypt into pre-allocated vectors.
   * @param plaintext Bytes to encrypt.
   * @param aad       Associated data (may be empty).
   * @param[out] outCipher Will be resized to ciphertext length (== plaintext.size()).
   * @param[out] outTag    Will be resized to Derived::TAG_LENGTH.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no allocation after initial reserve.
   */
  inline Status encrypt(bytes_span plaintext, bytes_span aad, std::vector<std::uint8_t>& outCipher,
                        std::vector<std::uint8_t>& outTag) noexcept COMPAT_HOT;

  /**
   * @brief AEAD decrypt into a pre-allocated vector.
   * @param ciphertext Encrypted bytes.
   * @param aad        Associated data.
   * @param[out] outPlain Resized to recovered plaintext length (== ciphertext.size()).
   * @param tag        Authentication tag (length must be Derived::TAG_LENGTH).
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no allocation after initial reserve.
   */
  inline Status decrypt(bytes_span ciphertext, bytes_span aad, std::vector<std::uint8_t>& outPlain,
                        bytes_span tag) noexcept COMPAT_HOT;

  // -------- Zero-allocation buffer APIs --------

  /**
   * @brief AEAD encrypt into caller-provided buffers (zero-allocation).
   * @param plaintext  Bytes to encrypt.
   * @param aad        Associated data.
   * @param[out] outBuf  Ciphertext buffer; capacity >= plaintext.size().
   * @param[in,out] outLen On input: outBuf capacity; on output: ciphertext length.
   * @param[out] tagBuf  Tag buffer; capacity >= Derived::TAG_LENGTH.
   * @param[in,out] tagLen On input: tagBuf capacity; on output: tag length.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
   */
  inline Status encrypt(bytes_span plaintext, bytes_span aad, std::uint8_t* outBuf,
                        std::size_t& outLen, std::uint8_t* tagBuf,
                        std::size_t& tagLen) noexcept COMPAT_HOT;

  /**
   * @brief AEAD decrypt into caller-provided buffer (zero-allocation).
   * @param ciphertext Encrypted bytes.
   * @param aad        Associated data.
   * @param[out] outBuf  Plaintext buffer; capacity >= ciphertext.size().
   * @param[in,out] outLen On input: outBuf capacity; on output: plaintext length.
   * @param tag        Tag bytes; length must be Derived::TAG_LENGTH.
   * @param tagLen     Length of tag.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
   */
  inline Status decrypt(bytes_span ciphertext, bytes_span aad, std::uint8_t* outBuf,
                        std::size_t& outLen, const std::uint8_t* tag,
                        std::size_t tagLen) noexcept COMPAT_HOT;

protected:
  bytes_span key_; ///< Non-owning key view.
  bytes_span iv_;  ///< Non-owning IV/nonce view.
};

} // namespace apex::encryption

#include "src/utilities/encryption/openssl/src/AeadBase.tpp"
#endif // APEX_UTILITIES_ENCRYPTION_AEAD_BASE_HPP
