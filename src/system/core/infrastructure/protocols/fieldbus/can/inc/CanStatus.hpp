#ifndef APEX_PROTOCOLS_FIELDBUS_CAN_CAN_STATUS_HPP
#define APEX_PROTOCOLS_FIELDBUS_CAN_CAN_STATUS_HPP
/**
 * @file CanStatus.hpp
 * @brief Compact, strongly-typed status codes for CAN device operations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

/* -------------------------------- Status -------------------------------- */

/**
 * @enum Status
 * @brief Status for CAN device operations.
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
 *    returns ETIMEDOUT), not for elapsed readiness polling--backend-specific.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,
  WOULD_BLOCK, ///< Operation not ready without blocking (including elapsed bounded wait).
  // Errors:
  ERROR_TIMEOUT,        ///< True timeout from backend/OS (e.g., ETIMEDOUT).
  ERROR_CLOSED,         ///< Device/channel closed.
  ERROR_INVALID_ARG,    ///< Invalid argument (e.g., bad DLC/ID/flags).
  ERROR_NOT_CONFIGURED, ///< Called before successful configure().
  ERROR_IO,             ///< Backend I/O or OS error.
  ERROR_UNSUPPORTED     ///< Requested feature not supported by backend.
};

/* ---------------------------------- API --------------------------------- */

/**
 * @brief Human-readable string for Status (cold path, no allocation).
 * @note RT-safe: no allocation or I/O
 */
const char* toString(Status s) noexcept;

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_CAN_CAN_STATUS_HPP
