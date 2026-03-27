#ifndef APEX_PROTOCOLS_FIELDBUS_LIN_STATUS_HPP
#define APEX_PROTOCOLS_FIELDBUS_LIN_STATUS_HPP
/**
 * @file LinStatus.hpp
 * @brief Compact, strongly-typed status codes for LIN operations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace lin {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status codes for LIN operations.
 *
 * Conventions:
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WOULD_BLOCK).
 *  - Errors are prefixed with ERROR_*.
 *
 * Notes:
 *  - `WOULD_BLOCK` covers "not ready now" in nonblocking/timeout-based flows.
 *  - `ERROR_TIMEOUT` is for true OS/backend timeouts (e.g., ETIMEDOUT).
 *  - `ERROR_CHECKSUM` is specific to LIN checksum validation failures.
 *  - `ERROR_SYNC` indicates LIN sync field (0x55) not detected.
 *  - `ERROR_PARITY` indicates protected identifier parity error.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,
  WOULD_BLOCK, ///< Operation not ready without blocking.
  // Errors:
  ERROR_TIMEOUT,        ///< True timeout from backend/OS (e.g., ETIMEDOUT).
  ERROR_CLOSED,         ///< Device/transport closed.
  ERROR_INVALID_ARG,    ///< Invalid argument (e.g., bad frame ID).
  ERROR_NOT_CONFIGURED, ///< Called before successful configure().
  ERROR_IO,             ///< Backend I/O or OS error.
  ERROR_CHECKSUM,       ///< Checksum validation failed.
  ERROR_SYNC,           ///< Sync field (0x55) not received.
  ERROR_PARITY,         ///< Protected ID parity error.
  ERROR_FRAME,          ///< Malformed frame (wrong length, unexpected data).
  ERROR_NO_RESPONSE,    ///< Slave did not respond within slot time.
  ERROR_BUS_COLLISION,  ///< Bus collision detected (readback mismatch).
  ERROR_BREAK           ///< Break field generation/detection failed.
};

/**
 * @brief Human-readable string for Status (cold path, no allocation).
 * @param s Status value.
 * @return Static string literal.
 * @note RT-safe: Returns pointer to static string.
 */
const char* toString(Status s) noexcept;

} // namespace lin
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_LIN_STATUS_HPP
