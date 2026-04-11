#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULER_TLM_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULER_TLM_HPP
/**
 * @file SchedulerTlm.hpp
 * @brief Telemetry wire format for Scheduler health queries.
 *
 * Packed POD struct returned by GET_HEALTH (opcode 0x0100) when sent to
 * the Scheduler component (fullUid=0x000100). Provides task scheduling
 * health: tick progress, deadline violations, skip counts.
 *
 * Wire format: little-endian, packed, 32 bytes total.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstdint>

namespace system_core {
namespace scheduler {

/* ----------------------------- SchedulerTlmOpcode ----------------------------- */

/// Telemetry opcodes for Scheduler (component-specific range 0x0100+).
enum class SchedulerTlmOpcode : std::uint16_t {
  GET_HEALTH = 0x0100 ///< Request scheduler health snapshot.
};

/* ----------------------------- SchedulerHealthTlm ----------------------------- */

/**
 * @struct SchedulerHealthTlm
 * @brief Scheduler health telemetry payload.
 *
 * Returned as response payload to GET_HEALTH (opcode 0x0100) when
 * addressed to the Scheduler component.
 *
 * Wire format (32 bytes, little-endian):
 *   Offset  Size  Field
 *   0       8     tickCount              - Total scheduler ticks since startup
 *   --- Task Stats ---
 *   8       4     taskCount              - Number of registered tasks
 *   12      4     totalPeriodViolations  - Cumulative deadline violations
 *   16      4     totalSkipCount         - Cumulative skip-on-busy skips
 *   --- Configuration ---
 *   20      2     fundamentalFreqHz      - Fundamental frequency (Hz)
 *   22      1     poolCount              - Number of thread pools
 *   23      1     sleeping               - Sleep mode active (0/1)
 *   24      1     skipOnBusy             - Skip-on-busy mode enabled (0/1)
 *   25      3     reserved               - Alignment padding
 *   --- Per-Tick ---
 *   28      4     violationsThisTick     - Deadline violations in most recent tick
 *
 * Total: 32 bytes.
 */
struct __attribute__((packed)) SchedulerHealthTlm {
  /* ----------------------------- Progress ----------------------------- */

  std::uint64_t tickCount{0}; ///< Total ticks since startup.

  /* ----------------------------- Task Stats ----------------------------- */

  std::uint32_t taskCount{0};             ///< Number of registered tasks.
  std::uint32_t totalPeriodViolations{0}; ///< Cumulative deadline violations.
  std::uint32_t totalSkipCount{0};        ///< Cumulative skip-on-busy skips.

  /* ----------------------------- Configuration ----------------------------- */

  std::uint16_t fundamentalFreqHz{0}; ///< Fundamental frequency (Hz).
  std::uint8_t poolCount{0};          ///< Number of thread pools.
  std::uint8_t sleeping{0};           ///< Sleep mode active (0=running, 1=sleeping).
  std::uint8_t skipOnBusy{0};         ///< Skip-on-busy mode (0=disabled, 1=enabled).
  std::uint8_t reserved[3]{};         ///< Alignment padding.

  /* ----------------------------- Per-Tick ----------------------------- */

  std::uint32_t violationsThisTick{0}; ///< Deadline violations in most recent tick.
};

static_assert(sizeof(SchedulerHealthTlm) == 32, "SchedulerHealthTlm size mismatch");

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULER_TLM_HPP
