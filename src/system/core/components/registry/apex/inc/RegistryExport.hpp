#ifndef APEX_SYSTEM_CORE_REGISTRY_EXPORT_HPP
#define APEX_SYSTEM_CORE_REGISTRY_EXPORT_HPP
/**
 * @file RegistryExport.hpp
 * @brief Packed binary format for registry database export.
 *
 * Format: RDAT (Registry Data)
 * - Header with counts
 * - Component index (fixed-size entries)
 * - Task index (fixed-size entries)
 * - Data index (fixed-size entries)
 * - String table (null-terminated strings)
 *
 * All multi-byte values are little-endian.
 * All structs are packed for direct serialization.
 *
 * @note NOT RT-safe: Export involves file I/O.
 */

#include "src/system/core/infrastructure/system_component/apex/inc/DataCategory.hpp"

#include <cstddef>
#include <cstdint>

#include <array>

namespace system_core {
namespace registry {

/* ----------------------------- Constants ----------------------------- */

/** @brief Magic bytes identifying RDAT format. */
static constexpr std::array<char, 4> RDAT_MAGIC = {'R', 'D', 'A', 'T'};

/** @brief Current format version (v2 adds ComponentType). */
static constexpr std::uint16_t RDAT_VERSION = 2;

/** @brief Default export filename. */
static constexpr const char* RDAT_FILENAME = "registry.rdat";

/* ----------------------------- Header ----------------------------- */

/**
 * @brief File header for RDAT format.
 * @note 16 bytes, packed.
 */
struct __attribute__((packed)) RdatHeader {
  std::array<char, 4> magic;    ///< "RDAT"
  std::uint16_t version;        ///< Format version (currently 1)
  std::uint16_t flags;          ///< Reserved for future use
  std::uint16_t componentCount; ///< Number of component entries
  std::uint16_t taskCount;      ///< Number of task entries
  std::uint16_t dataCount;      ///< Number of data entries
  std::uint16_t reserved;       ///< Padding to 16 bytes
};
static_assert(sizeof(RdatHeader) == 16, "RdatHeader must be 16 bytes");

/* ----------------------------- Component Entry ----------------------------- */

/**
 * @brief Packed component entry for export.
 * @note 24 bytes, packed. V2 uses first byte of reserved for ComponentType.
 */
struct __attribute__((packed)) RdatComponentEntry {
  std::uint32_t fullUid;      ///< (componentId << 8) | instanceIndex
  std::uint32_t nameOffset;   ///< Offset into string table
  std::uint16_t taskStart;    ///< Index of first task in task table
  std::uint16_t taskCount;    ///< Number of tasks for this component
  std::uint16_t dataStart;    ///< Index of first data entry in data table
  std::uint16_t dataCount;    ///< Number of data entries for this component
  std::uint8_t componentType; ///< ComponentType enum value (v2+)
  std::uint8_t reserved1;     ///< Reserved for future use
  std::uint16_t reserved2;    ///< Reserved for future use
  std::uint32_t reserved3;    ///< Reserved for future use
};
static_assert(sizeof(RdatComponentEntry) == 24, "RdatComponentEntry must be 24 bytes");

/* ----------------------------- Task Entry ----------------------------- */

/**
 * @brief Packed task entry for export.
 * @note 16 bytes, packed.
 */
struct __attribute__((packed)) RdatTaskEntry {
  std::uint32_t fullUid;    ///< Owning component's fullUid
  std::uint8_t taskUid;     ///< Task UID within component
  std::uint8_t reserved1;   ///< Alignment padding
  std::uint16_t reserved2;  ///< Alignment padding
  std::uint32_t nameOffset; ///< Offset into string table
  std::uint32_t reserved3;  ///< Reserved for future use
};
static_assert(sizeof(RdatTaskEntry) == 16, "RdatTaskEntry must be 16 bytes");

/* ----------------------------- Data Entry ----------------------------- */

/**
 * @brief Packed data entry for export.
 * @note 24 bytes, packed.
 */
struct __attribute__((packed)) RdatDataEntry {
  std::uint32_t fullUid;    ///< Owning component's fullUid
  std::uint8_t category;    ///< DataCategory enum value
  std::uint8_t reserved1;   ///< Alignment padding
  std::uint16_t reserved2;  ///< Alignment padding
  std::uint32_t nameOffset; ///< Offset into string table
  std::uint32_t size;       ///< Size of data in bytes
  std::uint64_t reserved3;  ///< Reserved for future use (snapshot offset)
};
static_assert(sizeof(RdatDataEntry) == 24, "RdatDataEntry must be 24 bytes");

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief Validate RDAT header magic and version.
 * @param header Header to validate.
 * @return True if header is valid RDAT format.
 */
[[nodiscard]] inline bool isValidRdatHeader(const RdatHeader& header) noexcept {
  return header.magic == RDAT_MAGIC && header.version == RDAT_VERSION;
}

/**
 * @brief Calculate total file size for given counts.
 * @param componentCount Number of components.
 * @param taskCount Number of tasks.
 * @param dataCount Number of data entries.
 * @param stringTableSize Size of string table in bytes.
 * @return Total file size in bytes.
 */
[[nodiscard]] inline std::size_t calculateRdatSize(std::uint16_t componentCount,
                                                   std::uint16_t taskCount, std::uint16_t dataCount,
                                                   std::size_t stringTableSize) noexcept {
  return sizeof(RdatHeader) + (componentCount * sizeof(RdatComponentEntry)) +
         (taskCount * sizeof(RdatTaskEntry)) + (dataCount * sizeof(RdatDataEntry)) +
         stringTableSize;
}

/**
 * @brief Get offset to component table in RDAT file.
 * @return Byte offset from file start.
 */
[[nodiscard]] inline constexpr std::size_t rdatComponentTableOffset() noexcept {
  return sizeof(RdatHeader);
}

/**
 * @brief Get offset to task table in RDAT file.
 * @param componentCount Number of components.
 * @return Byte offset from file start.
 */
[[nodiscard]] inline std::size_t rdatTaskTableOffset(std::uint16_t componentCount) noexcept {
  return sizeof(RdatHeader) + (componentCount * sizeof(RdatComponentEntry));
}

/**
 * @brief Get offset to data table in RDAT file.
 * @param componentCount Number of components.
 * @param taskCount Number of tasks.
 * @return Byte offset from file start.
 */
[[nodiscard]] inline std::size_t rdatDataTableOffset(std::uint16_t componentCount,
                                                     std::uint16_t taskCount) noexcept {
  return sizeof(RdatHeader) + (componentCount * sizeof(RdatComponentEntry)) +
         (taskCount * sizeof(RdatTaskEntry));
}

/**
 * @brief Get offset to string table in RDAT file.
 * @param componentCount Number of components.
 * @param taskCount Number of tasks.
 * @param dataCount Number of data entries.
 * @return Byte offset from file start.
 */
[[nodiscard]] inline std::size_t rdatStringTableOffset(std::uint16_t componentCount,
                                                       std::uint16_t taskCount,
                                                       std::uint16_t dataCount) noexcept {
  return sizeof(RdatHeader) + (componentCount * sizeof(RdatComponentEntry)) +
         (taskCount * sizeof(RdatTaskEntry)) + (dataCount * sizeof(RdatDataEntry));
}

} // namespace registry
} // namespace system_core

#endif // APEX_SYSTEM_CORE_REGISTRY_EXPORT_HPP
