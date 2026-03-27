#ifndef APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_STATUS_HPP
#define APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_STATUS_HPP
/**
 * @file RfcommStatus.hpp
 * @brief Status codes for Bluetooth RFCOMM operations.
 *
 * Provides a unified status enum following the protocol library pattern
 * used by UART, Modbus, and LIN libraries.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status codes for RFCOMM operations.
 *
 * Status values are ordered by severity:
 * - SUCCESS (0) = operation completed
 * - WOULD_BLOCK = non-error, retry later
 * - ERROR_* = actual errors
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,              ///< Operation completed successfully
  WOULD_BLOCK,              ///< Not ready within timeout policy
  ERROR_TIMEOUT,            ///< True OS timeout (ETIMEDOUT)
  ERROR_CLOSED,             ///< Connection closed/disconnected
  ERROR_INVALID_ARG,        ///< Invalid argument
  ERROR_NOT_CONFIGURED,     ///< configure() not called or failed
  ERROR_IO,                 ///< I/O or OS error
  ERROR_CONNECTION_REFUSED, ///< Remote refused connection (ECONNREFUSED)
  ERROR_HOST_UNREACHABLE,   ///< Remote host unreachable (EHOSTUNREACH)
  ERROR_ALREADY_CONNECTED,  ///< Already connected
  ERROR_NOT_CONNECTED       ///< Not connected (call connect() first)
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Convert Status to human-readable string.
 * @param status Status value to convert.
 * @return Pointer to static string (never null).
 * @note RT-safe: Returns static string, no allocation.
 */
const char* toString(Status status) noexcept;

/**
 * @brief Check if status represents success.
 * @param status Status value to check.
 * @return true if SUCCESS.
 * @note RT-safe: O(1), no allocation.
 */
inline bool isSuccess(Status status) noexcept { return status == Status::SUCCESS; }

/**
 * @brief Check if status represents an error.
 * @param status Status value to check.
 * @return true if any ERROR_* value.
 * @note RT-safe: O(1), no allocation.
 */
inline bool isError(Status status) noexcept {
  return status != Status::SUCCESS && status != Status::WOULD_BLOCK;
}

/**
 * @brief Check if operation should be retried.
 * @param status Status value to check.
 * @return true if WOULD_BLOCK.
 * @note RT-safe: O(1), no allocation.
 */
inline bool shouldRetry(Status status) noexcept { return status == Status::WOULD_BLOCK; }

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_STATUS_HPP
