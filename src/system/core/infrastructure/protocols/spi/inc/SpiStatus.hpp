#ifndef APEX_PROTOCOLS_SPI_STATUS_HPP
#define APEX_PROTOCOLS_SPI_STATUS_HPP
/**
 * @file SpiStatus.hpp
 * @brief Compact, strongly-typed status codes for SPI device operations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status for SPI device operations.
 *
 * Conventions
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WOULD_BLOCK).
 *  - Errors are prefixed with ERROR_*.
 *
 * Notes
 *  - `WOULD_BLOCK` covers "not ready now" and "not ready within the caller's wait policy"
 *    in nonblocking/timeout-based flows.
 *  - `ERROR_TIMEOUT` is reserved for true OS/kernel timeouts (e.g., an operation that
 *    returns ETIMEDOUT), not for elapsed readiness polling.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,
  WOULD_BLOCK, ///< Operation not ready without blocking (including elapsed bounded wait).
  // Errors:
  ERROR_TIMEOUT,        ///< True timeout from backend/OS (e.g., ETIMEDOUT).
  ERROR_CLOSED,         ///< Device closed or disconnected.
  ERROR_INVALID_ARG,    ///< Invalid argument (e.g., null buffer, invalid mode).
  ERROR_NOT_CONFIGURED, ///< Called before successful configure().
  ERROR_IO,             ///< Backend I/O or OS error.
  ERROR_UNSUPPORTED,    ///< Requested feature not supported by device.
  ERROR_BUSY            ///< Device already in use or locked.
};

/**
 * @brief Human-readable string for Status (cold path, no allocation).
 * @param s Status code to convert.
 * @return String literal describing the status.
 * @note RT-safe: Returns static string literals.
 */
const char* toString(Status s) noexcept;

} // namespace spi
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SPI_STATUS_HPP
