#ifndef APEX_UTILITIES_ENCRYPTION_MD5_HASH_HPP
#define APEX_UTILITIES_ENCRYPTION_MD5_HASH_HPP
/**
 * @file Md5Hash.hpp
 * @brief MD5 hash adapter using OpenSSL EVP with C++17-compatible API.
 */

#include "src/utilities/encryption/openssl/inc/HashBase.hpp"

#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

/**
 * @brief MD5 hash using OpenSSL EVP.
 *
 * Inherits from HashBase<Md5Hash> to provide vector and zero-allocation APIs.
 * @note On OpenSSL 3.x, MD5 requires the legacy provider.
 */
class Md5Hash : public HashBase<Md5Hash> {
public:
  /**
   * @brief Provider-friendly algorithm name (OpenSSL 3.x).
   */
  static const char* fetchHashName() noexcept;

  /**
   * @brief Pointer-based descriptor (OpenSSL 1.1.1 and fallback on 3.x).
   */
  static const EVP_MD* fetchHashAlgorithm() noexcept;
};

/**
 * @brief One-shot MD5 into a pre-allocated vector.
 * @param message   Bytes to hash.
 * @param[out] outDigest Vector for the digest (reserve EVP_MAX_MD_SIZE once).
 * @return Md5Hash::Status Status code.
 */
Md5Hash::Status md5Hash(bytes_span message, std::vector<uint8_t>& outDigest) noexcept;

/**
 * @brief One-shot MD5 into a caller-provided buffer.
 * @param message   Bytes to hash.
 * @param[out] outBuf Output buffer (≥ EVP_MAX_MD_SIZE).
 * @param[in,out] inoutLen On input: buffer capacity; on output: digest length.
 * @return Md5Hash::Status Status code.
 */
Md5Hash::Status md5Hash(bytes_span message, uint8_t* outBuf, size_t& inoutLen) noexcept;

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_MD5_HASH_HPP
