#ifndef APEX_SYSTEM_CORE_PROTOCOLS_APROTO_STATUS_HPP
#define APEX_SYSTEM_CORE_PROTOCOLS_APROTO_STATUS_HPP
/**
 * @file AprotoStatus.hpp
 * @brief Compact, strongly-typed status codes for APROTO operations.
 *
 * @note RT-safe: All functions are O(1) with no allocations.
 */

#include <cstdint>

namespace system_core {
namespace protocols {
namespace aproto {

/* ------------------------------ Status ---------------------------------- */

/**
 * @enum Status
 * @brief Status codes for APROTO encode/decode operations.
 *
 * Conventions:
 *   - SUCCESS = 0
 *   - Errors: ERROR_*
 *   - Warnings: WARN_*
 */
enum class Status : std::uint8_t {
  // Success ----------------------------------------------------------------
  SUCCESS = 0,

  // Header errors ----------------------------------------------------------
  ERROR_INVALID_MAGIC = 1,   ///< Magic bytes not APROTO_MAGIC
  ERROR_INVALID_VERSION = 2, ///< Unsupported protocol version
  ERROR_INCOMPLETE = 3,      ///< Buffer too small for header

  // Payload errors ---------------------------------------------------------
  ERROR_PAYLOAD_TOO_LARGE = 4, ///< Payload exceeds APROTO_MAX_PAYLOAD
  ERROR_BUFFER_TOO_SMALL = 5,  ///< Output buffer insufficient
  ERROR_PAYLOAD_TRUNCATED = 6, ///< Buffer smaller than payloadLength

  // Integrity errors -------------------------------------------------------
  ERROR_CRC_MISMATCH = 7, ///< CRC32 validation failed

  // Encryption errors ------------------------------------------------------
  ERROR_DECRYPT_FAILED = 8,  ///< AEAD decryption/auth failed
  ERROR_ENCRYPT_FAILED = 9,  ///< AEAD encryption failed
  ERROR_INVALID_KEY = 10,    ///< Key index not found or invalid
  ERROR_MISSING_CRYPTO = 11, ///< Encrypted flag set but no crypto metadata

  // Warnings ---------------------------------------------------------------
  WARN_RESERVED_FLAGS = 12, ///< Reserved flag bits are non-zero

  // Marker -----------------------------------------------------------------
  EOE_APROTO
};

/* --------------------------------- API ---------------------------------- */

/**
 * @brief Human-readable string for Status.
 * @param s Status value.
 * @return Static string (no allocation).
 * @note RT-safe: O(1).
 */
const char* toString(Status s) noexcept;

/**
 * @brief Check if status indicates success.
 * @param s Status value.
 * @return true if SUCCESS.
 * @note RT-safe.
 */
[[nodiscard]] inline bool isSuccess(Status s) noexcept { return s == Status::SUCCESS; }

/**
 * @brief Check if status indicates an error.
 * @param s Status value.
 * @return true if ERROR_*.
 * @note RT-safe.
 */
[[nodiscard]] inline bool isError(Status s) noexcept {
  const auto v = static_cast<std::uint8_t>(s);
  const auto warnStart = static_cast<std::uint8_t>(Status::WARN_RESERVED_FLAGS);
  return v != 0 && v < warnStart;
}

/**
 * @brief Check if status indicates a warning.
 * @param s Status value.
 * @return true if WARN_*.
 * @note RT-safe.
 */
[[nodiscard]] inline bool isWarning(Status s) noexcept {
  const auto v = static_cast<std::uint8_t>(s);
  const auto warnStart = static_cast<std::uint8_t>(Status::WARN_RESERVED_FLAGS);
  const auto eoe = static_cast<std::uint8_t>(Status::EOE_APROTO);
  return v >= warnStart && v < eoe;
}

} // namespace aproto
} // namespace protocols
} // namespace system_core

#endif // APEX_SYSTEM_CORE_PROTOCOLS_APROTO_STATUS_HPP
