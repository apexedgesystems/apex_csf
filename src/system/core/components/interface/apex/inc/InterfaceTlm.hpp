#ifndef APEX_SYSTEM_CORE_INTERFACE_INTERFACE_TLM_HPP
#define APEX_SYSTEM_CORE_INTERFACE_INTERFACE_TLM_HPP
/**
 * @file InterfaceTlm.hpp
 * @brief Telemetry wire format for ApexInterface stats queries.
 *
 * Packed POD struct returned by GET_STATS (opcode 0x0100) when sent to
 * the Interface component (fullUid=0x000400). Provides communication
 * health: packet rates, error rates, queue overflow counts.
 *
 * Wire format: little-endian, packed, 56 bytes total.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstdint>

namespace system_core {
namespace interface {

/* ----------------------------- InterfaceTlmOpcode ----------------------------- */

/// Telemetry opcodes for ApexInterface (component-specific range 0x0100+).
enum class InterfaceTlmOpcode : std::uint16_t {
  GET_STATS = 0x0100 ///< Request interface statistics snapshot.
};

/* ----------------------------- InterfaceHealthTlm ----------------------------- */

/**
 * @struct InterfaceHealthTlm
 * @brief Interface communication statistics telemetry payload.
 *
 * Returned as response payload to GET_STATS (opcode 0x0100) when
 * addressed to the Interface component. All counters are cumulative
 * since startup.
 *
 * Wire format (56 bytes, little-endian):
 *   Offset  Size  Field
 *   --- External Interface ---
 *   0       4     packetsReceived        - Valid APROTO packets from socket
 *   4       4     packetsInvalid         - Parse failures
 *   8       4     systemCommands         - System opcodes handled locally
 *   12      4     routedCommands         - Commands routed to components
 *   16      4     responsesQueued        - ACK/NAK responses queued for TX
 *   20      4     telemetryFrames        - Telemetry frames transmitted
 *   24      4     framingErrors          - SLIP/COBS decode errors
 *   --- Internal Bus ---
 *   28      4     internalCommandsSent   - Model-to-model commands posted
 *   32      4     internalTelemetrySent  - Internal telemetry posted
 *   36      4     internalCommandsFailed - Queue full or component not found
 *   40      4     multicastCommandsSent  - Multicast commands posted
 *   44      4     broadcastCommandsSent  - Broadcast commands posted
 *   --- Queue Health ---
 *   48      4     cmdQueueOverflows      - Command inbox full events
 *   52      4     tlmQueueOverflows      - Telemetry outbox full events
 *
 * Total: 56 bytes.
 */
struct __attribute__((packed)) InterfaceHealthTlm {
  /* ----------------------------- External Interface ----------------------------- */

  std::uint32_t packetsReceived{0}; ///< Valid APROTO packets received from socket.
  std::uint32_t packetsInvalid{0};  ///< Invalid packets (parse error, too small).
  std::uint32_t systemCommands{0};  ///< System opcodes handled locally.
  std::uint32_t routedCommands{0};  ///< Commands routed to components.
  std::uint32_t responsesQueued{0}; ///< ACK/NAK responses queued for TX.
  std::uint32_t telemetryFrames{0}; ///< Telemetry frames transmitted to socket.
  std::uint32_t framingErrors{0};   ///< SLIP/COBS framing decode errors.

  /* ----------------------------- Internal Bus ----------------------------- */

  std::uint32_t internalCommandsSent{0};   ///< Internal commands posted (model-to-model).
  std::uint32_t internalTelemetrySent{0};  ///< Internal telemetry posted (to external).
  std::uint32_t internalCommandsFailed{0}; ///< Internal commands failed (queue full/not found).
  std::uint32_t multicastCommandsSent{0};  ///< Multicast commands posted.
  std::uint32_t broadcastCommandsSent{0};  ///< Broadcast commands posted.

  /* ----------------------------- Queue Health ----------------------------- */

  std::uint32_t cmdQueueOverflows{0}; ///< Command inbox full events.
  std::uint32_t tlmQueueOverflows{0}; ///< Telemetry outbox full events.
};

static_assert(sizeof(InterfaceHealthTlm) == 56, "InterfaceHealthTlm size mismatch");

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_INTERFACE_TLM_HPP
