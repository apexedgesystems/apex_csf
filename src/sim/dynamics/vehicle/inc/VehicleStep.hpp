#ifndef APEX_SIM_DYNAMICS_VEHICLE_VEHICLE_STEP_HPP
#define APEX_SIM_DYNAMICS_VEHICLE_VEHICLE_STEP_HPP
/**
 * @file VehicleStep.hpp
 * @brief Vehicle-level 6-DOF step that consumes the compositional aggregates.
 *
 * `rigid_body` is the lowest layer and intentionally does not depend on
 * `mass_properties` or `wrench`. This module sits one level up,
 * includes all three, and offers `vehicle::step` -- a 6-DOF step that takes
 * the aggregated mass properties and net force/moment produced by the
 * composition layers, so a caller that has built a vehicle from sources can
 * step it without manually unpacking force / moment / mass / inertia.
 *
 *   mass_properties::MassAccumulator mass;
 *   mass.add(dry);
 *   mass.add(fuelTank);
 *   const auto mp = mass.result();
 *
 *   wrench::WrenchAccumulator loads;
 *   loads.add(engine);
 *   loads.add(aero);
 *   const auto fm = loads.resultAbout(mp.cg_m);
 *
 *   vehicle::step(state, mp, fm, t, dt);
 *
 * The reference point the moment is taken about must be the same CG used by
 * the inertia tensor (mp.cg_m) for the moment to be consistent with Euler's
 * equations; that is the caller's responsibility.
 */

#include "src/sim/dynamics/wrench/inc/Wrench.hpp"
#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

namespace sim::dynamics::vehicle {

/**
 * One RK4 step of 6-DOF dynamics driven by the compositional aggregates.
 *
 * Forwards to the callback-based `rigid_body::stepRigidBody6DOF` with constant
 * force / moment / mass / inertia for this step:
 *   force_body  = fm.force
 *   moment_body = fm.moment
 *   mass_kg     = mp.mass_kg
 *   I           = mp.inertia_about_cg
 *
 * @param state  mutated in place
 * @param mp     aggregated whole-body mass properties (about its CG)
 * @param fm     net force + moment about the same CG
 * @param t      current time (s)
 * @param dt     step (s)
 */
inline void step(rigid_body::RigidBody6DOFState& state,
                 const mass_properties::AggregateMassProperties& mp, const wrench::Wrench& fm,
                 double t, double dt) {
  const rigid_body::Vec3 force = fm.force;
  const rigid_body::Vec3 moment = fm.moment;
  rigid_body::stepRigidBody6DOF(
      state, [&force](double, const rigid_body::RigidBody6DOFState&) { return force; },
      [&moment](double, const rigid_body::RigidBody6DOFState&) { return moment; }, mp.mass_kg,
      mp.inertia_about_cg, t, dt);
}

} // namespace sim::dynamics::vehicle

#endif // APEX_SIM_DYNAMICS_VEHICLE_VEHICLE_STEP_HPP
