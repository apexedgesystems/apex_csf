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

  /// TRAJECTORY mode: a constant steering angle at a constant throttle
  /// (a circle of radius wheelbase / tan(angle): 1.5 m at 5 deg is
  /// 17 m).
  double trajectory_throttle_frac{0.6};
  double trajectory_steer_deg{5.0};

  /// WAYPOINT mode, pure pursuit along the leg: the aim point sits on
  /// the line from where the leg started to its target, lookahead_m
  /// ahead of the rover's projection onto that line and running past
  /// the target, so an off-axis start converges onto the leg and the
  /// last metres are driven straight along it (the rover arrives
  /// aligned with the leg, and the next leg starts square). The
  /// steering angle comes from the plant's geometry: delta =
  /// atan(2 L sin(alpha) / d), alpha the bearing error to the aim
  /// point, d its distance.
  double wheelbase_m{1.5};
  double max_steer_deg{33.0};
  double lookahead_m{2.5};

  /// WAYPOINT mode, speed profile: cruise fraction of the plant's max
  /// speed; braking deceleration for the trapezoidal ramp-down (speed
  /// target = min(cruise, sqrt(2 a d)) so the rover stops on the target
  /// under the plant's own braking limit); a corner speed factor while
  /// the bearing error exceeds corner_deg, held low enough that a corner
  /// reads as a turn on screen (0.6 m/s at 33° lock is about 15°/s, a
  /// 90° turn in 6 s on a 2.3 m radius).
  double cruise_throttle_frac{0.375}; ///< 3 m/s of 8.
  double brake_m_s2{2.0};
  double corner_speed_frac{0.2};
  double corner_deg{15.0};

  /// A leg is ARRIVED when the remaining distance is below this, or
  /// once the rover has passed the leg's end within passed_end_cross_m
  /// of the line (a cut corner completes the leg instead of looping
  /// back for the point).
  double arrival_tolerance_m{0.2};
  double passed_end_cross_m{0.5};

  /// Hard bound on the heading rate: the steering lock is limited by
  /// the current speed so v tan(delta) / L never exceeds this (a corner
  /// entered at cruise is taken on a wider arc until the rover has
  /// slowed, instead of snapping at full lock).
  double max_yaw_rate_deg_s{15.0};
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
  /// Where the current leg started (the rover's grid position when the
  /// target was set): with the target it defines the line the rover
  /// follows.
  double leg_start_north_m{0.0};
  double leg_start_east_m{0.0};
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
  double steer_angle_deg{0.0};
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
  double bearing_deg{0.0};       ///< Bearing to the aim point [deg from north, cw].
  double heading_error_deg{0.0}; ///< Wrapped bearing - heading [-180, 180].
  double cross_track_m{0.0};     ///< Signed distance from the leg line (+ right of it).
};

} // namespace rover_controller
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_CONTROLLER_DATA_HPP
