#ifndef APEX_SYSTEM_CORE_SCHEDULER_EXPORT_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_EXPORT_HPP
/**
 * @file SchedulerExport.hpp
 * @brief Packed binary format for scheduler database export.
 *
 * Format: SDAT (Scheduler Data)
 * - Header with counts and configuration
 * - Task entries (fixed-size, scheduling parameters)
 * - Per-tick schedule (variable-length, task indices per tick)
 * - String table (null-terminated task names)
 *
 * All multi-byte values are little-endian.
 * All structs are packed for direct serialization.
 *
 * @note NOT RT-safe: Export involves file I/O.
 */

#include <cstddef>
#include <cstdint>

#include <array>

namespace system_core {
namespace scheduler {

/* ----------------------------- Constants ----------------------------- */

/** @brief Magic bytes identifying SDAT format. */
static constexpr std::array<char, 4> SDAT_MAGIC = {'S', 'D', 'A', 'T'};

/** @brief Current format version. */
static constexpr std::uint16_t SDAT_VERSION = 1;

/** @brief Default export filename. */
static constexpr const char* SDAT_FILENAME = "sched.rdat";

/* ----------------------------- Header ----------------------------- */

/**
 * @brief File header for SDAT format.
 * @note 16 bytes, packed.
 */
struct __attribute__((packed)) SdatHeader {
  std::array<char, 4> magic;     ///< "SDAT"
  std::uint16_t version;         ///< Format version
  std::uint16_t flags;           ///< Reserved for future use
  std::uint16_t fundamentalFreq; ///< Fundamental frequency (ticks/sec)
  std::uint16_t taskCount;       ///< Number of task entries
  std::uint16_t tickCount;       ///< Number of ticks in schedule table (LCM period)
  std::uint16_t reserved;        ///< Padding to 16 bytes
};
static_assert(sizeof(SdatHeader) == 16, "SdatHeader must be 16 bytes");

/* ----------------------------- Task Entry ----------------------------- */

/**
 * @brief Packed task entry for export.
 *
 * Contains all scheduling parameters for a single task.
 * Matches the runtime TaskEntry configuration.
 *
 * @note 24 bytes, packed.
 */
struct __attribute__((packed)) SdatTaskEntry {
  std::uint32_t fullUid;      ///< Owner component's fullUid
  std::uint8_t taskUid;       ///< Task UID within component
  std::uint8_t poolIndex;     ///< Thread pool index
  std::uint16_t freqN;        ///< Frequency numerator (Hz)
  std::uint16_t freqD;        ///< Frequency denominator (>=1)
  std::uint16_t offset;       ///< Tick offset within period
  std::int8_t priority;       ///< Task priority [-128, 127]
  std::uint8_t sequenceGroup; ///< Sequence group (0xFF = none)
  std::uint8_t sequencePhase; ///< Phase within sequence group
  std::uint8_t reserved1;     ///< Padding
  std::uint32_t nameOffset;   ///< Offset into string table
  std::uint32_t reserved2;    ///< Reserved for future use
};
static_assert(sizeof(SdatTaskEntry) == 24, "SdatTaskEntry must be 24 bytes");

/* ----------------------------- Tick Entry ----------------------------- */

/**
 * @brief Per-tick schedule entry header.
 *
 * Each tick has a header followed by taskCount task indices (uint16_t each).
 * Format: [SdatTickEntry][index0][index1]...[indexN-1]
 *
 * @note 4 bytes, packed.
 */
struct __attribute__((packed)) SdatTickEntry {
  std::uint16_t tick;      ///< Tick number (0 to tickCount-1)
  std::uint16_t taskCount; ///< Number of tasks scheduled on this tick
};
static_assert(sizeof(SdatTickEntry) == 4, "SdatTickEntry must be 4 bytes");

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief Validate SDAT header magic and version.
 * @param header Header to validate.
 * @return True if header is valid SDAT format.
 */
[[nodiscard]] inline bool isValidSdatHeader(const SdatHeader& header) noexcept {
  return header.magic == SDAT_MAGIC && header.version == SDAT_VERSION;
}

/**
 * @brief Get offset to task table in SDAT file.
 * @return Byte offset from file start.
 */
[[nodiscard]] inline constexpr std::size_t sdatTaskTableOffset() noexcept {
  return sizeof(SdatHeader);
}

/**
 * @brief Get offset to tick schedule in SDAT file.
 * @param taskCount Number of tasks.
 * @return Byte offset from file start.
 */
[[nodiscard]] inline std::size_t sdatTickScheduleOffset(std::uint16_t taskCount) noexcept {
  return sizeof(SdatHeader) + (taskCount * sizeof(SdatTaskEntry));
}

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_EXPORT_HPP
