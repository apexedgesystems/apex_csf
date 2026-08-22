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
  GET_HEALTH = 0x0100,      ///< Request scheduler health snapshot.
  GET_TASK_STATS = 0x0101,  ///< Request per-task runtime stats snapshot.
  RUN_TASK_CENSUS = 0x0102, ///< Execute the pre-clock task census.
  GET_TASK_CENSUS = 0x0103, ///< Request the census report.
  RUN_PREFLIGHT = 0x0104,   ///< Re-run the static table feasibility analysis.
  GET_PREFLIGHT = 0x0105,   ///< Request the table feasibility verdicts.
  GET_REQUIREMENTS = 0x0106 ///< Request the scheduler's host requirements.
};

/* ----------------------------- Requirements ----------------------------- */

/// Maximum pools reported in the requirements snapshot.
inline constexpr std::size_t REQUIREMENTS_POOL_CAP = 8;

#pragma pack(push, 1)
/**
 * @struct PoolRequirementRowTlm
 * @brief One pool's requested host configuration (16 bytes).
 *
 * What the scheduler ASKS the host for; whether the host granted it is
 * the consumer's correlation to make (RT-config warnings, isolcpus,
 * governor) -- the scheduler publishes needs, not judgments.
 */
struct PoolRequirementRowTlm {
  std::uint8_t poolId{0};        ///< Pool index.
  std::uint8_t policy{0};        ///< Requested POSIX policy (0/1/2 = OTHER/FIFO/RR).
  std::int8_t priority{0};       ///< Requested POSIX priority.
  std::uint8_t pad{0};           ///< Alignment.
  std::uint16_t workers{0};      ///< Constructed worker count.
  std::uint16_t pad2{0};         ///< Alignment.
  std::uint64_t affinityMask{0}; ///< Requested CPU set (bit N = CPU N; 0 = any).
};

/**
 * @struct SchedulerRequirementsTlm
 * @brief The scheduler's declared host requirements.
 *
 * Returned for GET_REQUIREMENTS and registered as an INSPECT OUTPUT so
 * any consumer can correlate host posture against declared needs.
 */
struct SchedulerRequirementsTlm {
  std::uint16_t fundamentalFreqHz{0}; ///< Tick rate the clock must hold.
  std::uint8_t poolCount{0};          ///< Rows populated.
  std::uint8_t seqGroupCount{0};      ///< Sequence groups in the table.
  std::uint16_t taskCount{0};         ///< Scheduled tasks.
  std::uint16_t pad{0};               ///< Alignment.
  PoolRequirementRowTlm pools[REQUIREMENTS_POOL_CAP]{};
};
#pragma pack(pop)

/* ----------------------------- Task Stats ----------------------------- */

/// Maximum tasks reported in one stats snapshot.
inline constexpr std::size_t TASK_STATS_TLM_CAP = 32;

#pragma pack(push, 1)
/**
 * @struct TaskStatsRowTlm
 * @brief One task's runtime counters (16 bytes, little-endian).
 */
struct TaskStatsRowTlm {
  std::uint32_t fullUid{0};         ///< Owner component fullUid.
  std::uint8_t taskUid{0};          ///< Task UID within the component.
  std::uint8_t pad{0};              ///< Alignment.
  std::uint16_t lastRuntimeUs16{0}; ///< Most recent runtime (us, saturated).
  std::uint32_t maxRuntimeUs{0};    ///< Worst dispatch-to-complete (us).
  std::uint16_t overruns16{0};      ///< Period overruns (saturated).
  std::uint16_t violations16{0};    ///< Deadline violations (saturated).
};

/* ----------------------------- Task Census ----------------------------- */

#pragma pack(push, 1)
/**
 * @struct CensusRowTlm
 * @brief One task's measured cost vs its period budget (20 bytes).
 *
 * verdict: 0 = PASS, 1 = WARN (first run exceeds the budget -- expect a
 * startup violation), 2 = FAIL (steady state exceeds the budget -- the
 * task cannot hold its period at all).
 */
struct CensusRowTlm {
  std::uint32_t fullUid{0};  ///< Owner component fullUid.
  std::uint8_t taskUid{0};   ///< Task UID within the component.
  std::uint8_t verdict{0};   ///< 0 PASS / 1 WARN / 2 FAIL.
  std::uint16_t pad{0};      ///< Alignment.
  std::uint32_t firstUs{0};  ///< First execution (cold: lazy inits land here).
  std::uint32_t steadyUs{0}; ///< Fastest of the remaining repetitions.
  std::uint32_t budgetUs{0}; ///< Period budget (0 = unknown).
};

/**
 * @struct SchedulerCensusTlm
 * @brief Pre-clock task census report.
 *
 * Returned for GET_TASK_CENSUS and registered as an INSPECT OUTPUT.
 */
struct SchedulerCensusTlm {
  std::uint16_t taskCount{0}; ///< Rows populated (0 = census never ran).
  std::uint8_t truncated{0};  ///< 1 when tasks exceeded the row cap.
  std::uint8_t overall{0};    ///< Worst row verdict.
  CensusRowTlm rows[TASK_STATS_TLM_CAP]{};
};
#pragma pack(pop)

/**
 * @struct SchedulerTaskStatsTlm
 * @brief Per-task runtime stats snapshot.
 *
 * Returned as response payload to GET_TASK_STATS and registered as an
 * INSPECT OUTPUT; rows populated at snapshot time from the live
 * counters (readers never touch scheduler internals).
 */
struct SchedulerTaskStatsTlm {
  std::uint16_t taskCount{0}; ///< Rows populated.
  std::uint16_t truncated{0}; ///< 1 when tasks exceeded the row cap.
  TaskStatsRowTlm rows[TASK_STATS_TLM_CAP]{};
};
#pragma pack(pop)

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
