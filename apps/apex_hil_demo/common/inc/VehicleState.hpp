#ifndef APEX_HIL_DEMO_VEHICLE_STATE_HPP
#define APEX_HIL_DEMO_VEHICLE_STATE_HPP
/**
 * @file VehicleState.hpp
 * @brief Shared vehicle state and control types for the HIL demonstration.
 *
 * Compiled on both STM32 (Cortex-M4) and POSIX hosts. Uses float for
 * wire format (STM32 FPU compatibility); plant uses double internally.
 *
 * @note RT-safe: All types are POD with no heap allocation.
 */

#include <stdint.h>

namespace appsim {
namespace hil {

/* ----------------------------- Vec3f ----------------------------- */

/**
 * @struct Vec3f
 * @brief 3D vector using single-precision floats.
 *
 * Used for wire format between host and STM32. The plant model
 * uses double internally and converts when packing messages.
 *
 * @note RT-safe: POD type.
 */
struct Vec3f {
  float x{0.0F};
  float y{0.0F};
  float z{0.0F};
};

/* ----------------------------- ControlMode ----------------------------- */

/**
 * @brief Control mode for the flight controller.
 */
enum class ControlMode : uint8_t {
  IDLE = 0x00,     ///< No thrust output.
  HOLD_ALT = 0x01, ///< Maintain target altitude.
  WAYPOINT = 0x02  ///< Navigate toward target waypoint.
};

/* ----------------------------- VehicleState ----------------------------- */

/**
 * @struct VehicleState
 * @brief Complete vehicle state vector for wire transfer.
 *
 * Sent from host (plant) to STM32 (controller) each control tick.
 * 56 bytes total.
 *
 * @note RT-safe: POD type.
 */
struct VehicleState {
  Vec3f pos;          ///< Position [m] (NED or ECI frame).
  Vec3f vel;          ///< Velocity [m/s].
  Vec3f accel;        ///< Acceleration [m/s^2] (from last step).
  float altitude;     ///< Altitude above reference [m].
  float simTime;      ///< Simulation time [s].
  uint32_t stepCount; ///< Plant step counter.
  uint16_t seqNum;    ///< Sender sequence number (host increments per send).
  uint16_t ackSeq;    ///< Last received ControlCmd.seqNum from this controller.
};

/* ----------------------------- ControlCmd ----------------------------- */

/**
 * @struct ControlCmd
 * @brief Control command from STM32 controller to host plant.
 *
 * Sent from STM32 (controller) to host (plant) each control tick.
 * 20 bytes total.
 *
 * @note RT-safe: POD type.
 */
struct ControlCmd {
  Vec3f thrust;     ///< Thrust vector [N] (body or NED frame).
  ControlMode mode; ///< Active control mode.
  uint8_t reserved; ///< Padding for alignment.
  uint16_t seqNum;  ///< Sender sequence number (controller increments per send).
  uint16_t ackSeq;  ///< Last received VehicleState.seqNum from host.
};

/* ----------------------------- HeartbeatData ----------------------------- */

/**
 * @struct HeartbeatData
 * @brief Periodic heartbeat from STM32 firmware.
 *
 * Sent at 1 Hz for monitoring. 12 bytes total.
 *
 * @note RT-safe: POD type.
 */
struct HeartbeatData {
  uint32_t cycleCount; ///< Executive cycle count.
  uint32_t stepCount;  ///< Controller step count.
  uint32_t overheadUs; ///< Last tick overhead [microseconds].
};

} // namespace hil
} // namespace appsim

#endif // APEX_HIL_DEMO_VEHICLE_STATE_HPP
