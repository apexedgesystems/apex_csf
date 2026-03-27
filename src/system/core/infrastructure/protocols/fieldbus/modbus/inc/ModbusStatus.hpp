#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_STATUS_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_STATUS_HPP
/**
 * @file ModbusStatus.hpp
 * @brief Compact, strongly-typed status codes for Modbus operations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status codes for Modbus operations.
 *
 * Conventions:
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WOULD_BLOCK).
 *  - Errors are prefixed with ERROR_*.
 *
 * Notes:
 *  - `WOULD_BLOCK` covers "not ready now" and "not ready within the caller's
 *    wait policy" in nonblocking/timeout-based flows.
 *  - `ERROR_TIMEOUT` is for true OS/backend timeouts (e.g., ETIMEDOUT), not
 *    for elapsed readiness polling.
 *  - `ERROR_CRC` is specific to Modbus RTU CRC validation failures.
 *  - `ERROR_EXCEPTION` indicates a Modbus exception response was received
 *    (function code has high bit set). Check exceptionCode() for details.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,
  WOULD_BLOCK, ///< Operation not ready without blocking (including elapsed bounded wait).
  // Errors:
  ERROR_TIMEOUT,        ///< True timeout from backend/OS (e.g., ETIMEDOUT).
  ERROR_CLOSED,         ///< Device/transport closed.
  ERROR_INVALID_ARG,    ///< Invalid argument (e.g., bad unit address, register count).
  ERROR_NOT_CONFIGURED, ///< Called before successful configure().
  ERROR_IO,             ///< Backend I/O or OS error.
  ERROR_CRC,            ///< CRC validation failed (RTU only).
  ERROR_FRAME,          ///< Malformed frame (wrong length, bad function code).
  ERROR_EXCEPTION,      ///< Modbus exception response received.
  ERROR_UNSUPPORTED     ///< Requested feature not supported.
};

/**
 * @brief Human-readable string for Status (cold path, no allocation).
 * @param s Status value.
 * @return Static string literal.
 * @note RT-safe: Returns pointer to static string.
 */
const char* toString(Status s) noexcept;

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_STATUS_HPP
