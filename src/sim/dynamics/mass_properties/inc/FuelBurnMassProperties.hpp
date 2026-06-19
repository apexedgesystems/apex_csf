#ifndef APEX_SIM_DYNAMICS_MASS_PROPERTIES_FUEL_BURN_HPP
#define APEX_SIM_DYNAMICS_MASS_PROPERTIES_FUEL_BURN_HPP
/**
 * @file FuelBurnMassProperties.hpp
 * @brief Time-varying mass / CG / inertia from TSFC-driven fuel burn.
 *
 * Models a vehicle that loses mass at a rate proportional to the
 * thrust it produces:
 *
 *   mdot_fuel = -TSFC * T_total
 *
 * where TSFC is the thrust-specific fuel consumption (kg of fuel per
 * newton of thrust per second) and T_total is the summed thrust of all
 * engines: a thirstier engine, or more of them, burns mass faster.
 *
 *   m(t)   = m_empty + fuel(t)
 *   CG(t)  = CG_empty + (CG_full - CG_empty) * (fuel / fuel_capacity)
 *   I(t)   = I_empty + (I_full - I_empty) * (fuel / fuel_capacity)
 *
 * Linear interpolation is a reasonable first-order approximation for
 * fuel-tank mass distribution that doesn't shift dramatically (i.e.,
 * symmetric main wing tanks burned in proportion). Real aircraft burn
 * tanks in a CG-managed sequence, which this model does not represent.
 *
 * The struct defaults below are illustrative, notional values for an
 * unnamed vehicle; set the TSFC, mass endpoints, CG endpoints, and
 * inertia tensors per the specific vehicle being simulated.
 *
 * Inertia tensor scaling: linear with fuel mass is a coarse
 * approximation -- the inertia of liquid fuel about the CG depends on
 * tank geometry (parallel-axis offset), which the linear scaling
 * ignores.
 *
 * Coupling back to the controller: as mass + CG + inertia change, the
 * trim condition at steady flight drifts. The outer-loop holds integrate
 * that slow drift and re-trim automatically.
 */

#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"   // Vec3
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp" // InertiaTensor

namespace sim::dynamics::mass_properties {

/* ------------------------------ Params ------------------------------ */

/**
 * Fuel-burn + mass-property reference configuration.
 *
 * `*_full` and `*_empty` define the two endpoints of the linear
 * interpolation. The simulator linearly interpolates by current fuel
 * fraction (0 = empty, 1 = full).
 */
struct FuelBurnMassPropertiesParams {
  // All defaults are illustrative, notional values for an unnamed
  // vehicle. Override every field per the specific vehicle simulated.

  // ---- Fuel burn rate ----
  double TSFC_kg_per_N_s = 2.0e-5; // notional thrust-specific fuel consumption

  // ---- Mass endpoints ----
  double m_empty_kg = 1000.0;       // notional empty mass
  double fuel_capacity_kg = 1000.0; // notional max usable fuel

  // ---- CG endpoints (body-frame offset from reference, m) ----
  // CG_full means CG when tanks are full. CG_empty when tanks are empty.
  // Convention: +x forward, +y right, +z down (standard body axes).
  // CG migrates forward (+x) as fuel burns in this notional example.
  sim::dynamics::rigid_body::Vec3 cg_full_m = {0.0, 0.0, 0.0};
  sim::dynamics::rigid_body::Vec3 cg_empty_m = {0.1, 0.0, 0.0};

