#ifndef APEX_SYSTEM_CORE_ACTION_ACTIONCOMPONENTSTATUS_HPP
#define APEX_SYSTEM_CORE_ACTION_ACTIONCOMPONENTSTATUS_HPP
/**
 * @file ActionComponentStatus.hpp
 * @brief Compact, strongly-typed status codes for ActionComponent operations.
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <cstdint>

namespace system_core {
namespace action {

/**
 * @enum Status
 * @brief Status for ActionComponent.
 *
 * Conventions
 *  - SUCCESS = 0.
 *  - Errors: ERROR_*. Warnings: WARN_*.
 *  - Errors begin after system_component::Status::EOE_SYSTEM_COMPONENT.
 */
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0,

  // Action component errors ---------------------------------------------------
  ERROR_NO_RESOLVER = static_cast<std::uint8_t>(system_component::Status::EOE_SYSTEM_COMPONENT),
  ERROR_QUEUE_FULL,

  // Warnings ------------------------------------------------------------------
  WARN_RESOLVE_FAILURES, ///< One or more targets could not be resolved.

  // Marker --------------------------------------------------------------------
  EOE_ACTION
};

/** @brief Human-readable string for Status (cold path, no allocation). */
const char* toString(Status s) noexcept;

} // namespace action
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ACTION_ACTIONCOMPONENTSTATUS_HPP
