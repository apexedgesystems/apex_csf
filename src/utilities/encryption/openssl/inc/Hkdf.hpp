#ifndef APEX_UTILITIES_ENCRYPTION_HKDF_HPP
#define APEX_UTILITIES_ENCRYPTION_HKDF_HPP
/**
 * @file Hkdf.hpp
 * @brief HKDF (HMAC-based Key Derivation Function) per RFC 5869.
 *
 * HKDF is essential for deriving cryptographic keys from:
 *  - Diffie-Hellman shared secrets
 *  - Pre-shared keys
 *  - Other key material
 *
 * Two-phase operation:
 *  1. Extract: Condense input key material into a pseudorandom key (PRK)
 *  2. Expand: Derive output key material from PRK
 *
 * Common use: TLS 1.3, Signal Protocol, WireGuard, Noise Protocol.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace apex::encryption {

using bytes_span = compat::bytes_span;

/**
 * @brief HKDF key derivation function.
 *
 * Supports SHA-256 and SHA-512 as the underlying hash.
 */
class Hkdf {
public:
  /// Status codes for HKDF operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,
    ERROR_INVALID_HASH = 1,
    ERROR_OUTPUT_TOO_LONG = 2,
    ERROR_EXTRACT_FAILED = 3,
    ERROR_EXPAND_FAILED = 4,
    ERROR_UNKNOWN = 255
  };

  /// Supported hash algorithms for HKDF.
  enum class HashType : std::uint8_t { SHA256 = 0, SHA512 = 1 };

  /**
   * @brief Extract-and-Expand in one call (most common usage).
   *
   * @param hashType Hash algorithm to use (SHA256 or SHA512).
   * @param ikm Input key material (shared secret, pre-shared key, etc.).
   * @param salt Optional salt (can be empty; uses zero-filled salt).
   * @param info Context/application-specific info (can be empty).
   * @param outputLength Desired output key length in bytes.
   * @param[out] outKey Derived key material.
   * @return Status code.
   *
   * @note RT-safe: Bounded operations, no heap allocation after reserve.
   *
   * Example:
   * @code
   *   std::vector<uint8_t> derivedKey;
   *   auto status = Hkdf::derive(
   *       Hkdf::HashType::SHA256,
   *       sharedSecret,  // From DH/ECDH
   *       {},            // No salt
   *       "my-app-v1",   // Context info
   *       32,            // 256-bit key
   *       derivedKey);
   * @endcode
   */
  [[nodiscard]] static Status derive(HashType hashType, bytes_span ikm, bytes_span salt,
                                     bytes_span info, std::size_t outputLength,
                                     std::vector<std::uint8_t>& outKey) noexcept;

  /**
   * @brief Extract-and-Expand into caller-provided buffer (zero-allocation).
   *
   * @param hashType Hash algorithm to use.
   * @param ikm Input key material.
   * @param salt Optional salt.
   * @param info Context info.
   * @param[out] outBuf Output buffer.
   * @param[in,out] outLen On input: buffer capacity; on output: bytes written.
   * @return Status code.
   *
   * @note RT-safe: Bounded operations, no heap allocation.
   */
  [[nodiscard]] static Status derive(HashType hashType, bytes_span ikm, bytes_span salt,
                                     bytes_span info, std::uint8_t* outBuf,
                                     std::size_t& outLen) noexcept;

  /**
   * @brief Extract phase only (produces PRK).
   *
   * Use when you need to expand the same PRK multiple times with different info.
   *
   * @param hashType Hash algorithm.
   * @param ikm Input key material.
   * @param salt Optional salt.
   * @param[out] outPrk Pseudorandom key (hash output size).
   * @return Status code.
   */
  [[nodiscard]] static Status extract(HashType hashType, bytes_span ikm, bytes_span salt,
                                      std::vector<std::uint8_t>& outPrk) noexcept;

  /**
   * @brief Expand phase only (derives OKM from PRK).
   *
   * @param hashType Hash algorithm.
   * @param prk Pseudorandom key from extract().
   * @param info Context info.
   * @param outputLength Desired output length.
   * @param[out] outKey Output key material.
   * @return Status code.
   */
  [[nodiscard]] static Status expand(HashType hashType, bytes_span prk, bytes_span info,
                                     std::size_t outputLength,
                                     std::vector<std::uint8_t>& outKey) noexcept;

  /// Returns hash output size for the given hash type.
  [[nodiscard]] static std::size_t hashSize(HashType hashType) noexcept;

  /// Returns maximum output length for the given hash type (255 * hashSize).
  [[nodiscard]] static std::size_t maxOutputLength(HashType hashType) noexcept;
};

/* ----------------------------- Convenience API ----------------------------- */

/**
 * @brief HKDF-SHA256 derive (most common usage).
 * @note RT-safe: Bounded operations, no heap allocation after reserve.
 */
inline Hkdf::Status hkdfSha256(bytes_span ikm, bytes_span salt, bytes_span info,
                               std::size_t outputLength,
                               std::vector<std::uint8_t>& outKey) noexcept {
  return Hkdf::derive(Hkdf::HashType::SHA256, ikm, salt, info, outputLength, outKey);
}

/**
 * @brief HKDF-SHA512 derive.
 * @note RT-safe: Bounded operations, no heap allocation after reserve.
 */
inline Hkdf::Status hkdfSha512(bytes_span ikm, bytes_span salt, bytes_span info,
                               std::size_t outputLength,
                               std::vector<std::uint8_t>& outKey) noexcept {
  return Hkdf::derive(Hkdf::HashType::SHA512, ikm, salt, info, outputLength, outKey);
}

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_HKDF_HPP
