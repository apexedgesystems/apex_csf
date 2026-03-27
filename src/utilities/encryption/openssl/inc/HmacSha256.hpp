#ifndef APEX_UTILITIES_ENCRYPTION_HMAC_SHA256_HPP
#define APEX_UTILITIES_ENCRYPTION_HMAC_SHA256_HPP
/**
 * @file HmacSha256.hpp
 * @brief HMAC-SHA256 adapter using OpenSSL EVP_MAC with C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/MacBase.hpp"

#include <cstddef>
#include <cstdint>
#include <openssl/core_names.h> // OSSL_MAC_PARAM_DIGEST
#include <vector>

namespace apex::encryption {

/**
 * @brief HMAC-SHA256 using OpenSSL EVP_MAC.
 *
 * Inherits from MacBase<HmacSha256> for vector and zero-allocation APIs.
 */
class HmacSha256 : public MacBase<HmacSha256> {
public:
  /**
   * @brief Pointer-based MAC descriptor.
   * @return HMAC implementation (OpenSSL).
   */
  static const EVP_MAC* fetchMacAlgorithm() noexcept;

  /**
   * @brief Parameter name to select digest algorithm.
   * @return OSSL_MAC_PARAM_DIGEST.
   */
  static const char* fetchParamName() noexcept;

  /**
   * @brief Parameter value for SHA-256 selection.
   * @return "SHA256".
   */
  static const char* fetchParamValue() noexcept;

  /// Required key length (bytes).
  static constexpr size_t KEY_LENGTH = 256 / 8;

  /// Output tag length (bytes).
  static constexpr size_t DIGEST_LENGTH = 256 / 8;
};

/**
 * @brief One-shot HMAC-SHA256 into a pre-allocated vector.
 * @param message Bytes to authenticate.
 * @param[out] out Vector for the tag (reserve EVP_MAX_MD_SIZE once).
 * @return HmacSha256::Status Status code.
 */
HmacSha256::Status hmacSha256(bytes_span message, std::vector<uint8_t>& out) noexcept;

/**
 * @brief One-shot HMAC-SHA256 into a caller-provided buffer.
 * @param message Bytes to authenticate.
 * @param[out] outBuf Output buffer (≥ EVP_MAX_MD_SIZE).
 * @param[in,out] outLen On input: buffer capacity; on output: tag length.
 * @return HmacSha256::Status Status code.
 */
HmacSha256::Status hmacSha256(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_HMAC_SHA256_HPP
