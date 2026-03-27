#ifndef APEX_UTILITIES_ENCRYPTION_HMAC_SHA512_HPP
#define APEX_UTILITIES_ENCRYPTION_HMAC_SHA512_HPP
/**
 * @file HmacSha512.hpp
 * @brief HMAC-SHA512 adapter using OpenSSL EVP_MAC with a C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/MacBase.hpp"

#include <cstddef>
#include <cstdint>
#include <openssl/core_names.h> // OSSL_MAC_PARAM_DIGEST
#include <vector>

namespace apex::encryption {

/**
 * @brief HMAC-SHA512 using OpenSSL EVP_MAC.
 *
 * Inherits from MacBase<HmacSha512> for vector and zero-allocation APIs.
 */
class HmacSha512 : public MacBase<HmacSha512> {
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
   * @brief Parameter value for SHA-512 selection.
   * @return "SHA512".
   */
  static const char* fetchParamValue() noexcept;

  /// Required key length (bytes).
  static constexpr size_t KEY_LENGTH = 512 / 8;

  /// Output tag length (bytes).
  static constexpr size_t DIGEST_LENGTH = 512 / 8;
};

/**
 * @brief One-shot HMAC-SHA512 into a pre-allocated vector.
 * @param message Bytes to authenticate.
 * @param[out] out Vector for the tag (reserve EVP_MAX_MD_SIZE once).
 * @return HmacSha512::Status Status code.
 */
HmacSha512::Status hmacSha512(bytes_span message, std::vector<uint8_t>& out) noexcept;

/**
 * @brief One-shot HMAC-SHA512 into a caller-provided buffer.
 * @param message Bytes to authenticate.
 * @param[out] outBuf Output buffer (≥ EVP_MAX_MD_SIZE).
 * @param[in,out] outLen On input: buffer capacity; on output: tag length.
 * @return HmacSha512::Status Status code.
 */
HmacSha512::Status hmacSha512(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_HMAC_SHA512_HPP
