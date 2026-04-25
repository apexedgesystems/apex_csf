#ifndef APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_STATUS_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_STATUS_HPP
/**
 * @file ExecutiveStatus.hpp
 * @brief Compact, strongly-typed status codes for executive operations.
 */

#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <cstdint>

namespace executive {

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status for ApexExecutive operations.
 *
 * Conventions:
 *  - SUCCESS = 0.
 *  - Errors are prefixed with ERROR_*.
 *  - Warnings are prefixed with WARN_*.
 *  - Errors begin after system_component::Status::EOE_SYSTEM_COMPONENT.
 */
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0,

  // Error codes (start after SystemComponent base) ---------------------------
  ERROR_MODULE_INIT_FAIL =
      static_cast<std::uint8_t>(system_core::system_component::Status::EOE_SYSTEM_COMPONENT),
  ERROR_ARG_PARSE_FAIL,
  ERROR_RUNTIME_FAILURE,
  ERROR_SHUTDOWN_FAILURE,
  ERROR_HARD_REALTIME_FAILURE,
  ERROR_COMPONENT_COLLISION,
  ERROR_SIGNAL_BLOCK_FAILED,
  ERROR_SCHEDULER_NO_TASKS,
  ERROR_CONFIG_NOT_FOUND,
  ERROR_TPRM_UNPACK_FAIL,

  // Warning codes -------------------------------------------------------------
  WARN_CLOCK_DRIFT,
  WARN_FRAME_OVERRUN,
  WARN_REGISTRY_EXPORT,
  WARN_CLOCK_FROZEN,
  WARN_CLOCK_STOP_TIMEOUT,
  WARN_TASK_DRAIN_TIMEOUT,
  WARN_IO_ERROR,
  WARN_STARTUP_TIME_PASSED,
  WARN_INVALID_CLOCK_FREQ,
  WARN_THREAD_CONFIG_FAIL,
  WARN_TPRM_LOAD_FAIL,
  WARN_SWAP_FAILED,
  WARN_QUEUE_ALLOC_FAIL,

  // Marker --------------------------------------------------------------------
  EOE_EXECUTIVE
};

/* ----------------------------- CommandResult (Executive) ----------------------------- */

/**
 * @enum ExecCommandResult
 * @brief Executive-specific handleCommand() result codes (values 16+).
 *
 * Common codes (0-6) are in system_component::CommandResult.
 * These extend that range for hot-swap and executive-specific operations.
 */
enum class ExecCommandResult : std::uint8_t {
  DLOPEN_FAILED = ///< Dynamic library open failed.
  static_cast<std::uint8_t>(system_core::system_component::CommandResult::EOE_COMMAND_RESULT),
  NOT_SWAPPABLE,   ///< Component not locked or identity mismatch.
  INIT_FAILED,     ///< Component initialization failed after swap.
  TASK_MISMATCH,   ///< New component task count differs from old.
  REGISTRY_FAILED, ///< Registry re-population failed after swap.
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Human-readable string for Status (cold path, no allocation).
 * @param s Status code.
 * @return Static string (no allocation).
 * @note RT-safe: Returns pointer to static string literal.
 */
const char* toString(Status s) noexcept;

/**
 * @brief Check if status is SUCCESS.
 * @param s Status code.
 * @return true if s == SUCCESS.
 */
[[nodiscard]] inline constexpr bool isSuccess(Status s) noexcept { return s == Status::SUCCESS; }

/**
 * @brief Check if status is an error (ERROR_* codes).
 * @param s Status code.
 * @return true if s is an error code.
 */
[[nodiscard]] inline constexpr bool isError(Status s) noexcept {
  const auto v = static_cast<std::uint8_t>(s);
  const auto errStart = static_cast<std::uint8_t>(Status::ERROR_MODULE_INIT_FAIL);
  const auto warnStart = static_cast<std::uint8_t>(Status::WARN_CLOCK_DRIFT);
  return v >= errStart && v < warnStart;
}

/**
 * @brief Check if status is a warning (WARN_* codes).
 * @param s Status code.
 * @return true if s is a warning code.
 */
[[nodiscard]] inline constexpr bool isWarning(Status s) noexcept {
  const auto v = static_cast<std::uint8_t>(s);
  const auto warnStart = static_cast<std::uint8_t>(Status::WARN_CLOCK_DRIFT);
  const auto eoe = static_cast<std::uint8_t>(Status::EOE_EXECUTIVE);
  return v >= warnStart && v < eoe;
}

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_STATUS_HPP
