#ifndef APEX_SYSTEM_CORE_SYSTEM_COMPONENT_HOSTREQUIREMENTS_HPP
#define APEX_SYSTEM_CORE_SYSTEM_COMPONENT_HOSTREQUIREMENTS_HPP
/**
 * @file HostRequirements.hpp
 * @brief Generic host-requirements record any component may publish.
 *
 * A component that needs something from the host -- real-time thread
 * policy, pinned cores, worker threads, a rate the platform must hold --
 * registers one of these under the conventional data name
 * HOST_REQUIREMENTS_NAME. Assessors (the stock SystemMonitor, or any
 * user-built support component) discover every publisher by scanning
 * the registry for that name and shape: no publisher addresses, no
 * publisher headers, no coupling in either direction. Attribution
 * comes from the registry entry's owner fullUid.
 *
 * The record is a fixed-size POD wire shape; alignas keeps field
 * references well-formed wherever an instance lands.
 */

#include <cstdint>

namespace system_core {
namespace system_component {

/// Conventional registry data name for host-requirements records.
inline constexpr const char* HOST_REQUIREMENTS_NAME = "hostRequirements";

/// Maximum requirement rows one record carries.
inline constexpr std::size_t HOST_REQUIREMENTS_ROW_CAP = 8;

#pragma pack(push, 1)
/**
 * @struct HostRequirementRow
 * @brief One thread-group's requested host configuration (16 bytes).
 */
struct HostRequirementRow {
  std::uint8_t groupId{0};       ///< Publisher-scoped group index (e.g., pool id).
  std::uint8_t policy{0};        ///< Requested POSIX policy (0/1/2 = OTHER/FIFO/RR).
  std::int8_t priority{0};       ///< Requested POSIX priority.
  std::uint8_t pad{0};           ///< Alignment.
  std::uint16_t threads{0};      ///< Threads serving this group.
  std::uint16_t pad2{0};         ///< Alignment.
  std::uint64_t affinityMask{0}; ///< Requested CPU set (bit N = CPU N; 0 = any).
};

/**
 * @struct HostRequirements
 * @brief A component's declared host needs (publish via registerData).
 */
struct alignas(8) HostRequirements {
  std::uint16_t rateHz{0};    ///< Rate the host must hold for this component (0 = none).
  std::uint8_t rowCount{0};   ///< Rows populated.
  std::uint8_t reserved{0};   ///< Alignment.
  std::uint16_t taskCount{0}; ///< Scheduled work items behind these needs (informational).
  std::uint16_t pad{0};       ///< Alignment.
  HostRequirementRow rows[HOST_REQUIREMENTS_ROW_CAP]{};
};
#pragma pack(pop)

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SYSTEM_COMPONENT_HOSTREQUIREMENTS_HPP
