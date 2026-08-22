#ifndef APEX_HORIZON_DEMO_AIRCRAFT_COMMAND_HPP
#define APEX_HORIZON_DEMO_AIRCRAFT_COMMAND_HPP
/**
 * @file AircraftCommand.hpp
 * @brief Aircraft component command opcodes + payload structs.
 *
 * Aircraft opcodes are dispatched by `Aircraft::handleCommand(opcode,
 * payload, response)` per the apex SwModelBase pattern. They reach the
 * component via two transports:
 *
 *   1. **TCP/APROTO** through ApexInterface — external clients (e.g.,
 *      `demos/apex_horizon_demo/scripts/checkout.py`) connect to the
 *      interface's TCP port and send APROTO frames targeting the
 *      Aircraft's fullUid (0x00E000) with the opcode below.
 *
 *   2. **SHM/APROTO** through ShmRingBridge — the consumer writes APROTO frames
 *      into the bridge's Ring B (consumer → apex). ShmRingBridge decodes
 *      the frame and calls `internalBus->postInternalCommand(...)`,
 *      which routes through the same per-component cmd queue as TCP.
 *      See `horizon/docs/BRIDGE_FORMAT.md` v2 addendum.
 *
 * Both paths land at the same `handleCommand` callback. Aircraft
 * doesn't know or care which transport carried the command.
 *
 * Opcode space:
 *   - 0x0000-0x00FF: APROTO system opcodes (handled by ApexInterface itself)
 *   - 0x0080-0x008F: SwModelBase common opcodes (delegated to base class)
 *   - 0x0100+:       Component-specific (defined here)
 */

#include <cstdint>

namespace appsim {
namespace aircraft {

/* ----------------------------- Opcodes ----------------------------- */

/// Aircraft command opcodes. Component-specific range is 0x0100+.
enum class AircraftOpcode : std::uint16_t {
  /// Toggle Dryden turbulence on/off.
  /// Payload: AircraftCmdSetEnable (1 byte: 0=off, 1=on, others=invalid).
  /// Response: empty (status code = SUCCESS / INVALID_PAYLOAD).
  ///
  /// Disabling zeros the gust output to the aircraft's apparent wind
  /// each tick; the Dryden filter state continues to advance so
  /// re-enabling resumes without a transient.
  SET_TURBULENCE_ENABLE = 0x0100,

  /// Toggle GustAlleviation feedforward on/off.
  /// Payload: AircraftCmdSetEnable (1 byte: 0=off, 1=on).
  /// Response: empty.
  ///
  /// Aircraft owns the enable flag; AircraftController polls
  /// aircraft_->isGustAlleviationEnabled() and zeroes its feedforward
  /// elevator contribution when disabled. Same ownership pattern as
  /// SET_TURBULENCE_ENABLE — Aircraft is the single source of
  /// disturbance / response command state.
  SET_GUST_ALLEVIATION_ENABLE = 0x0101,

  /// Read current command-state flags + active overrides.
  /// Payload: empty.
  /// Response: AircraftCommandSnapshot (sizeof = 16 bytes; see below).
  GET_COMMAND_STATE = 0x0102,
};

/* ----------------------------- Payload structs ----------------------------- */

/// 1-byte enable/disable payload (used by SET_TURBULENCE_ENABLE,
/// SET_GUST_ALLEVIATION_ENABLE, etc.)
struct AircraftCmdSetEnable {
  std::uint8_t enabled; ///< 0 = disable, 1 = enable.
};
static_assert(sizeof(AircraftCmdSetEnable) == 1, "AircraftCmdSetEnable must be exactly 1 byte");

/// Snapshot of the aircraft's current command-state flags. Returned
/// by GET_COMMAND_STATE so external clients (HUD, checkout script, ops
/// dashboard) can read the live state without a separate INSPECT call.
struct AircraftCommandSnapshot {
  std::uint8_t turbulence_enabled;       ///< 0/1 — current Dryden gating
  std::uint8_t gust_alleviation_enabled; ///< 0/1 — controller δe_gust gate
  std::uint8_t reserved[6];              ///< Reserved for future flags
  std::uint64_t cmd_count;               ///< Number of commands handled since boot
};
static_assert(sizeof(AircraftCommandSnapshot) == 16,
              "AircraftCommandSnapshot must be exactly 16 bytes");

} // namespace aircraft
} // namespace appsim

#endif // APEX_HORIZON_DEMO_AIRCRAFT_COMMAND_HPP
