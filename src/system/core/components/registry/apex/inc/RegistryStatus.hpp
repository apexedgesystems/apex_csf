#ifndef APEX_SYSTEM_CORE_REGISTRY_REGISTRY_STATUS_HPP
#define APEX_SYSTEM_CORE_REGISTRY_REGISTRY_STATUS_HPP
/**
 * @file RegistryStatus.hpp
 * @brief Compact, strongly-typed status codes for registry operations.
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <cstdint>

namespace system_core {
namespace registry {

/**
 * @enum Status
 * @brief Status for ApexRegistry operations.
 *
 * Conventions
 *  - SUCCESS = 0.
 *  - Errors: ERROR_*. Warnings: WARN_*.
 *  - Errors begin after system_component::Status::EOE_SYSTEM_COMPONENT.
 */
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0,

  // Registration errors -------------------------------------------------------
  ERROR_ALREADY_FROZEN = static_cast<std::uint8_t>(system_component::Status::EOE_SYSTEM_COMPONENT),
  ERROR_NOT_FROZEN,          ///< Query before freeze().
  ERROR_NULL_POINTER,        ///< Null data pointer provided.
  ERROR_DUPLICATE_COMPONENT, ///< Component with fullUid already registered.
  ERROR_DUPLICATE_TASK,      ///< Task with fullUid+taskUid already registered.
  ERROR_DUPLICATE_DATA,      ///< Data with fullUid+category already registered.
  ERROR_COMPONENT_NOT_FOUND, ///< Referenced component not registered.
  ERROR_CAPACITY_EXCEEDED,   ///< Maximum entries exceeded.
  ERROR_INVALID_CATEGORY,    ///< Invalid DataCategory value.
  ERROR_ZERO_SIZE,           ///< Zero-size data registration.

  // Query errors --------------------------------------------------------------
  ERROR_NOT_FOUND, ///< Requested entry not found.

  // I/O errors ----------------------------------------------------------------
  ERROR_IO, ///< File I/O error during export/import.

  // Warnings ------------------------------------------------------------------
  WARN_EMPTY_NAME, ///< Empty name string provided.

  // Marker --------------------------------------------------------------------
  EOE_REGISTRY
};

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
 */
[[nodiscard]] inline bool isSuccess(Status s) noexcept { return s == Status::SUCCESS; }

/**
 * @brief Check if status indicates an error.
 * @param s Status value.
 * @return true if ERROR_*.
 */
[[nodiscard]] inline bool isError(Status s) noexcept {
  const auto v = static_cast<std::uint8_t>(s);
  const auto warnStart = static_cast<std::uint8_t>(Status::WARN_EMPTY_NAME);
  return v != 0 && v < warnStart;
}

/**
 * @brief Check if status indicates a warning.
 * @param s Status value.
 * @return true if WARN_*.
 */
[[nodiscard]] inline bool isWarning(Status s) noexcept {
  const auto v = static_cast<std::uint8_t>(s);
  const auto warnStart = static_cast<std::uint8_t>(Status::WARN_EMPTY_NAME);
  const auto eoe = static_cast<std::uint8_t>(Status::EOE_REGISTRY);
  return v >= warnStart && v < eoe;
}

} // namespace registry
} // namespace system_core

#endif // APEX_SYSTEM_CORE_REGISTRY_REGISTRY_STATUS_HPP
