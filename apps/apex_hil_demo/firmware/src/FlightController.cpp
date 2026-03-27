/**
 * @file FlightController.cpp
 * @brief Flight controller implementation.
 *
 * PD altitude hold with proportional lateral guidance.
 */

#include "apps/apex_hil_demo/firmware/inc/FlightController.hpp"

namespace appsim {
namespace hil {

/* ----------------------------- FlightController Methods ----------------------------- */

void FlightController::init(float targetAlt) noexcept {
  targetAlt_ = targetAlt;
  stepCount_ = 0;
  hasState_ = false;
  mode_ = ControlMode::IDLE;
}

void FlightController::updateState(const VehicleState& state) noexcept {
  lastState_ = state;
  hasState_ = true;
}

ControlCmd FlightController::computeControl() noexcept {
  ControlCmd cmd{};
  cmd.mode = mode_;

  if (!hasState_ || mode_ == ControlMode::IDLE) {
    ++stepCount_;
    return cmd;
  }

  // Weight compensation (NED: upward force = negative z)
  const float WEIGHT = MASS * G0;

  // Altitude PD: error = targetAlt - current altitude
  // VehicleState.vel.z is NED (+z = down), so positive Vz means falling.
  // We want to oppose downward velocity.
  const float ALT_ERR = targetAlt_ - lastState_.altitude;
  float thrustZ = KP_ALT * ALT_ERR - KD_ALT * lastState_.vel.z + WEIGHT;

  // Clamp
  if (thrustZ > THRUST_MAX) {
    thrustZ = THRUST_MAX;
  }
  if (thrustZ < 0.0F) {
    thrustZ = 0.0F;
  }

  // NED frame: upward thrust is negative z
  cmd.thrust.z = -thrustZ;

  // Lateral guidance (WAYPOINT mode only)
  if (mode_ == ControlMode::WAYPOINT) {
    cmd.thrust.x = KP_LAT * (target_.x - lastState_.pos.x);
    cmd.thrust.y = KP_LAT * (target_.y - lastState_.pos.y);
  }

  ++stepCount_;
  return cmd;
}

void FlightController::setTarget(const Vec3f& target) noexcept { target_ = target; }

} // namespace hil
} // namespace appsim
