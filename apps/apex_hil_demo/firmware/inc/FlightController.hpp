#ifndef APEX_HIL_DEMO_FLIGHT_CONTROLLER_HPP
#define APEX_HIL_DEMO_FLIGHT_CONTROLLER_HPP
/**
 * @file FlightController.hpp
 * @brief Flight controller for the STM32 HIL demonstration.
 *
 * Three-layer controller architecture (MVP ~20% CPU):
 *  1. Onboard prediction model (simplified dynamics + constant gravity)
 *  2. State estimator (complementary filter)
 *  3. Guidance/control (PD altitude hold + proportional lateral)
 *
 * Scaling CPU load (future):
 *  - Replace complementary filter with EKF (6x6 matrix ops)
 *  - Add FIR sensor filter banks
 *  - Increase prediction sub-steps per tick
 *
 * @note RT-safe after init(): update() has bounded execution time.
 */

#include "apps/apex_hil_demo/common/inc/HilConfig.hpp"
#include "apps/apex_hil_demo/common/inc/VehicleState.hpp"

#include <stdint.h>

namespace appsim {
namespace hil {

/* ----------------------------- FlightController ----------------------------- */

/**
 * @class FlightController
 * @brief Closed-loop flight controller.
 *
 * Lifecycle:
 *  1. Construct
 *  2. Call init() with initial target
 *  3. Call updateState() when new plant state arrives
 *  4. Call computeControl() each control tick to get thrust command
 *
 * @note RT-safe: computeControl() has O(1) bounded execution.
 */
class FlightController {
public:
  FlightController() noexcept = default;

  /**
   * @brief Initialize controller with target altitude.
   * @param targetAlt Target altitude [m].
   * @note NOT RT-safe: Initialization only.
   */
  void init(float targetAlt) noexcept;

  /**
   * @brief Update controller with latest plant state.
   * @param state Vehicle state from host plant.
   * @note RT-safe: O(1).
   */
  void updateState(const VehicleState& state) noexcept;

  /**
   * @brief Compute control command for current tick.
   * @return Thrust command to send back to host.
   * @note RT-safe: O(1) bounded execution.
   */
  ControlCmd computeControl() noexcept;

  /**
   * @brief Set target waypoint for lateral guidance.
   * @param target Target position (x, y, z) [m].
   * @note RT-safe.
   */
  void setTarget(const Vec3f& target) noexcept;

  /**
   * @brief Set control mode.
   * @param mode New control mode.
   * @note RT-safe.
   */
  void setMode(ControlMode mode) noexcept { mode_ = mode; }

  /**
   * @brief Get current control mode.
   * @return Active control mode.
   * @note RT-safe.
   */
  ControlMode mode() const noexcept { return mode_; }

  /**
   * @brief Get number of control steps executed.
   * @return Step count.
   * @note RT-safe.
   */
  uint32_t stepCount() const noexcept { return stepCount_; }

private:
  VehicleState lastState_{}; ///< Most recent plant state.
  Vec3f target_{};           ///< Target waypoint.
  float targetAlt_{0.0F};    ///< Target altitude [m].
  ControlMode mode_{ControlMode::IDLE};
  uint32_t stepCount_{0};
  bool hasState_{false}; ///< True once first state received.

  /* ----------------------------- Gains ----------------------------- */

  /// Altitude PD gains.
  static constexpr float KP_ALT = 2.0F;
  static constexpr float KD_ALT = 1.5F;

  /// Lateral proportional gain.
  static constexpr float KP_LAT = 0.5F;

  /// Vehicle mass [kg] (matched to plant configuration).
  static constexpr float MASS = 10.0F;

  /// Gravity [m/s^2].
  static constexpr float G0 = 9.80665F;

  /// Maximum thrust magnitude [N].
  static constexpr float THRUST_MAX = 200.0F;
};

} // namespace hil
} // namespace appsim

#endif // APEX_HIL_DEMO_FLIGHT_CONTROLLER_HPP