  // ---- Inertia tensor endpoints ----
  // I_full when tanks are full (heavier -> larger I). I_empty when empty.
  // Both in body axes about the *current* CG (we approximate the small
  // CG offset by ignoring the parallel-axis correction).
  // Notional: simple diagonal-ish tensor; empty ~60% of full.
  sim::dynamics::rigid_body::InertiaTensor I_full = {1000.0, 2000.0, 3000.0, 0.0};
  sim::dynamics::rigid_body::InertiaTensor I_empty = {600.0, 1200.0, 1800.0, 0.0};
};

/* ------------------------------ State ------------------------------ */

/** Internal fuel + bookkeeping. */
struct FuelBurnMassPropertiesState {
  double fuel_kg = 1000.0;           ///< current fuel mass; full at start (= fuel_capacity)
  double fuel_burned_total_kg = 0.0; ///< cumulative -- diagnostic
};

/* ------------------------------ Result ------------------------------ */

/** Per-step mass-property snapshot. */
struct FuelBurnMassPropertiesResult {
  double m_total_kg;                               ///< m_empty + fuel
  sim::dynamics::rigid_body::Vec3 cg_offset_m;     ///< body-frame CG offset
  sim::dynamics::rigid_body::InertiaTensor I_body; ///< body-frame inertia
  double fuel_kg;                                  ///< echoed from state
  double fuel_fraction;                            ///< fuel / fuel_capacity in [0, 1]
  double m_dot_fuel_kg_s;                          ///< instantaneous burn rate (positive = burning)
};

/* ------------------------------ Driver ------------------------------ */

/**
 * Advance one tick of fuel burn + recompute mass / CG / inertia.
 *
 * @param  s              state (mutated in place)
 * @param  p              parameters
 * @param  T_total_N      total thrust from all engines this tick (positive)
 * @param  dt_s           step (s)
 * @return current m, CG, I, fuel, mdot_fuel
 */
inline FuelBurnMassPropertiesResult stepFuelBurn(FuelBurnMassPropertiesState& s,
                                                 const FuelBurnMassPropertiesParams& p,
                                                 double T_total_N, double dt_s) {
  // ---- Fuel burn step (forward Euler -- fuel burn is slow vs dt) ----
  if (T_total_N < 0.0)
    T_total_N = 0.0; // negative thrust = no burn (idealization)
  const double m_dot_fuel = p.TSFC_kg_per_N_s * T_total_N;
  const double burned_this_step = m_dot_fuel * dt_s;

  // Drain fuel; clamp to zero (engines flame out at empty in real life,
  // but we just stop burning here).
  if (burned_this_step >= s.fuel_kg) {
    // Fuel just ran out this step.
    s.fuel_burned_total_kg += s.fuel_kg;
    s.fuel_kg = 0.0;
  } else {
    s.fuel_kg -= burned_this_step;
    s.fuel_burned_total_kg += burned_this_step;
  }

  // ---- Linear interpolation at current fuel fraction ----
  const double frac = (p.fuel_capacity_kg > 0.0) ? s.fuel_kg / p.fuel_capacity_kg : 0.0;

  FuelBurnMassPropertiesResult r;
  r.m_total_kg = p.m_empty_kg + s.fuel_kg;
  r.cg_offset_m =
      sim::dynamics::rigid_body::Vec3{p.cg_empty_m.x + (p.cg_full_m.x - p.cg_empty_m.x) * frac,
                                      p.cg_empty_m.y + (p.cg_full_m.y - p.cg_empty_m.y) * frac,
                                      p.cg_empty_m.z + (p.cg_full_m.z - p.cg_empty_m.z) * frac};
  r.I_body.Ixx = p.I_empty.Ixx + (p.I_full.Ixx - p.I_empty.Ixx) * frac;
  r.I_body.Iyy = p.I_empty.Iyy + (p.I_full.Iyy - p.I_empty.Iyy) * frac;
  r.I_body.Izz = p.I_empty.Izz + (p.I_full.Izz - p.I_empty.Izz) * frac;
  r.I_body.Ixz = p.I_empty.Ixz + (p.I_full.Ixz - p.I_empty.Ixz) * frac;
  r.fuel_kg = s.fuel_kg;
  r.fuel_fraction = frac;
  r.m_dot_fuel_kg_s = (s.fuel_kg > 0.0) ? m_dot_fuel : 0.0;
  return r;
}

} // namespace sim::dynamics::mass_properties

#endif // APEX_SIM_DYNAMICS_MASS_PROPERTIES_FUEL_BURN_HPP
