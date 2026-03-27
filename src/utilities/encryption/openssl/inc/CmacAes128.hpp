#ifndef APEX_UTILITIES_ENCRYPTION_CMAC_AES128_HPP
#define APEX_UTILITIES_ENCRYPTION_CMAC_AES128_HPP
/**
 * @file CmacAes128.hpp
 * @brief CMAC-AES128 adapter using OpenSSL EVP_MAC with a C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/MacBase.hpp"

#include <cstddef>
#include <cstdint>
#include <openssl/core_names.h> // OSSL_MAC_PARAM_CIPHER
#include <vector>

namespace apex::encryption {

/**
 * @brief CMAC-AES128 using OpenSSL EVP_MAC.
 *
 * Inherits from MacBase<CmacAes128> for vector and zero-allocation APIs.
 */
class CmacAes128 : public MacBase<CmacAes128> {
public:
  /**
   * @brief Pointer-based MAC descriptor.
   * @return CMAC implementation (OpenSSL).
   */
  static const EVP_MAC* fetchMacAlgorithm() noexcept;

  /**
   * @brief Parameter name to select the cipher.
   * @return OSSL_MAC_PARAM_CIPHER.
   */
  static const char* fetchParamName() noexcept;

  /**
   * @brief Parameter value for AES-128-CBC selection.
   * @return "AES-128-CBC".
   */
  static const char* fetchParamValue() noexcept;

  /// Required key length (bytes).
  static constexpr size_t KEY_LENGTH = 16;

  /// Output tag length (bytes).
  static constexpr size_t DIGEST_LENGTH = 16;
};

/**
 * @brief One-shot CMAC-AES128 into a pre-allocated vector.
 * @param message Bytes to authenticate.
 * @param[out] out Vector for the tag (reserve EVP_MAX_MD_SIZE once).
 * @return CmacAes128::Status Status code.
 */
CmacAes128::Status cmacAes128(bytes_span message, std::vector<uint8_t>& out) noexcept;

/**
 * @brief One-shot CMAC-AES128 into a caller-provided buffer.
 * @param message Bytes to authenticate.
 * @param[out] outBuf Output buffer (≥ EVP_MAX_MD_SIZE).
 * @param[in,out] outLen On input: buffer capacity; on output: tag length.
 * @return CmacAes128::Status Status code.
 */
CmacAes128::Status cmacAes128(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_CMAC_AES128_HPP
