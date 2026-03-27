#ifndef APEX_SYSTEM_CORE_FILESYSTEM_STATUS_HPP
#define APEX_SYSTEM_CORE_FILESYSTEM_STATUS_HPP
/**
 * @file FileSystemStatus.hpp
 * @brief Compact, strongly-typed status codes for filesystem operations.
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include <cstdint>

namespace system_core {
namespace filesystem {

/**
 * @enum Status
 * @brief Status for FileSystemBase operations.
 *
 * Conventions
 *  - SUCCESS = 0.
 *  - Errors are prefixed with ERROR_*.
 *  - Errors begin after system_component::Status::EOE_SYSTEM_COMPONENT.
 */
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0,

  // Filesystem-specific (start after SystemComponent base) --------------------
  ERROR_FS_CREATION_FAIL =
      static_cast<std::uint8_t>(system_component::Status::EOE_SYSTEM_COMPONENT),
  ERROR_FS_TAR_CREATE_FAIL,
  ERROR_FS_TAR_MOVE_FAIL,
  ERROR_INVALID_FS,

  // Marker --------------------------------------------------------------------
  EOE_FILESYSTEM
};

/** @brief Human-readable string for Status (cold path, no allocation). */
const char* toString(Status s) noexcept;

} // namespace filesystem
} // namespace system_core

#endif // APEX_SYSTEM_CORE_FILESYSTEM_STATUS_HPP
