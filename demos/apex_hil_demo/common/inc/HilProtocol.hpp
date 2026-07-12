#ifndef APEX_HIL_DEMO_PROTOCOL_HPP
#define APEX_HIL_DEMO_PROTOCOL_HPP
/**
 * @file HilProtocol.hpp
 * @brief UART protocol definitions for HIL communication.
 *
 * Defines the message format between STM32 firmware and POSIX host
 * over a SLIP-framed UART link. Both sides include this header.
 *
 * Wire format: SLIP frame containing [opcode(1) + payload(N) + CRC-16(2)].
 *
 * Uses the same SLIP framing library (system_core_protocols_framing_slip)
 * and CRC-16/XMODEM (utilities_checksums_crc) on both platforms,
 * demonstrating Apex cross-platform code reuse.
 *
 * @note RT-safe: POD types only.
 */

#include "VehicleState.hpp"

#include <stdint.h>

namespace appsim {
namespace hil {

/* ----------------------------- Opcodes ----------------------------- */

/**
 * @brief Message opcodes for the HIL protocol.
 */
enum class HilOpcode : uint8_t {
  /* Host -> STM32 commands */
  CMD_START = 0x01,      ///< Start simulation / enable controller.
  CMD_STOP = 0x02,       ///< Stop simulation / disable controller.
  CMD_RESET = 0x03,      ///< Reset to initial conditions.
  CMD_SET_MODE = 0x04,   ///< Set control mode (payload: ControlMode).
  CMD_SET_TARGET = 0x05, ///< Set target waypoint (payload: Vec3f).

  /* Host -> STM32 data */
  STATE_UPDATE = 0x10, ///< Vehicle state from plant (payload: VehicleState).

  /* STM32 -> Host data */
  CONTROL_CMD = 0x20, ///< Control command (payload: ControlCmd).
  HEARTBEAT = 0x30,   ///< Heartbeat (payload: HeartbeatData).

  /* STM32 -> Host responses */
  ACK = 0xA0, ///< Command acknowledged.
  NACK = 0xA1 ///< Command rejected.
};

/* ----------------------------- Status ----------------------------- */

/**
 * @brief Controller status reported in heartbeat.
 */
enum class ControllerStatus : uint8_t {
  IDLE = 0x00,    ///< Waiting for CMD_START.
  RUNNING = 0x01, ///< Active control loop.
  ERROR = 0x02    ///< Fault condition.
};

} // namespace hil
} // namespace appsim

#endif // APEX_HIL_DEMO_PROTOCOL_HPP
