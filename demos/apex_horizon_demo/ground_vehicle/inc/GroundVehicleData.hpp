#ifndef APEX_HORIZON_DEMO_GROUND_VEHICLE_DATA_HPP
#define APEX_HORIZON_DEMO_GROUND_VEHICLE_DATA_HPP
/**
 * @file GroundVehicleData.hpp
 * @brief Tunable + state + telemetry structs for the GroundVehicle (rover).
 *
 * Scope:
 *   - 2D kinematic motion (constant-throttle forward + steady turn) on a
 *     celestial body's surface. Position carried as (lat, lon); altitude
 *     clamped to terrain elevation each tick (no real vertical dynamics).
 *   - Lidar sweep: N rays cast forward at angles spread across `fov_deg`.
 *     Each ray marches in `step_m` increments until it intersects terrain
 *     above the sensor height. Reports hit distances back via telemetry.
 *   - Slope detection from terrain gradient at the current location;
 *     flagged `is_slipping` when slope exceeds `max_slope_deg`.
 *
 * All structs trivially-copyable for TPRM compatibility.
 */

#include <cstdint>

namespace appsim {
namespace ground_vehicle {

/* ----------------------------- Constants ----------------------------- */

/// Maximum lidar rays the telemetry struct can hold. Tunables select
/// the actual `lidar_n_rays` to use (1..MAX_LIDAR_RAYS).
inline constexpr std::size_t MAX_LIDAR_RAYS = 16u;

/* ----------------------------- GroundVehicleTunables ----------------------------- */

/**
 * @brief Tunable parameters for the GroundVehicle component.
 * Trivially-copyable; 112 bytes (TPRM-compatible).
 */
struct GroundVehicleTunables {
  /// Vehicle mass [kg]. Currently informational; kinematic MVP doesn't
  /// integrate forces, but propagator-style refinements would use it.
  double mass_kg{1500.0};

  /// Maximum speed the vehicle will accelerate to [m/s].
  double max_speed_m_s{8.0};

  /// Default throttle setting (0..1). MVP applies this constantly; a
  /// future controller component would override it.
  double throttle_default{0.6};

  /// Steering rate [deg/s]. Constant turn = constant heading change.
  double steer_rate_deg_s{6.0};

  /// Initial geodetic position [deg].
  double init_lat_deg{39.5};
  double init_lon_deg{-105.5};

  /// Initial heading [deg from north, clockwise].
  double init_heading_deg{45.0};

  /// Slope above which the vehicle is flagged as slipping [deg].
  /// MVP just reports the flag; doesn't slow the vehicle.
  double max_slope_deg{25.0};

  /// Number of forward lidar rays (1..MAX_LIDAR_RAYS).
  std::uint32_t lidar_n_rays{8};
  std::uint32_t lidar_reserved{0};

  /// Maximum range a single lidar ray will march [m]. Beyond this,
  /// the ray reports "no hit" at max range.
  double lidar_max_range_m{500.0};

  /// Total angular field of view of the lidar fan [deg], centered on
  /// the vehicle heading.
  double lidar_fov_deg{90.0};

  /// Step size used when marching a lidar ray [m]. Smaller = finer
  /// hit-detection; coarser = faster.
  double lidar_step_m{5.0};

  /// Tag for log lines.
  char body_label[16]{};
};

/* ----------------------------- GroundVehicleState ----------------------------- */

/**
 * @brief Internal bookkeeping for the GroundVehicle.
 */
struct GroundVehicleState {
  /// Number of `vehicleStep` invocations.
  std::uint64_t tick_count{0};

  /// Number of integration steps that produced a position update
  /// (all tick_count usually, but separate for clarity).
  std::uint64_t step_count{0};

  /// True after the first tick has applied initial pose.
  std::uint8_t initialized{0};

  std::uint8_t reserved[7]{};
};

/* ----------------------------- GroundVehicleTelemetry ----------------------------- */

/**
 * @brief Public-face telemetry (OUTPUT) for the rover. 256 bytes.
 *
 * Wire-format-aligned: byte-identical with the consumer side's
 * RoverFrame contract (ROVR/1). The ShmRingBridge SUPPORT component
 * pure-memcpys this struct into the SPSC ring; the visualization
 * consumer reads it as RoverFrame. The two sides agree on bytes, not on
 * type names — the same decoupling pattern the file formats use.
 *
 * If you change this layout you MUST bump the ROVR wire-format version
 * on both sides of the bridge (this struct and the consumer's contract
 * header), otherwise old/new peers will silently disagree on what the
 * bytes mean.
 */
struct GroundVehicleTelemetry {
  /// Producer-side monotonic timestamp at the moment vehicleStep ran [ns].
  /// UE5 uses this to detect dropped frames + measure end-to-end latency.
  std::uint64_t timestamp_ns{0};

  /// Tick count at which this snapshot was produced.
  std::uint64_t tick{0};

  /// Pose: geodetic position + heading + altitude above body ref surface.
  double pos_lat_deg{0.0};
  double pos_lon_deg{0.0};
  double pos_alt_m{0.0};
  double heading_deg{0.0};

  /// Linear speed along the heading direction [m/s].
  double speed_m_s{0.0};

  /// Terrain elevation at the vehicle's current (lat, lon) [m].
  double ground_elevation_m{0.0};

  /// Slope of the terrain at the vehicle position [deg].
  /// Computed from the terrain gradient via 4-point sampling.
  double slope_deg{0.0};

  /// Azimuth (deg from north, clockwise) the slope rises toward.
  double slope_azimuth_deg{0.0};

  /// 1 iff slope_deg > tunables.max_slope_deg.
  std::uint8_t is_slipping{0};

  /// 1 iff the vehicle's current (lat, lon) is outside the body's
  /// terrain coverage (e.g., outside the htile bounds).
  std::uint8_t is_off_terrain{0};

  std::uint16_t reserved0{0};

  /// Number of valid lidar rays in this frame (mirror of
  /// tunables.lidar_n_rays at publish time). Indices
  /// [lidar_n_rays, MAX_LIDAR_RAYS) carry undefined data.
  std::uint32_t lidar_n_rays{0};

  /// Per-ray lidar return distance [m]. If `lidar_hit[i]` is 0 the
  /// value is `lidar_max_range_m` ("no hit within range").
  double lidar_range_m[MAX_LIDAR_RAYS]{};

  /// 1 iff the corresponding ray hit terrain within its march range.
  std::uint8_t lidar_hit[MAX_LIDAR_RAYS]{};

  /// Padding to a round 256 bytes; reserved for future fields without
  /// a wire-format version bump (e.g. wheel speeds, IMU, contact normals).
  std::uint8_t reserved1[24]{};
};

static_assert(sizeof(GroundVehicleTelemetry) == 256,
              "GroundVehicleTelemetry must match the ROVR/1 RoverFrame wire "
              "format: exactly 256 bytes");

} // namespace ground_vehicle
} // namespace appsim

#endif // APEX_HORIZON_DEMO_GROUND_VEHICLE_DATA_HPP
