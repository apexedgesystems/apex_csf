#ifndef APEX_SIM_ENVIRONMENT_CELESTIAL_BODY_DATA_HPP
#define APEX_SIM_ENVIRONMENT_CELESTIAL_BODY_DATA_HPP
/**
 * @file CelestialBodyData.hpp
 * @brief Tunable parameter struct + state struct for CelestialBody.
 *
 * `CelestialBodyTunables` is the trivially-copyable struct that
 * configures a CelestialBody at startup. It selects which celestial
 * body the component represents and at what fidelity each environment
 * subsystem (gravity, terrain, atmosphere) runs.
 *
 * Trivially-copyable invariant required by `TunableParam<T>` -- no
 * std::string members; file paths use fixed-size char buffers.
 */

#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"

#include <cstddef>
#include <cstdint>

namespace sim {
namespace environment {
namespace celestial_body {

/* ----------------------------- Constants ----------------------------- */

/// Maximum length of a data-file path stored in tunables. 256 covers any
/// practical relative-or-absolute filesystem path; longer paths would
/// likely indicate a misconfiguration.
inline constexpr std::size_t MAX_DATA_PATH = 256u;

/* ----------------------------- CelestialBodyTunables ----------------------------- */

/**
 * @brief Configuration parameters for a CelestialBody component.
 *
 * Selects (a) which body to represent and (b) per-subsystem fidelity +
 * (c) data file paths for file-backed fidelities. For analytic
 * fidelities (CONSTANT/SPHERE/ELLIPSOID for terrain; CONSTANT/J2 for
 * gravity; CONSTANT/EXPONENTIAL for atmosphere), the corresponding
 * data path is ignored.
 */
struct CelestialBodyTunables {
  /// Which celestial body this component represents.
  sim::environment::Body body{sim::environment::Body::OTHER};

  /// Gravity fidelity: CONSTANT, J2, or SPHERICAL.
  sim::environment::GravityFidelity gravity_fidelity{sim::environment::GravityFidelity::CONSTANT};

  /// Terrain fidelity: CONSTANT, SPHERE, ELLIPSOID, or HTILE.
  sim::environment::TerrainFidelity terrain_fidelity{sim::environment::TerrainFidelity::CONSTANT};

  /// Atmosphere fidelity: CONSTANT, EXPONENTIAL, LAYERED, or EMPIRICAL.
  sim::environment::AtmosphereFidelity atmosphere_fidelity{
      sim::environment::AtmosphereFidelity::CONSTANT};

  /// Path to the gravity coefficient .bin file (only used when
  /// gravity_fidelity == SPHERICAL). NUL-padded.
  char gravity_data_path[MAX_DATA_PATH]{};

  /// Path to the terrain .htile file (only used when terrain_fidelity == HTILE).
  char terrain_data_path[MAX_DATA_PATH]{};

  /// Path to the atmosphere .atm file (only used when atmosphere_fidelity == LAYERED).
  char atmosphere_data_path[MAX_DATA_PATH]{};
};

/* ----------------------------- CelestialBodyState ----------------------------- */

/**
 * @brief Internal lifecycle bookkeeping (STATE category).
 *
 * Tracks the component's init progress. Not the public face of the
 * component -- for the body identity / physical summary that other
 * components should consume, see `CelestialBodyTelemetry` (OUTPUT).
 */
struct CelestialBodyState {
  /// True iff the env factory built non-null models for all 3 subsystems.
  std::uint8_t env_built{0};

  /// True iff every file-backed model loaded its data file successfully.
  std::uint8_t data_loaded{0};

  /// 0 = not initialized; 1 = initialized OK; 2 = init failed.
  std::uint8_t init_status{0};

  std::uint8_t reserved[5]{};
};

/* ----------------------------- CelestialBodyTelemetry ----------------------------- */

/**
 * @brief Public-face telemetry (OUTPUT category) for downstream subscribers.
 *
 * Populated once at init from the underlying env models. Lets other
 * components -- and external bridges forwarding state to UE5 --
 * read the body's identity + physical summary without dynamic_casting
 * through the polymorphic `gravity()` / `terrain()` / `atmosphere()`
 * accessors. Trivially copyable so it can be packed onto the wire.
 *
 * NOT updated per-tick (CelestialBody is passive); a full re-init or
 * a configure() apply() would be required to refresh values.
 */
struct CelestialBodyTelemetry {
  /// Mirrored body / fidelity discriminators (uint8 of the underlying enums).
  std::uint8_t body;
  std::uint8_t gravity_fidelity;
  std::uint8_t terrain_fidelity;
  std::uint8_t atmosphere_fidelity;
  /// 0 = not initialized; 1 = initialized OK; 2 = init failed.
  std::uint8_t init_status;
  /// 1 iff the atmosphere model reports `isVacuum()` -- lets drag
  /// computations short-circuit without a virtual call.
  std::uint8_t is_vacuum_atmosphere;
  std::uint8_t reserved0[2];

  /// Body reference radius from the gravity model [m]. 0 if unavailable.
  double reference_radius_m;
  /// Surface gravity magnitude (queried at +X = ref_radius along x-axis) [m/s^2].
  double surface_gravity_m_s2;
  /// Atmosphere density at altitude=0 (where applicable) [kg/m^3]. 0 for vacuum.
  double surface_atmosphere_density_kg_m3;
  /// Atmosphere temperature at altitude=0 (where applicable) [K]. 0 for vacuum.
  double surface_atmosphere_temperature_K;

  /// Max degree of the gravity model (0 for analytic, N for spherical harmonics).
  std::int16_t gravity_max_degree;
  std::uint8_t reserved1[6];
};

} // namespace celestial_body
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_CELESTIAL_BODY_DATA_HPP
