#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULER_DATA_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULER_DATA_HPP
/**
 * @file SchedulerData.hpp
 * @brief TPRM data structures for scheduler configuration.
 *
 * Defines the binary-serializable configuration for scheduler initialization.
 * The scheduler tprm contains:
 *   - Header: pool configuration and task count
 *   - Task entries: scheduling parameters for each task
 *
 * Executive uses these entries to wire models/tasks to the scheduler.
 * The scheduler itself doesn't know about models - it just receives
 * task pointers and configs from the executive.
 *
 * @note All structures are packed for binary serialization via hex2cpp.
 */

#include <cstdint>

namespace system_core {
namespace scheduler {

/* ----------------------------- Constants ----------------------------- */

/// Indicates no sequence group (task runs independently).
constexpr std::uint8_t NO_SEQUENCE_GROUP = 0xFF;

/* ----------------------------- SchedulerTprmHeader ----------------------------- */

#pragma pack(push, 1)

/**
 * @struct SchedulerTprmHeader
 * @brief Header for scheduler tprm configuration.
 *
 * Fixed-size header at the start of the scheduler tprm.
 * Followed by numTasks SchedulerTaskEntry structs.
 */
struct SchedulerTprmHeader {
  std::uint8_t numPools;       ///< Number of thread pools to create.
  std::uint8_t workersPerPool; ///< Default worker count per pool.
  std::uint8_t numTasks;       ///< Number of task entries following header.
};

static_assert(sizeof(SchedulerTprmHeader) == 3, "SchedulerTprmHeader must be exactly 3 bytes");

/* ----------------------------- SchedulerTaskEntry ----------------------------- */

/**
 * @struct SchedulerTaskEntry
 * @brief TPRM entry defining how to schedule a single task.
 *
 * Executive uses fullUid + taskUid to resolve to a task pointer,
 * then constructs a TaskConfig from the scheduling parameters.
 *
 * Fields:
 *   - fullUid: Component full UID (componentId << 8 | instanceIndex)
 *   - taskUid: Task UID within the component (defined by component)
 *   - poolIndex: Which thread pool to run on (0 = default)
 *   - freqN/freqD: Frequency as numerator/denominator (effective Hz = freqN/freqD)
 *   - offset: Tick offset within the task's period
 *   - priority: Logical priority [-128, 127]
 *   - sequenceGroup: Component's sequence group index (0xFF = no sequencing)
 *   - sequencePhase: Phase within sequence group (ignored if no sequencing)
 *
 * Full UID structure (24 bits used, stored in 32-bit field):
 *   - Bits 8-23: componentId (16 bits)
 *   - Bits 0-7: instanceIndex (8 bits)
 *
 * UID ranges:
 *   - 0: Executive
 *   - 1-100: System components (Scheduler=1, FileSystem=2, etc.)
 *   - 101+: Models
 */
struct SchedulerTaskEntry {
  std::uint32_t fullUid;      ///< Component full UID (componentId << 8 | instanceIndex).
  std::uint8_t taskUid;       ///< Task UID within component.
  std::uint8_t poolIndex;     ///< Thread pool index (0 = default).
  std::uint16_t freqN;        ///< Frequency numerator (Hz).
  std::uint16_t freqD;        ///< Frequency denominator (>=1).
  std::uint16_t offset;       ///< Tick offset within period.
  std::int8_t priority;       ///< Logical priority [-128, 127].
  std::uint8_t sequenceGroup; ///< Sequence group index (0xFF = none).
  std::uint8_t sequencePhase; ///< Phase within sequence group.
};

static_assert(sizeof(SchedulerTaskEntry) == 15, "SchedulerTaskEntry must be exactly 15 bytes");

#pragma pack(pop)

/* ----------------------------- Helper Functions ----------------------------- */

/**
 * @brief Calculate expected tprm size for validation.
 * @param numTasks Number of task entries.
 * @return Expected total size in bytes.
 */
[[nodiscard]] constexpr std::size_t schedulerTprmSize(std::uint8_t numTasks) noexcept {
  return sizeof(SchedulerTprmHeader) + (numTasks * sizeof(SchedulerTaskEntry));
}

/**
 * @brief Check if a sequence group value indicates no sequencing.
 * @param group Sequence group index.
 * @return true if task should run independently.
 */
[[nodiscard]] constexpr bool isUnsequenced(std::uint8_t group) noexcept {
  return group == NO_SEQUENCE_GROUP;
}

/**
 * @brief Extract componentId from fullUid.
 * @param fullUid Full UID (componentId << 8 | instanceIndex).
 * @return 16-bit component ID.
 */
[[nodiscard]] constexpr std::uint16_t extractComponentId(std::uint32_t fullUid) noexcept {
  return static_cast<std::uint16_t>(fullUid >> 8);
}

/**
 * @brief Extract instanceIndex from fullUid.
 * @param fullUid Full UID (componentId << 8 | instanceIndex).
 * @return 8-bit instance index.
 */
[[nodiscard]] constexpr std::uint8_t extractInstanceIndex(std::uint32_t fullUid) noexcept {
  return static_cast<std::uint8_t>(fullUid & 0xFF);
}

/**
 * @brief Compose fullUid from componentId and instanceIndex.
 * @param componentId 16-bit component type ID.
 * @param instanceIndex 8-bit instance index.
 * @return 32-bit full UID.
 */
[[nodiscard]] constexpr std::uint32_t composeFullUid(std::uint16_t componentId,
                                                     std::uint8_t instanceIndex) noexcept {
  return (static_cast<std::uint32_t>(componentId) << 8) | instanceIndex;
}

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULER_DATA_HPP
