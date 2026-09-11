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

/* ----------------------------- CelestialBodyTunables ----------------------------- */

/**
 * @brief Configuration parameters for a CelestialBody component.
 *
 * Selects (a) which body to represent, (b) per-subsystem fidelity, and
 * (c) the WORLD BINDING for file-backed fidelities: the uid of the
 * world bundle carrying this body's content artifacts, and the pinned
 * bundle content hash the master thereby authorizes. Analytic
 * fidelities (CONSTANT/SPHERE/ELLIPSOID terrain; CONSTANT/J2 gravity;
 * CONSTANT/EXPONENTIAL atmosphere) need no binding (world_uid 0).
 * A file-backed fidelity with no binding, a binding that resolves to
 * no bundle, or a bundle whose content hash differs from the pin all
 * refuse init loudly -- the world a config declares is the world that
 * runs, or nothing does.
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

  /// World bundle uid this body binds (reserved world range,
  /// e.g. 0x010100). Zero = unbound; valid only while every fidelity
  /// is analytic. Sits at offset 4 so the pin lands naturally
  /// 8-aligned with no hidden padding -- the toml layout mirrors the
  /// struct byte-for-byte.
  std::uint32_t world_uid{0};

  /// Pinned bundle content hash: the bundle bound at init must carry
  /// exactly this bundleContentHash. The authored toml states it; the
  /// master packs it; world_pack --verify prints the value to pin.
  std::uint64_t world_pin{0};
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

  /// World identity as bound at init (ground truth for INSPECT): the
  /// bundle uid and pin actually verified, plus the per-role inner
  /// spec hashes of the entries loaded (zero when the role was not
  /// loaded or the inner format predates provenance headers).
  std::uint32_t world_uid{0};
  std::uint32_t world_reserved{0};
  std::uint64_t world_pin{0};
  std::uint64_t atmosphere_spec_hash{0};
  std::uint64_t terrain_spec_hash{0};
};

/* ----------------------------- CelestialBodyTelemetry ----------------------------- */

/**
 * @brief Public-face telemetry (OUTPUT category) for downstream subscribers.
 *
 * Populated once at init from the underlying env models. Lets other
 * components -- and external bridges forwarding state to the consumer --
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
