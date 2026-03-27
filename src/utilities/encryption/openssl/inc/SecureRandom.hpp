#ifndef APEX_UTILITIES_ENCRYPTION_SECURE_RANDOM_HPP
#define APEX_UTILITIES_ENCRYPTION_SECURE_RANDOM_HPP
/**
 * @file SecureRandom.hpp
 * @brief Cryptographically secure random number generation.
 *
 * Wrapper around OpenSSL RAND_bytes for generating:
 *  - Initialization vectors (IVs)
 *  - Nonces
 *  - Session keys
 *  - Random padding
 *
 * @note NOT RT-safe: May block on entropy exhaustion. For RT systems,
 * pre-generate random material during initialization.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace apex::encryption {

/**
 * @brief Cryptographically secure random number generator.
 *
 * Uses OpenSSL's RAND_bytes which sources entropy from the OS
 * (/dev/urandom on Linux, CryptGenRandom on Windows).
 */
class SecureRandom {
public:
  /// Status codes for RNG operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,
    ERROR_ENTROPY = 1,      ///< Insufficient entropy available
    ERROR_INVALID_SIZE = 2, ///< Requested size too large
    ERROR_UNKNOWN = 255
  };

  /**
   * @brief Fill buffer with random bytes.
   *
   * @param[out] buffer Buffer to fill with random data.
   * @param size Number of bytes to generate.
   * @return Status code.
   *
   * @note NOT RT-safe: May block waiting for entropy.
   */
  [[nodiscard]] static Status fill(std::uint8_t* buffer, std::size_t size) noexcept;

  /**
   * @brief Fill vector with random bytes.
   *
   * @param size Number of bytes to generate.
   * @param[out] out Vector to fill (resized to size).
   * @return Status code.
   *
   * @note NOT RT-safe: May block, allocates.
   */
  [[nodiscard]] static Status generate(std::size_t size, std::vector<std::uint8_t>& out) noexcept;

  /**
   * @brief Generate random bytes into fixed-size array.
   *
   * @tparam N Array size.
   * @param[out] out Array to fill.
   * @return Status code.
   *
   * @note NOT RT-safe: May block waiting for entropy.
   *
   * Example:
   * @code
   *   std::array<uint8_t, 12> nonce;
   *   SecureRandom::generate(nonce);
   * @endcode
   */
  template <std::size_t N>
  [[nodiscard]] static Status generate(std::array<std::uint8_t, N>& out) noexcept {
    return fill(out.data(), N);
  }

  /**
   * @brief Generate a random 32-bit unsigned integer.
   * @param[out] value Random value.
   * @return Status code.
   */
  [[nodiscard]] static Status randomUint32(std::uint32_t& value) noexcept;

  /**
   * @brief Generate a random 64-bit unsigned integer.
   * @param[out] value Random value.
   * @return Status code.
   */
  [[nodiscard]] static Status randomUint64(std::uint64_t& value) noexcept;

  /**
   * @brief Check if the RNG is properly seeded.
   * @return true if sufficient entropy is available.
   */
  [[nodiscard]] static bool isSeeded() noexcept;
};

/* ----------------------------- Convenience API ----------------------------- */

/**
 * @brief Generate a random IV/nonce of specified size.
 *
 * @tparam N Size of IV (e.g., 12 for GCM, 16 for CBC).
 * @param[out] iv Array to fill with random bytes.
 * @return Status code.
 *
 * @note NOT RT-safe: May block waiting for entropy.
 *
 * Example:
 * @code
 *   std::array<uint8_t, 12> iv;
 *   generateIv(iv);
 * @endcode
 */
template <std::size_t N> SecureRandom::Status generateIv(std::array<std::uint8_t, N>& iv) noexcept {
  return SecureRandom::generate(iv);
}

/**
 * @brief Generate a random key of specified size.
 *
 * @tparam N Size of key (e.g., 16, 24, 32 for AES).
 * @param[out] key Array to fill with random bytes.
 * @return Status code.
 *
 * @note NOT RT-safe: May block waiting for entropy.
 */
template <std::size_t N>
SecureRandom::Status generateKey(std::array<std::uint8_t, N>& key) noexcept {
  return SecureRandom::generate(key);
}

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_SECURE_RANDOM_HPP
