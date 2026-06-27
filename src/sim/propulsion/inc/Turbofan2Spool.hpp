#ifndef APEX_SIM_PROPULSION_TURBOFAN_2SPOOL_HPP
#define APEX_SIM_PROPULSION_TURBOFAN_2SPOOL_HPP
/**
 * @file Turbofan2Spool.hpp
 * @brief Two-spool turbofan with N1/N2 first-order spool dynamics.
 *
 * Models a high-bypass turbofan with two coupled shafts:
 *
 *   - N2 (high-pressure spool: HPC + HPT) -- driven by combustor fuel flow;
 *     first-order lag on throttle command with tau_N2 ~ 0.5 s.
 *   - N1 (low-pressure spool: fan + LPT) -- driven aerodynamically by N2's
 *     exhaust stream; first-order lag on N2 with tau_N1 ~ 2 s.
 *
 * The two-spool separate-exhaust cycle is the steady-state analysis behind
 * these dynamics; the spool-lag time constants are operational empirics
 * (rotor inertia + combustor authority), not derivable from the cycle.
 *
 * Idle floor: spools never decay below N1_idle_pct / N2_idle_pct (fuel cutoff
 * would be a separate kill switch).
 *
 * Thrust output: gross thrust scales with (N1 / N1_max)^2 and (rho/rho_0)^n.
 * The N1^2 nonlinearity comes from F ~ mdot_air * (V_e - V_0) where
 * mdot_air ~ N1 and V_e ~ sqrt(dT) with dT also rising with N1 -- the combined
 * effect is roughly quadratic over the operating range. It matters for
 * speed-hold control: a linear thrust model can let the speed loop go unstable
 * at low throttle.
 *
 *   T = T_max_sl * (N1 / N1_max)^2 * (rho/rho_0)^n_density
 *
 * For the gyroscopic effect of the spinning rotors on the Euler equations, the
 * model exposes the angular momentum of the rotating shafts via
 * `H_rotor_kgm2_s` in the result. The aircraft-side EOM adds H_rotor x omega_body
 * to the moment vector (per-engine; multiplied by engine count outside).
 */

#include <cmath>

namespace sim::propulsion {

/* -------------------------------- Params -------------------------------- */

/** Configurable two-spool turbofan parameters. Per-engine quantities;
 *  per-aircraft scaling (multi-engine sum) is handled by the caller.
 *  Defaults are illustrative high-bypass-turbofan values. */
struct Turbofan2SpoolParams {
  // ---- Maximum thrust + density scaling ----
  double T_max_sl_N = 250000.0; // per-engine max thrust at SL (N)
  double rho_ref_kg_m3 = 1.225; // reference density (USSA76 SL)
  double n_density = 0.7;       // (rho/rho_0)^n exponent (high-bypass ~ 0.7)

  // ---- N1/N2 spool dynamics ----
  double tau_N2_s = 0.5;      // HP spool time constant
  double tau_N1_s = 2.0;      // LP spool time constant (lag behind N2)
  double N1_max_rpm = 3900.0; // max N1 rotation rate
  double N2_max_rpm = 8800.0; // max N2 rotation rate
  double N1_idle_pct = 25.0;  // idle N1 (% of max) -- never decays below
  double N2_idle_pct = 60.0;  // idle N2 (% of max)

