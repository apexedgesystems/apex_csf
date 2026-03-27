#ifndef APEX_SYSTEM_CORE_BASE_SYSTEM_COMPONENT_STATUS_HPP
#define APEX_SYSTEM_CORE_BASE_SYSTEM_COMPONENT_STATUS_HPP
/**
 * @file SystemComponentStatus.hpp
 * @brief Compact, strongly-typed status codes for system components.
 *
 * Part of the base interface layer - no heavy dependencies.
 *
 * Conventions:
 *  - SUCCESS = 0.
 *  - Non-errors use plain names (e.g., WARN_NOOP).
 *  - Errors are prefixed with ERROR_*.
 *  - EOE_SYSTEM_COMPONENT marks the end of base codes; derivatives extend after it.
 */

#include <stdint.h>

namespace system_core {
namespace system_component {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Base status codes for SystemComponent hierarchy.
 *
 * Derived components extend from EOE_SYSTEM_COMPONENT to avoid collisions.
 */
enum class Status : uint8_t {
  // Success -------------------------------------------------------------
  SUCCESS = 0,

  // Warnings ------------------------------------------------------------
  WARN_NOOP, ///< Operation had no effect.

  // Errors --------------------------------------------------------------
  ERROR_PARAM,               ///< Invalid argument / out-of-range.
  ERROR_ALREADY_INITIALIZED, ///< init() called on already-initialized component.
  ERROR_NOT_INITIALIZED,     ///< Operation requires prior init().
  ERROR_NOT_CONFIGURED,      ///< init() called without prior load().
  ERROR_LOAD_INVALID,        ///< load() validation failed.
  ERROR_CONFIG_APPLY_FAIL,   ///< configure() apply failed; active(A) unchanged.

  // End-of-enum marker --------------------------------------------------
  EOE_SYSTEM_COMPONENT
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Human-readable string for Status.
 * @param s Status code.
 * @return Static string (no allocation).
 * @note RT-safe: Returns pointer to static string literal.
 */
[[nodiscard]] inline const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::WARN_NOOP:
    return "WARN_NOOP";
  case Status::ERROR_PARAM:
    return "ERROR_PARAM";
  case Status::ERROR_ALREADY_INITIALIZED:
    return "ERROR_ALREADY_INITIALIZED";
  case Status::ERROR_NOT_INITIALIZED:
    return "ERROR_NOT_INITIALIZED";
  case Status::ERROR_NOT_CONFIGURED:
    return "ERROR_NOT_CONFIGURED";
  case Status::ERROR_LOAD_INVALID:
    return "ERROR_LOAD_INVALID";
  case Status::ERROR_CONFIG_APPLY_FAIL:
    return "ERROR_CONFIG_APPLY_FAIL";
  case Status::EOE_SYSTEM_COMPONENT:
    return "EOE_SYSTEM_COMPONENT";
  }
  return "UNKNOWN_STATUS";
}

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_BASE_SYSTEM_COMPONENT_STATUS_HPP
