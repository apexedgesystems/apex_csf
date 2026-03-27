#ifndef APEX_UTILITIES_ENCRYPTION_POLY1305_HPP
#define APEX_UTILITIES_ENCRYPTION_POLY1305_HPP
/**
 * @file Poly1305.hpp
 * @brief Poly1305 adapter using OpenSSL EVP_MAC with a C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/MacBase.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace apex::encryption {

/**
 * @brief Poly1305 one-time MAC using OpenSSL EVP_MAC.
 *
 * Inherits from MacBase<Poly1305> for vector and zero-allocation APIs.
 */
class Poly1305 : public MacBase<Poly1305> {
public:
  /**
   * @brief Pointer-based MAC descriptor.
   * @return POLY1305 implementation (OpenSSL).
   */
  static const EVP_MAC* fetchMacAlgorithm() noexcept;

  /**
   * @brief No parameter name required for Poly1305.
   * @return nullptr
   */
  static const char* fetchParamName() noexcept;

  /**
   * @brief No parameter value required for Poly1305.
   * @return nullptr
   */
  static const char* fetchParamValue() noexcept;

  /// Required key length (bytes).
  static constexpr size_t KEY_LENGTH = 32;

  /// Output tag length (bytes).
  static constexpr size_t DIGEST_LENGTH = 16;
};

/**
 * @brief One-shot Poly1305 into a pre-allocated vector.
 * @param message Bytes to authenticate.
 * @param[out] out Vector for the tag (reserve EVP_MAX_MD_SIZE once).
 * @return Poly1305::Status Status code.
 */
Poly1305::Status poly1305(bytes_span message, std::vector<uint8_t>& out) noexcept;

/**
 * @brief One-shot Poly1305 into a caller-provided buffer.
 * @param message Bytes to authenticate.
 * @param[out] outBuf Output buffer (≥ EVP_MAX_MD_SIZE).
 * @param[in,out] outLen On input: buffer capacity; on output: tag length.
 * @return Poly1305::Status Status code.
 */
Poly1305::Status poly1305(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_POLY1305_HPP
