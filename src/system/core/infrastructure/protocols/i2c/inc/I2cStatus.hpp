#ifndef APEX_PROTOCOLS_I2C_STATUS_HPP
#define APEX_PROTOCOLS_I2C_STATUS_HPP
/**
 * @file I2cStatus.hpp
 * @brief Compact, strongly-typed status codes for I2C device operations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status for I2C device operations.
 *
 * Conventions
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WOULD_BLOCK).
 *  - Errors are prefixed with ERROR_*.
 *
 * Notes
 *  - `WOULD_BLOCK` covers "not ready now" in nonblocking/timeout-based flows.
 *  - `ERROR_NACK` indicates the device did not acknowledge (common for wrong address).
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,
  WOULD_BLOCK, ///< Operation not ready without blocking.
  // Errors:
  ERROR_TIMEOUT,        ///< True timeout from backend/OS.
  ERROR_CLOSED,         ///< Device closed or disconnected.
  ERROR_INVALID_ARG,    ///< Invalid argument (e.g., null buffer, bad address).
  ERROR_NOT_CONFIGURED, ///< Called before successful configure().
  ERROR_IO,             ///< Backend I/O or OS error.
  ERROR_UNSUPPORTED,    ///< Requested feature not supported by device.
  ERROR_BUSY,           ///< Device already in use or bus busy.
  ERROR_NACK            ///< No acknowledge from slave device.
};

/**
 * @brief Human-readable string for Status (cold path, no allocation).
 * @param s Status code to convert.
 * @return String literal describing the status.
 * @note RT-safe: Returns static string literals.
 */
const char* toString(Status s) noexcept;

} // namespace i2c
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_I2C_STATUS_HPP
