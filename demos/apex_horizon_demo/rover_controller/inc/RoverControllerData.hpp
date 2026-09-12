#ifndef APEX_HORIZON_DEMO_ROVER_CONTROLLER_DATA_HPP
#define APEX_HORIZON_DEMO_ROVER_CONTROLLER_DATA_HPP

/**
 * @file RoverControllerData.hpp
 * @brief Tunables, state, and output for the RoverController.
 *
 * The controller works in a local grid: metres north (y) and east (x)
 * of the anchor (the terrain patch's anchor by default). Targets are
 * grid points; the vehicle's geodetic pose is projected onto the grid
 * each tick with the same local-flat approximation the plant
 * integrates with, so the two never disagree about where the rover is.
 */

#include <cstdint>

namespace appsim {
namespace rover_controller {

/* ----------------------------- Modes ----------------------------- */

/// Drive modes. Numeric values are the wire codes (frame byte
/// controller_mode and the SET_MODE payload).
enum class DriveMode : std::uint8_t {
  HOLD = 0,       ///< Zero throttle, zero steer: the rover sits.
  TRAJECTORY = 1, ///< The built-in circle: constant throttle + turn.
  WAYPOINT = 2,   ///< Steer to the current target, stop on arrival.
};

/* ----------------------------- RoverControllerTunables ----------------------------- */

struct RoverControllerTunables {
  /// Boot mode (DriveMode code).
  std::uint8_t boot_mode{0};
  std::uint8_t reserved0[7]{};

  /// Grid anchor: geodetic origin of the (north, east) frame.
  double anchor_lat_deg{39.5};
  double anchor_lon_deg{-105.5};

  /// TRAJECTORY mode constants (mirror the plant's own defaults).
  double trajectory_throttle_frac{0.6};
  double trajectory_steer_rate_deg_s{6.0};

  /// WAYPOINT mode: heading loop gain [deg/s per deg of error] and its
  /// steer-rate authority [deg/s].
  double heading_gain{0.8};
  double max_steer_rate_deg_s{30.0};

  /// WAYPOINT mode: cruise throttle fraction, and the approach gain
  /// [1/s]: the speed target is min(cruise, gain * remaining distance),
  /// so the plant's first-order speed lag (1 s) closes on the target
  /// critically damped instead of overshooting.
  double cruise_throttle_frac{0.5};
  double approach_gain_per_s{0.5};

  /// A leg is ARRIVED when the remaining distance is below this.
  double arrival_tolerance_m{0.5};

  /// Throttle fraction while the heading error exceeds
  /// `turn_in_place_deg` (the kinematic plant turns without speed;
  /// crawling keeps the motion legible on screen).
  double turn_in_place_deg{60.0};
  double turn_throttle_frac{0.1};
};

/* ----------------------------- RoverControllerState ----------------------------- */

struct RoverControllerState {
  std::uint64_t tick_count{0};

  /// Current target in the grid [m north, m east] and whether one is
  /// set. `target_seq` bumps on every new target so a consumer can
  /// tell a re-target from a repeat.
  double target_north_m{0.0};
  double target_east_m{0.0};
  std::uint8_t target_valid{0};
  std::uint8_t initialized{0};
  std::uint16_t target_seq{0};
  /// Last commanded target sequence and mode adopted from the plant's
  /// command state (edge-triggered adoption, as the aircraft's mask).
  std::uint16_t adopted_target_seq{0};
  std::uint8_t adopted_mode{255};
  std::uint8_t reserved[1]{};
};

/* ----------------------------- RoverControllerOutput ----------------------------- */

/**
 * @brief Controller OUTPUT: the drive command the plant consumes plus
 * diagnostics. The first three fields are laid out exactly as
 * GroundVehicleDriveCommand so the plant can be handed a pointer to
 * this block's head.
 */
struct RoverControllerOutput {
  /* ---- Drive command (consumed by GroundVehicle; layout pinned) ---- */
  double steer_rate_deg_s{0.0};
  double throttle_frac{0.0};
  std::uint8_t valid{0};
  std::uint8_t mode{0};    ///< DriveMode code in effect.
  std::uint8_t arrived{0}; ///< 1 once the current target is inside tolerance.
  std::uint8_t reserved0[5]{};

  /* ---- Diagnostics ---- */
  std::uint64_t tick{0};
  std::uint16_t target_seq{0};
  std::uint8_t reserved1[6]{};
  double grid_north_m{0.0}; ///< Vehicle position on the grid.
  double grid_east_m{0.0};
  double distance_m{0.0};        ///< Remaining distance to the target.
  double bearing_deg{0.0};       ///< Bearing to the target [deg from north, cw].
  double heading_error_deg{0.0}; ///< Wrapped bearing - heading [-180, 180].
};

} // namespace rover_controller
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_CONTROLLER_DATA_HPP
