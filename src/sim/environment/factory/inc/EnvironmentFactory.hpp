#ifndef APEX_SIM_ENVIRONMENT_FACTORY_HPP
#define APEX_SIM_ENVIRONMENT_FACTORY_HPP
/**
 * @file EnvironmentFactory.hpp
 * @brief Cross-subsystem factory for gravity, terrain, and atmosphere
 *        models, dispatching on (Body, fidelity).
 *
 * Returns models polymorphically through their respective base interfaces
 * (`gravity::GravityModelBase`, `terrain::TerrainModelBase`,
 * `atmosphere::AtmosphereModelBase`) so callers can swap fidelity levels
 * without recompiling consumer code.
 *
 * Initialization contract:
 *   - Analytic / body-default models (CONSTANT / SPHERE / ELLIPSOID, J2 and
 *     EXPONENTIAL with body defaults, and the Earth/Moon wrappers) are
 *     returned READY-TO-USE: the factory has populated all body params from
 *     internal constants.
 *   - File/coeff-backed models (gravity SPHERICAL, terrain HTILE for OTHER,
 *     atmosphere LAYERED for OTHER) are returned UNINITIALIZED. Caller must
 *     `init(coeffSource, params)` for gravity SPHERICAL or `load(path)` for
 *     terrain HTILE / atmosphere LAYERED.
 *
 * For Body::OTHER, models with no analytic body default (gravity J2, terrain
 * HTILE, atmosphere LAYERED) are returned uninitialized -- the factory has no
 * body defaults to apply, and the caller must supply them.
 *
 * Failure: a make*Model call returns nullptr only for a fidelity it cannot
 * satisfy (the reserved atmosphere EMPIRICAL slot, or an enum value outside
 * the defined ladder). Every defined (Body, fidelity) pair returns a non-null
 * model; the unique_ptr return type means a failed call leaks nothing.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/factory/inc/Body.hpp"
#include "src/sim/environment/factory/inc/EnvironmentFidelity.hpp"
#include "src/sim/environment/gravity/inc/GravityModelBase.hpp"
#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"

#include <memory>

namespace sim {
namespace environment {

/* ----------------------------- API ----------------------------- */

/// Build a gravity model for `body` at the requested `fidelity`.
/// Returns nullptr only for invalid fidelity values.
std::unique_ptr<gravity::GravityModelBase> makeGravityModel(Body body, GravityFidelity fidelity);

/// Build a terrain model for `body` at the requested `fidelity`.
/// Returns nullptr only for invalid fidelity values.
std::unique_ptr<terrain::TerrainModelBase> makeTerrainModel(Body body, TerrainFidelity fidelity);

/// Build an atmosphere model for `body` at the requested `fidelity`.
///   - CONSTANT: returns ready (vacuum if Moon, sea-level if Earth, vacuum if OTHER).
///   - EXPONENTIAL: returns ready with body defaults (Earth tropo / Moon vacuum)
///     or default Earth-tropo for OTHER.
///   - LAYERED: Earth -> Ussa76 ready; Moon -> vacuum ready; OTHER -> uninitialized
///     (caller must `load(path)` on the returned LayeredAtmosphere).
///   - EMPIRICAL: not yet implemented; returns nullptr.
std::unique_ptr<atmosphere::AtmosphereModelBase> makeAtmosphereModel(Body body,
                                                                     AtmosphereFidelity fidelity);

/* ----------------------------- Bundled creation ----------------------------- */

/// Bundle of polymorphic models returned by `makeEnvironment`. Each
/// pointer is built per its respective fidelity in the EnvironmentSpec.
/// Fields for unported subsystems (magnetic, ...) land here as they're added.
struct EnvironmentModels {
  std::unique_ptr<gravity::GravityModelBase> gravity;
  std::unique_ptr<terrain::TerrainModelBase> terrain;
  std::unique_ptr<atmosphere::AtmosphereModelBase> atmosphere;
};

/// Spec for `makeEnvironment`: pick a body + per-subsystem fidelity.
/// Each fidelity defaults to CONSTANT so callers can opt in only to
/// the subsystems they need. Adding a new subsystem only adds an
/// opt-in field here -- no caller code has to change unless it wants
/// the new subsystem.
struct EnvironmentSpec {
  Body body = Body::OTHER;
  GravityFidelity gravity = GravityFidelity::CONSTANT;
  TerrainFidelity terrain = TerrainFidelity::CONSTANT;
  AtmosphereFidelity atmosphere = AtmosphereFidelity::CONSTANT;
};

/// Build all subsystem models for `spec` in one call.
EnvironmentModels makeEnvironment(const EnvironmentSpec& spec);

} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_FACTORY_HPP
