#ifndef APEX_SIM_PROPULSION_DENSITY_SCALED_THRUST_HPP
#define APEX_SIM_PROPULSION_DENSITY_SCALED_THRUST_HPP
/**
 * @file DensityScaledThrust.hpp
 * @brief Empirical thrust model: scales sea-level thrust by ambient density.
 *
 *   T(throttle, rho) = T_max_sl * throttle * (rho / rho_0)^n
 *
 * Where:
 *   T_max_sl  - max thrust at sea level (N)
 *   throttle  - command in [0, 1]
 *   rho_0     - reference (sea-level) density (kg/m^3)
 *   n         - density exponent (~1.0 for turbojets; lower for high-bypass
 *               turbofans; ~0 for rockets; n=0.7 is a common approximation)
 *
 * The cheap baseline rung; the two-spool turbofan adds N1/N2 spool dynamics,
 * throttle lag, and rotor angular momentum.
 */

#include <cmath>

namespace sim::propulsion {

/** Configurable density-scaled thrust model. */
struct DensityScaledThrustParams {
  double T_max_sl_N = 1.0e6;    // max thrust at sea level (N) -- large 4-engine transport ~1 MN
  double rho_ref_kg_m3 = 1.225; // reference density (kg/m^3, sea-level USSA76)
  double n_density = 0.7;       // density exponent
  double throttle_min = 0.0;    // floor (idle)
  double throttle_max = 1.0;    // ceiling
};

/**
 * Evaluate thrust at a flight condition.
 *
 * Throttle is clamped to [throttle_min, throttle_max]. If rho <= 0 (vacuum /
 * fault), thrust is zero -- air-breathing engines need air.
 */
inline double evaluateThrust(const DensityScaledThrustParams& p, double throttle,
                             double rho_kg_m3) {
  if (rho_kg_m3 <= 0.0) {
    return 0.0;
  }
  if (throttle < p.throttle_min) {
    throttle = p.throttle_min;
  }
  if (throttle > p.throttle_max) {
    throttle = p.throttle_max;
  }
  const double rho_ratio = rho_kg_m3 / p.rho_ref_kg_m3;
  const double scale = std::pow(rho_ratio, p.n_density);
  return p.T_max_sl_N * throttle * scale;
}

} // namespace sim::propulsion

#endif // APEX_SIM_PROPULSION_DENSITY_SCALED_THRUST_HPP
