#ifndef APEX_SYSTEM_COMPONENT_TLM_HPP
#define APEX_SYSTEM_COMPONENT_TLM_HPP
/**
 * @file SystemComponentTlm.hpp
 * @brief Telemetry response structs for base component commands.
 *
 * These structs define the wire format for telemetry responses to the
 * common component opcodes (0x0080-0x00FF) that all components inherit
 * from SystemComponentBase.
 *
 * All structs are POD types suitable for direct memcpy serialization.
 * @note RT-safe: All types are compile-time sized with no allocation.
 */

#include <cstdint>

namespace system_core {
namespace system_component {

/* ----------------------------- ComponentCommandCountTlm ----------------------------- */

/**
 * @struct ComponentCommandCountTlm
 * @brief Response for GET_COMMAND_COUNT (opcode 0x0080).
 *
 * Returns command statistics for a component.
 *
 * Wire format (16 bytes, little-endian):
 *   Offset  Size  Field
 *   0       8     commandCount     - Total commands received
 *   8       8     rejectedCount    - Commands that returned non-zero status
 */
struct ComponentCommandCountTlm {
  std::uint64_t commandCount{0};  ///< Total commands received since startup.
  std::uint64_t rejectedCount{0}; ///< Commands that returned non-zero status.
} __attribute__((packed));

static_assert(sizeof(ComponentCommandCountTlm) == 16, "ComponentCommandCountTlm must be 16 bytes");

/* ----------------------------- ComponentStatusInfoTlm ----------------------------- */

/**
 * @struct ComponentStatusInfoTlm
 * @brief Response for GET_STATUS_INFO (opcode 0x0081).
 *
 * Returns lifecycle state for a component.
 *
 * Wire format (4 bytes):
 *   Offset  Size  Field
 *   0       1     status        - Last operation status code
 *   1       1     initialized   - 1 if init() succeeded, 0 otherwise
 *   2       1     configured    - 1 if configured, 0 otherwise
 *   3       1     registered    - 1 if registered with executive, 0 otherwise
 */
struct ComponentStatusInfoTlm {
  std::uint8_t status{0};      ///< Last operation status code.
  std::uint8_t initialized{0}; ///< 1 if init() succeeded.
  std::uint8_t configured{0};  ///< 1 if configured (params loaded or set ready).
  std::uint8_t registered{0};  ///< 1 if registered with executive.
} __attribute__((packed));

static_assert(sizeof(ComponentStatusInfoTlm) == 4, "ComponentStatusInfoTlm must be 4 bytes");

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_TLM_HPP