  // ---- Inertia of rotating mass (for the gyroscopic term) ----
  /// Total I_rotor about engine spin axis (LP + HP combined, per engine);
  /// sum across N engines for the aircraft's H_rotor reported to the EOM.
  double I_rotor_kgm2 = 150.0;
};

/* -------------------------------- State -------------------------------- */

/** Internal spool state. Caller advances via `stepTurbofan2Spool` each tick. */
struct Turbofan2SpoolState {
  double N1_pct = 25.0; // current N1 (% of max) -- initializes to idle
  double N2_pct = 60.0; // current N2 (% of max) -- initializes to idle
};

/* -------------------------------- Result -------------------------------- */

struct Turbofan2SpoolResult {
  double N1_pct;         // post-step N1 (% of N1_max)
  double N2_pct;         // post-step N2 (% of N2_max)
  double thrust_N;       // per-engine thrust at this state
  double H_rotor_kgm2_s; // per-engine angular momentum of LP+HP rotors
                         // (about engine spin axis, typically body x)
};

/* -------------------------------- Driver -------------------------------- */

/**
 * Advance one tick of the two-spool model.
 *
 * Spool dynamics (first-order lag on % rotation rate):
 *
 *   N2_target = max(throttle * 100, N2_idle_pct)
 *   dN2/dt    = (N2_target - N2) / tau_N2
 *
 *   N1_target = interpolated from N2, with idle floor N1_idle_pct
 *   dN1/dt    = (N1_target - N1) / tau_N1
 *
 * Backward Euler (semi-implicit) for unconditional stability at any dt:
 *   N(t+dt) = N(t) + dt/(tau + dt) * (N_target - N(t))
 *
 * @param  s          state (mutated in place)
 * @param  p          parameters
 * @param  throttle   command in [0, 1] (clamped internally)
 * @param  rho_kg_m3  ambient density (kg/m^3)
 * @param  dt_s       step (s)
 * @return per-engine thrust + N1/N2 + rotor angular momentum
 */
inline Turbofan2SpoolResult stepTurbofan2Spool(Turbofan2SpoolState& s,
                                               const Turbofan2SpoolParams& p, double throttle,
                                               double rho_kg_m3, double dt_s) {
  // Clamp throttle.
  if (throttle < 0.0) {
    throttle = 0.0;
  }
  if (throttle > 1.0) {
    throttle = 1.0;
  }

  // ---- N2 dynamics (driven by throttle) ----
  double N2_target = throttle * 100.0;
  if (N2_target < p.N2_idle_pct) {
    N2_target = p.N2_idle_pct;
  }
  const double k2 = dt_s / (p.tau_N2_s + dt_s); // backward-Euler smoothing
  s.N2_pct = s.N2_pct + k2 * (N2_target - s.N2_pct);

  // ---- N1 dynamics (driven by N2) ----
  // Aerodynamic coupling: N1 linearly interpolates from N1_idle (when N2 is at
  // idle) to 100% (when N2 is at 100%). The two spools have different idle %s;
  // the 100% (full-thrust) endpoints coincide. Linear interp between them.
  const double N2_span = 100.0 - p.N2_idle_pct;
  const double N1_span = 100.0 - p.N1_idle_pct;
  double N1_target = (N2_span > 0.0)
                         ? p.N1_idle_pct + (s.N2_pct - p.N2_idle_pct) * (N1_span / N2_span)
                         : p.N1_idle_pct;
  if (N1_target < p.N1_idle_pct) {
    N1_target = p.N1_idle_pct;
  }
  const double k1 = dt_s / (p.tau_N1_s + dt_s);
  s.N1_pct = s.N1_pct + k1 * (N1_target - s.N1_pct);

  // ---- Thrust output: T_max * (N1/100)^2 * (rho/rho_0)^n ----
  double thrust_N = 0.0;
  if (rho_kg_m3 > 0.0) {
    const double N1_frac = s.N1_pct / 100.0;
    const double rho_ratio = rho_kg_m3 / p.rho_ref_kg_m3;
    const double rho_scale = std::pow(rho_ratio, p.n_density);
    thrust_N = p.T_max_sl_N * (N1_frac * N1_frac) * rho_scale;
  }

  // ---- Rotor angular momentum (per engine) for the gyroscopic term ----
  // omega_rotor = N1_pct * (N1_max_rpm / 100) * 2*pi/60  rad/s
  const double omega_rad_s = (s.N1_pct / 100.0) * p.N1_max_rpm * (2.0 * M_PI / 60.0);
  const double H_rotor = p.I_rotor_kgm2 * omega_rad_s;

  return Turbofan2SpoolResult{s.N1_pct, s.N2_pct, thrust_N, H_rotor};
}

} // namespace sim::propulsion

#endif // APEX_SIM_PROPULSION_TURBOFAN_2SPOOL_HPP
