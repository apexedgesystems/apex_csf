#ifndef APEX_SYSTEM_CORE_INTERFACE_STATUS_HPP
#define APEX_SYSTEM_CORE_INTERFACE_STATUS_HPP
/**
 * @file InterfaceStatus.hpp
 * @brief Compact, strongly-typed status codes for interface component operations.
 *
 * Conventions:
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WARN_QUEUE_OVERFLOW).
 *  - Errors are prefixed with ERROR_*.
 *  - Extends from SystemComponentStatus::EOE_SYSTEM_COMPONENT.
 *  - EOE_INTERFACE marks the end of interface codes.
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <cstdint>

namespace system_core {
namespace interface {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status codes for interface component operations.
 *
 * Extends from system_component::Status to allow unified error handling.
 */
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------
  SUCCESS = 0,

  // Extend from SystemComponentStatus base errors -----------------------
  ERROR_NOT_INITIALIZED = static_cast<std::uint8_t>(system_component::Status::EOE_SYSTEM_COMPONENT),

  // Lifecycle errors ----------------------------------------------------
  ERROR_ALREADY_INITIALIZED, ///< configure() called on already-initialized interface.
  ERROR_CONFIG,              ///< Invalid configuration parameters.
  ERROR_CREATE_SERVER,       ///< Failed to create socket server.
  ERROR_BIND_OR_LISTEN,      ///< Failed to bind or listen on socket.

  // Runtime errors ------------------------------------------------------
  ERROR_CHANNEL_CLOSED,      ///< Channel is not connected.
  ERROR_SEND_FAILED,         ///< Failed to send data on channel.
  ERROR_RECV_FAILED,         ///< Failed to receive data on channel.
  ERROR_INVALID_PACKET,      ///< Malformed or invalid packet received.
  ERROR_ROUTE_FAILED,        ///< Failed to route command to component.
  ERROR_QUEUE_FULL,          ///< Queue is full, message dropped.
  ERROR_COMPONENT_NOT_FOUND, ///< Target component not found in registry.

  // Warnings ------------------------------------------------------------
  WARN_QUEUE_OVERFLOW, ///< Queue overflowed, oldest message dropped.

  // End-of-enum marker --------------------------------------------------
  EOE_INTERFACE
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Human-readable string for Status.
 * @param s Status code.
 * @return Static string (no allocation).
 * @note RT-safe: Returns pointer to static string literal.
 */
const char* toString(Status s) noexcept;

/**
 * @brief Check if status indicates success.
 * @param s Status code.
 * @return True if SUCCESS.
 * @note RT-safe.
 */
[[nodiscard]] inline bool isSuccess(Status s) noexcept { return s == Status::SUCCESS; }

/**
 * @brief Check if status indicates an error.
 * @param s Status code.
 * @return True if ERROR_* code.
 * @note RT-safe.
 */
[[nodiscard]] inline bool isError(Status s) noexcept {
  const auto val = static_cast<std::uint8_t>(s);
  const auto notInit = static_cast<std::uint8_t>(Status::ERROR_NOT_INITIALIZED);
  const auto warnStart = static_cast<std::uint8_t>(Status::WARN_QUEUE_OVERFLOW);
  return val >= notInit && val < warnStart;
}

/**
 * @brief Check if status indicates a warning.
 * @param s Status code.
 * @return True if WARN_* code.
 * @note RT-safe.
 */
[[nodiscard]] inline bool isWarning(Status s) noexcept { return s == Status::WARN_QUEUE_OVERFLOW; }

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_STATUS_HPP
