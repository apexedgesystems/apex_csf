#ifndef APEX_UTILITIES_ENCRYPTION_MAC_BASE_HPP
#define APEX_UTILITIES_ENCRYPTION_MAC_BASE_HPP
/**
 * @file MacBase.hpp
 * @brief CRTP base for one-shot MAC (Message Authentication Code) with OpenSSL, C++17-compatible.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <openssl/evp.h> // EVP_MAC, EVP_MAC_CTX
#include <vector>

// Compatibility shims
#include "src/utilities/compatibility/inc/compat_attributes.hpp" // COMPAT_HOT
#include "src/utilities/compatibility/inc/compat_span.hpp"       // compat::bytes_span

namespace apex::encryption {

// Project-wide alias
using bytes_span = compat::bytes_span;

/**
 * @brief CRTP base for one-shot MACs using OpenSSL.
 *
 * Derived must provide:
 *  - static const EVP_MAC* fetchMacAlgorithm() noexcept;      // pointer path (OpenSSL 3.x)
 *  - static const char*    fetchParamName() noexcept;         // e.g., OSSL_MAC_PARAM_DIGEST /
 * OSSL_MAC_PARAM_CIPHER
 *  - static const char*    fetchParamValue() noexcept;        // e.g., "SHA256" or "AES-128-CBC"
 *  - static constexpr size_t KEY_LENGTH;
 *  - static constexpr size_t DIGEST_LENGTH;
 *
 * Optionally (for provider fetch on OpenSSL 3.x):
 *  - static const char* fetchMacName() noexcept;              // e.g., "HMAC", "CMAC"
 *
 * Provides:
 *  - vector-based API (no allocations after reserve)
 *  - zero-allocation API (caller-provided buffer)
 *
 * @tparam Derived Concrete MAC type.
 */
template <typename Derived> class MacBase {
public:
  /// Status codes for MAC operations.
  enum class Status : uint8_t {
    SUCCESS = 0,                ///< Operation succeeded.
    ERROR_NULL_ALGORITHM = 1,   ///< fetchMacAlgorithm()/provider fetch returned null.
    ERROR_INIT = 2,             ///< EVP_MAC_init failed.
    ERROR_UPDATE = 3,           ///< EVP_MAC_update failed.
    ERROR_FINAL = 4,            ///< EVP_MAC_final failed.
    ERROR_OUTPUT_TOO_SMALL = 5, ///< Output buffer too small; outLen set to required.
    ERROR_INVALID_KEY = 6,      ///< Key length mismatch vs. Derived::KEY_LENGTH.
    ERROR_UNKNOWN = 255         ///< Unspecified error.
  };

  /**
   * @brief Construct MAC context.
   * @note NOT RT-safe: Allocates OpenSSL EVP_MAC_CTX.
   */
  MacBase();

  /**
   * @brief Destroy MAC context.
   * @note RT-safe: Frees pre-allocated context only.
   */
  ~MacBase();

  /**
   * @brief Compute MAC into a pre-allocated vector.
   * @param message Bytes to authenticate.
   * @param[out] out Vector for tag (reserve EVP_MAX_MD_SIZE once).
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no allocation after initial reserve.
   */
  inline Status mac(bytes_span message, std::vector<uint8_t>& out) noexcept COMPAT_HOT;

  /**
   * @brief Compute MAC into a caller-provided buffer (zero-allocation).
   * @param message Bytes to authenticate.
   * @param[out] outBuf Output buffer (capacity >= Derived::DIGEST_LENGTH).
   * @param[in,out] outLen On input: buffer capacity; on output: tag length.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
   */
  inline Status mac(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept COMPAT_HOT;

  /**
   * @brief Set the secret key.
   * @param key Key bytes (must equal Derived::KEY_LENGTH).
   * @note RT-safe: Stores non-owning view only.
   */
  inline void setKey(bytes_span key) noexcept { key_ = key; }

private:
  EVP_MAC_CTX* ctx_;                            ///< Reusable MAC context.
  std::array<uint8_t, EVP_MAX_MD_SIZE> buffer_; ///< Temporary MAC buffer.
  bytes_span key_;                              ///< Non-owning key view.
};

} // namespace apex::encryption

#include "src/utilities/encryption/openssl/src/MacBase.tpp"
#endif // APEX_UTILITIES_ENCRYPTION_MAC_BASE_HPP
