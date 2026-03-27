/**
 * @file EncryptionUtilities.hpp
 * @brief Common encryption utility functions for random byte generation and secure memory
 * operations.
 */

#ifndef APEX_UTILITIES_ENCRYPTION_UTILITIES_HPP
#define APEX_UTILITIES_ENCRYPTION_UTILITIES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "compat_span.hpp"

namespace apex::encryption {

/**
 * @brief Fill a buffer with cryptographically strong random bytes.
 *
 * Uses OpenSSL RAND_bytes internally.
 *
 * @param[out] outBuf Span of bytes to fill.
 * @return 0 on success, non-zero on failure.
 */
uint8_t generateRandomBytes(compat::mutable_bytes_span outBuf) noexcept;

/**
 * @brief Generate a vector of cryptographically strong random bytes.
 *
 * @param len Desired length in bytes.
 * @return Vector of length `len` on success; empty vector on failure.
 */
std::vector<uint8_t> generateRandomVector(size_t len) noexcept;

/**
 * @brief Convert a byte span to a lowercase hex string.
 *
 * @param data Input bytes.
 * @return Hex-encoded string of length 2*data.size().
 */
std::string toHex(compat::bytes_span data);

/**
 * @brief Decode a hex string into a byte vector.
 *
 * Invalid characters or odd length return false.
 *
 * @param hexStr Input hex string.
 * @param[out] outVec Output byte vector.
 * @return true on success; false on malformed input.
 */
bool fromHex(const std::string& hexStr, std::vector<uint8_t>& outVec) noexcept;

/**
 * @brief Constant-time compare of two byte buffers.
 *
 * @param a First buffer.
 * @param b Second buffer.
 * @return true if sizes match and contents are equal; false otherwise.
 */
bool constantTimeCompare(compat::bytes_span a, compat::bytes_span b) noexcept;

/**
 * @brief Securely zero a memory region.
 *
 * Uses `OPENSSL_cleanse` to prevent compiler optimizations.
 *
 * @param ptr Pointer to memory.
 * @param len Length in bytes.
 */
void secureZeroMemory(void* ptr, size_t len) noexcept;

} // namespace apex::encryption

#include "src/utilities/encryption/openssl/src/EncryptionUtilities.tpp"
#endif // APEX_UTILITIES_ENCRYPTION_UTILITIES_HPP
