#ifndef APEX_HORIZON_DEMO_GROUND_VEHICLE_COMMAND_HPP
#define APEX_HORIZON_DEMO_GROUND_VEHICLE_COMMAND_HPP

/**
 * @file GroundVehicleCommand.hpp
 * @brief ROVR/2 command surface: opcodes, payloads, bounds, and the
 * frame bytes that report command results and drive state.
 *
 * Every command is accepted whole or rejected whole: a payload with
 * any field out of range changes nothing and answers
 * INVALID_ARGUMENT. The result of the last command and its opcode
 * are stamped into the frame so a consumer sees the refusal, and what
 * was refused, on the stream rather than on a panel echo.
 *
 * The frame bytes are the reserved tail of GroundVehicleTelemetry
 * (offset 232); ROVR/2 stays version 2 because every byte is zero
 * until its feature acts.
 */

#include <cstddef>
#include <cstdint>

namespace appsim {
namespace ground_vehicle {

/* ----------------------------- Opcodes ----------------------------- */

/// Component-specific opcodes (0x0100+), all addressed to the vehicle.
enum class RoverOpcode : std::uint16_t {
  HALT = 0x0100,           ///< No payload. Plant-level stop; heading frozen.
  RESUME = 0x0101,         ///< No payload. Clears HALT and any throttle override.
  SET_THROTTLE = 0x0102,   ///< u8 percent 0..100 (shapes the built-in trajectory only).
  SET_MODE = 0x0103,       ///< RoverCmdSetMode: drive mode 0 HOLD, 1 TRAJECTORY, 2 WAYPOINT.
  SET_TARGET_REL = 0x0104, ///< RoverCmdSetTarget: (north, east) metres from the current position.
  SET_TARGET_ABS = 0x0105, ///< RoverCmdSetTarget: (north, east) metres from the grid anchor.
  SET_LED = 0x0106,        ///< RoverCmdSetLed: lamp 1..2, colour code, rate code.
  SET_SEQ_STATE = 0x0107,  ///< RoverCmdSeqState: sequences bracket themselves (display truth).
  /// Sequence-owned variants of SET_MODE / SET_TARGET_REL / SET_TARGET_ABS
  /// (same payloads). Sequences drive through these; the plain opcodes
  /// answer NACK_BUSY while a sequence is running, so a panel cannot
  /// steal the drive from a tour without halting it first.
  SET_MODE_SEQ = 0x0113,
  SET_TARGET_REL_SEQ = 0x0114,
  SET_TARGET_ABS_SEQ = 0x0115,
};

/// Last rover-range opcode (for result stamping).
inline constexpr std::uint16_t kRoverOpcodeLast = 0x0115;

/// Component-specific command result: the drive is owned by a running
/// sequence (extends the framework's CommandResult codes).
inline constexpr std::uint8_t kCommandResultBusy = 0x20;

/* ----------------------------- Bounds ----------------------------- */

inline constexpr std::uint8_t kDriveModeMax = 2; ///< 0 HOLD, 1 TRAJECTORY, 2 WAYPOINT.
inline constexpr std::uint8_t kLampCount = 2;    ///< Lamps are 1-based on the wire.
inline constexpr std::uint8_t kLedColourMax =
    5; ///< 0 OFF, 1 RED, 2 GREEN, 3 BLUE, 4 YELLOW, 5 WHITE.
inline constexpr std::uint8_t kLedRateMax =
    5; ///< 0 OFF, 1 0.5 Hz, 2 1 Hz, 3 2 Hz, 4 5 Hz, 5 10 Hz.
inline constexpr float kTargetAbsMaxM = 2000.0F; ///< |north|, |east| bound for any target.

/// Strobe period in 100 Hz frames per rate code (index = code); 0 = steady.
inline constexpr std::uint16_t kLedRateFrames[kLedRateMax + 1] = {0, 200, 100, 50, 20, 10};

/* ----------------------------- Payloads ----------------------------- */

struct RoverCmdSetMode {
  std::uint8_t mode; ///< 0..kDriveModeMax.
};
static_assert(sizeof(RoverCmdSetMode) == 1);

/// Little-endian pair of single-precision metres.
struct RoverCmdSetTarget {
  float a_m; ///< north (REL: displacement; ABS: from anchor).
  float b_m; ///< east.
};
static_assert(sizeof(RoverCmdSetTarget) == 8);

struct RoverCmdSetLed {
  std::uint8_t lamp;   ///< 1..kLampCount.
  std::uint8_t colour; ///< 0..kLedColourMax.
  std::uint8_t rate;   ///< 0..kLedRateMax.
};
static_assert(sizeof(RoverCmdSetLed) == 3);

struct RoverCmdSeqState {
  std::uint8_t state;          ///< 0 idle; 1..31 running RTS id; 0x10|reason recovery halt.
  std::uint8_t waypoint_total; ///< Legs in the running sequence (0 when idle).
};
static_assert(sizeof(RoverCmdSeqState) == 2);

/* ----------------------------- Frame bytes (telemetry reserved1, base 232)
 * ----------------------------- */

/// Offsets within GroundVehicleTelemetry::reserved1.
enum FrameByte : std::size_t {
  FB_BOARD_LINK = 0,      ///< 0 NEVER, 1 UP, 2 LOST (hardware form).
  FB_CONTROLLER_MODE = 1, ///< 0 HOLD, 1 TRAJECTORY, 2 WAYPOINT, 3 HALTED.
  FB_SEQ_STATE = 2,
  FB_ACTIVE_WAYPOINT = 3,
  FB_WAYPOINT_TOTAL = 4,
  FB_LED_BITS = 5, ///< bit0 lamp1 on, bit1 lamp2 on (live).
  FB_LED1_COLOUR = 6,
  FB_LED1_RATE = 7,
  FB_LED2_COLOUR = 8,
  FB_LED2_RATE = 9,
  FB_LAST_CMD_RESULT = 10,
  FB_LAST_CMD_OPCODE_LO = 11,
  FB_LAST_CMD_OPCODE_HI = 12,
  FB_BOARD_LOAD_PCT = 13,
  FB_BOARD_TICK_LO = 14,
  FB_BOARD_TICK_HI = 15,
  FB_MAST_PAN_DEG = 16,
  FB_MAST_EXT_PCT = 17,
};

/// last_cmd_result codes on the wire.
enum class CmdResultCode : std::uint8_t {
  NONE = 0,
  ACK = 1,
  NACK_INVALID_ARGUMENT = 2, ///< Payload short or a field out of range.
  NACK_EXEC_FAILED = 3,      ///< The mode refuses (e.g. a target while HALTED).
  NACK_BUSY = 4,             ///< A sequence owns the target.
  NACK_LINK_DOWN = 5,        ///< Hardware form: the board is unreachable.
};

/// Frame controller_mode value while a plant-level HALT is in effect.
inline constexpr std::uint8_t kFrameModeHalted = 3;

/// seq_state values: 0 idle, 1..31 a running sequence, 0x10|reason a
/// halt. A plant-level HALT stamps MANUAL_HALT unless a recovery
/// reason is already set; RESUME clears any halt reason to idle.
inline constexpr std::uint8_t kSeqStateHaltBit = 0x10;
inline constexpr std::uint8_t kSeqStateManualHalt = 0x15;

} // namespace ground_vehicle
} // namespace appsim

#endif // APEX_HORIZON_DEMO_GROUND_VEHICLE_COMMAND_HPP
