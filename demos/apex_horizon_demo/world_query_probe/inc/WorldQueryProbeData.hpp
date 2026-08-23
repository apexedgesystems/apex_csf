#ifndef APEX_HORIZON_DEMO_WORLD_QUERY_PROBE_DATA_HPP
#define APEX_HORIZON_DEMO_WORLD_QUERY_PROBE_DATA_HPP
/**
 * @file WorldQueryProbeData.hpp
 * @brief Tunable + state structs for the WorldQueryProbe demo component.
 *
 * The probe rotates through a small list of well-known points on the
 * body it's attached to, querying gravity / terrain / atmosphere at
 * each point on every tick. Point count is bounded by tunables to keep
 * the struct trivially-copyable (TPRM requirement).
 */

#include <cstddef>
#include <cstdint>

namespace appsim {
namespace world_query_probe {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of survey points held in tunables. Probe rotates
/// through them each tick. Keep small to bound struct size.
inline constexpr std::size_t MAX_POINTS = 8u;

/* ----------------------------- WorldQueryProbeTunables ----------------------------- */

/**
 * @brief Tunable parameters for a WorldQueryProbe.
 *
 * Trivially-copyable. Stores up to MAX_POINTS (lat, lon) survey points;
 * the probe round-robins through the first `num_points`. `body_label`
 * is a short tag that goes into log lines so multi-probe demos
 * distinguish their output.
 */
struct WorldQueryProbeTunables {
  /// How many entries of `lat_deg` / `lon_deg` are valid (1..MAX_POINTS).
  std::uint32_t num_points{0};
  std::uint32_t reserved0{0};
  /// Survey latitudes in degrees (-90..90).
  double lat_deg[MAX_POINTS]{};
  /// Survey longitudes in degrees (-180..180 or 0..360, body-dependent).
  double lon_deg[MAX_POINTS]{};
  /// Altitude above the body's reference surface for atmosphere queries [m].
  double atmosphere_alt_m{0.0};
  /// Radius from body center for the gravity sample query [m].
  /// 0 means "use ref_radius + atmosphere_alt_m".
  double gravity_query_radius_m{0.0};
  /// Body tag (NUL-padded), inserted into log lines.
  char body_label[16]{};
};

/* ----------------------------- WorldQueryProbeState ----------------------------- */

/**
 * @brief Internal bookkeeping (STATE category) for the WorldQueryProbe.
 *
 * Tick counters and round-robin cursor. Not the public face — for the
 * sample values themselves see `WorldQueryProbeTelemetry` (OUTPUT).
 */
struct WorldQueryProbeState {
  /// Number of probe ticks completed.
  std::uint64_t tick_count{0};
  /// Index of the survey point that will be sampled on the next tick
  /// (0..num_points-1, wraps).
  std::uint32_t next_point_index{0};
  std::uint32_t reserved0{0};
  /// Number of consecutive failed queries (gravity/terrain/atmosphere
  /// returning false). Resets to 0 on a successful query.
  std::uint32_t consecutive_failures{0};
  std::uint32_t reserved1{0};
};

/* ----------------------------- WorldQueryProbeTelemetry ----------------------------- */

/**
 * @brief Public-face telemetry (OUTPUT category) for downstream subscribers.
 *
 * Refreshed every tick with the latest sampled environmental state.
 * Other components (system monitors, ground stations, future
 * BridgeWriter pushing to the consumer) read this without needing to touch the
 * underlying CelestialBody / env model handles.
 */
struct WorldQueryProbeTelemetry {
  /// Tick count at which this snapshot was produced.
  std::uint64_t last_tick{0};
  /// Index into the tunables' `lat_deg` / `lon_deg` arrays that was sampled.
  std::uint32_t last_point_index{0};
  /// 1 iff the most recent query succeeded (all of gravity/terrain/atm OK).
  std::uint8_t last_query_succeeded{0};
  std::uint8_t reserved0[3]{};

  /// Latitude / longitude of the last sample [deg].
  double last_lat_deg{0.0};
  double last_lon_deg{0.0};

  /// Last terrain elevation above the body's reference surface [m].
  double last_terrain_elevation_m{0.0};

  /// Last atmospheric state at the sample altitude. Zeros for vacuum bodies.
  double last_atmosphere_density_kg_m3{0.0};
  double last_atmosphere_pressure_Pa{0.0};
  double last_atmosphere_temperature_K{0.0};
  double last_atmosphere_sound_speed_m_s{0.0};

  /// Last gravity-acceleration magnitude at the sample radius [m/s^2].
  double last_gravity_magnitude_m_s2{0.0};
};

} // namespace world_query_probe
} // namespace appsim

#endif // APEX_HORIZON_DEMO_WORLD_QUERY_PROBE_DATA_HPP
