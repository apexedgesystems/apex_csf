#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSTATUS_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSTATUS_HPP
/**
 * @file SchedulerStatus.hpp
 * @brief Compact, strongly-typed status codes for scheduler operations.
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include <cstdint>

namespace system_core {
namespace scheduler {

/**
 * @enum Status
 * @brief Status for SchedulerBase and derivatives.
 *
 * Conventions
 *  - SUCCESS = 0.
 *  - Errors: ERROR_*. Warnings: WARN_*.
 *  - Errors begin after system_component::Status::EOE_SYSTEM_COMPONENT.
 */
enum class Status : std::uint8_t {
  // Success -------------------------------------------------------------------
  SUCCESS = 0,

  // Scheduler base ------------------------------------------------------------
  ERROR_TFREQN_GT_FFREQ = static_cast<std::uint8_t>(system_component::Status::EOE_SYSTEM_COMPONENT),
  ERROR_FFREQ_MOD_TFREQN_DNE0,
  ERROR_OFFSET_GTE_TPT,
  ERROR_TFREQD_LT1,
  ERROR_TASK_EXECUTION_FAIL,
  ERROR_THREAD_LAUNCH_FAIL,

  // I/O errors ----------------------------------------------------------------
  ERROR_IO, ///< File I/O error (export/import).

  // Derivative: multi-thread / thread-pool -----------------------------------
  ERROR_THREADPOOL_OVERLOAD,
  ERROR_AFFINITY_SETTING_FAIL,
  ERROR_POLICY_SETTING_FAIL,

  // Warnings ------------------------------------------------------------------
  WARN_TASK_STARVATION,
  WARN_TASK_NON_SUCCESS_RET,
  WARN_CPU_UNDERUTILIZATION,
  WARN_PERIOD_VIOLATION, ///< Task still running when next invocation due.

  // Marker --------------------------------------------------------------------
  EOE_SCHEDULER
};

/** @brief Human-readable string for Status (cold path, no allocation). */
const char* toString(Status s) noexcept;

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERSTATUS_HPP