#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_MOON_VACUUM_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_MOON_VACUUM_HPP
/**
 * @file VacuumAtmosphereModel.hpp
 * @brief Moon vacuum atmosphere model (ConstantAtmosphere with rho = T = P = 0).
 *
 * The Moon technically has a tenuous exosphere, but for sim purposes
 * (drag, propagation, control) it's vacuum. This wrapper makes the intent
 * explicit at the type level: callers asking for "Moon atmosphere" get
 * a ConstantAtmosphere where `isVacuum()` returns true and any drag
 * computation can short-circuit.
 *
 * Mirrors `terrain/moon/LolaTerrainModel.hpp` and
 * `gravity/moon/GrailModel.hpp`: a Moon wrapper that fills in body
 * defaults so callers don't have to.
 *
 * For other airless / near-vacuum bodies (asteroids, etc.) construct a
 * `ConstantAtmosphere` directly with the appropriate (rho, T, P).
 */

#include "src/sim/environment/atmosphere/inc/ConstantAtmosphere.hpp"

namespace sim {
namespace environment {
namespace atmosphere {
namespace moon {

/* ----------------------------- VacuumAtmosphereModel ----------------------------- */

class VacuumAtmosphereModel final : public ConstantAtmosphere {
public:
  /// Default construction yields the vacuum sentinel (rho = T = P = 0).
  /// Inherited `isVacuum()` returns true.
  VacuumAtmosphereModel() noexcept = default;
};

} // namespace moon
} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_MOON_VACUUM_HPP
