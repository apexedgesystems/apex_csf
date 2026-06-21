#ifndef APEX_SIM_DYNAMICS_MASS_PROPERTIES_FUEL_BURN_HPP
#define APEX_SIM_DYNAMICS_MASS_PROPERTIES_FUEL_BURN_HPP
/**
 * @file FuelBurnMassProperties.hpp
 * @brief A fuel tank as a time-varying mass *source*.
 *
 * Models a fuel tank that loses mass at a rate proportional to the
 * thrust it feeds:
 *
 *   mdot_fuel = TSFC * T_total
 *
 * where TSFC is the thrust-specific fuel consumption (kg of fuel per
 * newton of thrust per second) and T_total is the summed thrust of all
 * engines: a thirstier engine, or more of them, burns mass faster.
 *
 * The tank is a `DynamicMassSource`: it holds its own parameters (tank
 * location, full-fuel own-inertia, capacity, TSFC) and its current fuel
 * state, and reports its *current* contribution via `current()`:
 *
 *   current() = { mass = fuel(t), cg = tank location,
 *                 inertia = I_fuel_full * (fuel / capacity) }
 *
 * It does NOT pretend to be the whole vehicle. A whole vehicle is
 *
 *   MassAccumulator acc;
 *   acc.add(dryStructure);
 *   acc.add(fuelTank);
 *   auto vehicle = acc.result();
 *
 * which combines the tank with the dry structure (and any other sources)
 * to produce whole-vehicle mass / CG / inertia about the net CG -- with
 * the parallel-axis cross terms a real CG shift creates.
 *
 * The fuel's own inertia scales with its current mass fraction (a coarse
 * but monotone approximation -- the true tensor depends on tank geometry
 * as the level drops). The parallel-axis offset between the tank CG and
 * the vehicle CG is handled exactly by `MassAccumulator`, not here.
 *
 * All defaults are illustrative, notional values for an unnamed vehicle;
 * set the TSFC, capacity, tank location, and the full-tank inertia per
 * the specific vehicle being simulated.
 */

#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"   // Vec3
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp" // InertiaTensor

namespace sim::dynamics::mass_properties {

/* ------------------------------ Params ------------------------------ */

/**
 * Fuel-tank reference configuration.
 *
 * The tank has a fixed location and a full-tank inertia (about its own
 * CG); the current fuel mass and its inertia scale down toward zero as
 * fuel burns.
 */
struct FuelBurnMassPropertiesParams {
  // All defaults are illustrative, notional values for an unnamed
  // vehicle. Override every field per the specific vehicle simulated.

  // ---- Fuel burn rate ----
  double TSFC_kg_per_N_s = 2.0e-5; // notional thrust-specific fuel consumption

  // ---- Tank capacity ----
  double fuel_capacity_kg = 1000.0; // notional max usable fuel

  // ---- Tank location (body-frame offset from reference, m) ----
  // Convention: +x forward, +y right, +z down (standard body axes).
  rigid_body::Vec3 cg_tank_m = {0.0, 0.0, 0.0};

  // ---- Fuel inertia at full tank (about the fuel's own CG) ----
  // The fuel's own-CG inertia at full tank; it scales linearly with the
  // current fuel fraction (coarse but monotone). The parallel-axis term
  // to the vehicle CG is applied by `MassAccumulator`, not here.
  rigid_body::InertiaTensor I_fuel_full = {100.0, 200.0, 300.0, 0.0};
};

/* ------------------------------ Source ------------------------------ */

/**
 * A fuel tank as a `DynamicMassSource`.
 *
 * Holds the tank parameters plus the current fuel state. `current()`
 * reports the tank as a `MassContributor`; `step` advances the burn.
 *
 * @var params               tank reference configuration (location, inertia, TSFC)
 * @var fuel_kg              current usable fuel mass (kg)
 * @var fuel_burned_total_kg cumulative fuel burned (kg) -- diagnostic
 * @var last_m_dot_fuel_kg_s instantaneous burn rate from the last `step` -- diagnostic
 */
struct FuelTankMassSource : DynamicMassSource {
  FuelBurnMassPropertiesParams params;
  double fuel_kg = 1000.0;
  double fuel_burned_total_kg = 0.0;
  double last_m_dot_fuel_kg_s = 0.0;

  /** Current fuel fraction in [0, 1] (0 when capacity is non-positive). */
  [[nodiscard]] double fuelFraction() const noexcept {
    return (params.fuel_capacity_kg > 0.0) ? fuel_kg / params.fuel_capacity_kg : 0.0;
  }

  /**
   * Report the current fuel as a contributor.
   *
   *   mass     = current fuel mass
   *   cg       = tank location
   *   inertia  = full-tank fuel inertia scaled by the current fuel fraction
   */
  [[nodiscard]] MassContributor current() const noexcept override {
    const double frac = fuelFraction();
    MassContributor c;
    c.mass_kg = fuel_kg;
    c.cg_m = params.cg_tank_m;
    c.inertia_about_own_cg =
        rigid_body::InertiaTensor{params.I_fuel_full.Ixx * frac, params.I_fuel_full.Iyy * frac,
                                  params.I_fuel_full.Izz * frac, params.I_fuel_full.Ixz * frac,
                                  params.I_fuel_full.Ixy * frac, params.I_fuel_full.Iyz * frac};
    return c;
  }

  /**
   * Advance one tick of fuel burn.
   *
   *   mdot_fuel = TSFC * T_total       (negative thrust -> no burn)
   *   fuel_kg  -= mdot_fuel * dt       (clamped at 0; engines just stop)
   *
   * After running out the burn rate reported is 0 (flame-out idealization).
   *
   * @param thrust_N  total thrust from all engines this tick (positive)
   * @param dt        step (s)
   * @return instantaneous burn rate this tick (kg/s; 0 if empty/idle)
   */
  double step(double thrust_N, double dt) noexcept {
    if (thrust_N < 0.0) {
      thrust_N = 0.0; // negative thrust = no burn (idealization)
    }
    const double m_dot_fuel = params.TSFC_kg_per_N_s * thrust_N;
    const double burned_this_step = m_dot_fuel * dt;

    if (burned_this_step >= fuel_kg) {
      // Fuel just ran out this step (or was already empty).
      fuel_burned_total_kg += fuel_kg;
      fuel_kg = 0.0;
    } else {
      fuel_kg -= burned_this_step;
      fuel_burned_total_kg += burned_this_step;
    }

    last_m_dot_fuel_kg_s = (fuel_kg > 0.0) ? m_dot_fuel : 0.0;
    return last_m_dot_fuel_kg_s;
  }
};

} // namespace sim::dynamics::mass_properties

#endif // APEX_SIM_DYNAMICS_MASS_PROPERTIES_FUEL_BURN_HPP
