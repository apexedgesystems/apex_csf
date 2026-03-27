#ifndef APEX_UTILITIES_ENCRYPTION_HASH_BASE_HPP
#define APEX_UTILITIES_ENCRYPTION_HASH_BASE_HPP
/**
 * @file HashBase.hpp
 * @brief CRTP base for OpenSSL-based cryptographic hash functions (C++17+).
 *
 * Provides vector-based and zero-allocation hashing APIs. Backed by
 * compatibility headers for C++17 (`bytes_span`) and compiler hints.
 */

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

// Read-only byte view from compat layer
using bytes_span = compat::bytes_span;

/**
 * @brief CRTP base for OpenSSL-based cryptographic hash functions.
 *
 * Derived must implement:
 *   static const EVP_MD* fetchHashAlgorithm() noexcept;
 *
 * APIs:
 *  - Vector-based hashing (no allocations after reserve).
 *  - Zero-allocation hashing (caller-provided buffer).
 *
 * @tparam Derived Concrete hash type.
 */
template <typename Derived> class HashBase {
public:
  /// Status codes for hashing.
  enum class Status : uint8_t {
    SUCCESS = 0,                ///< Operation succeeded.
    ERROR_NULL_ALGORITHM = 1,   ///< fetchHashAlgorithm() returned null.
    ERROR_DIGEST_INIT = 2,      ///< EVP_DigestInit_ex failed.
    ERROR_DIGEST_UPDATE = 3,    ///< EVP_DigestUpdate failed.
    ERROR_DIGEST_FINAL = 4,     ///< EVP_DigestFinal_ex failed.
    ERROR_OUTPUT_TOO_SMALL = 5, ///< Output buffer too small.
    ERROR_UNKNOWN = 255         ///< Unspecified error.
  };

  /**
   * @brief Construct hash context.
   * @note NOT RT-safe: Allocates OpenSSL EVP_MD_CTX.
   */
  HashBase();

  /**
   * @brief Destroy hash context.
   * @note RT-safe: Frees pre-allocated context only.
   */
  ~HashBase();

  /**
   * @brief Hash into a pre-allocated vector.
   * @param message Bytes to hash.
   * @param[out] outDigest Vector for digest (reserve EVP_MAX_MD_SIZE once).
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no allocation after initial reserve.
   */
  inline Status hash(bytes_span message, std::vector<uint8_t>& outDigest) noexcept COMPAT_HOT;

  /**
   * @brief Hash into a caller-provided buffer (zero-allocation).
   * @param message Bytes to hash.
   * @param[out] outBuf Output buffer (capacity >= EVP_MAX_MD_SIZE).
   * @param[in,out] outLen On input: buffer capacity; on output: digest length.
   * @return Status code.
   * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
   */
  inline Status hash(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept COMPAT_HOT;

private:
  EVP_MD_CTX* ctx_;                             ///< Reusable EVP context.
  std::array<uint8_t, EVP_MAX_MD_SIZE> buffer_; ///< Temp digest buffer.
};

} // namespace apex::encryption

#include "src/utilities/encryption/openssl/src/HashBase.tpp"
#endif // APEX_UTILITIES_ENCRYPTION_HASH_BASE_HPP
