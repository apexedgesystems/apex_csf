#ifndef APEX_SIM_DYNAMICS_DISTURBANCE_DRYDEN_TURBULENCE_HPP
#define APEX_SIM_DYNAMICS_DISTURBANCE_DRYDEN_TURBULENCE_HPP
/**
 * @file DrydenTurbulence.hpp
 * @brief Three-axis Dryden continuous turbulence model (MIL-HDBK-1797).
 *
 * Generates body-frame gust velocity components (u_g, v_g, w_g) by
 * passing white noise through transfer functions whose magnitude-
 * squared equals the Dryden power spectral densities:
 *
 *   Phi_u(Omega) = sigma_u^2 * (2 L_u / pi)            / (1 + (L_u Omega)^2)
 *   Phi_v(Omega) = sigma_v^2 * (L_v / pi)              * (1 + 12 (L_v Omega)^2) / (1 + 4 (L_v
 * Omega)^2)^2 Phi_w(Omega) = sigma_w^2 * (L_w / pi)              * (1 + 12 (L_w Omega)^2) / (1 + 4
 * (L_w Omega)^2)^2
 *
 * where Omega = omega / V is spatial frequency (rad/m), L is the turbulence
 * scale length (m), and sigma is the RMS intensity (m/s).
 *
 * Realization (continuous-time, then discrete via backward Euler):
 *
 *   H_u(s) = sigma_u * sqrt(2 L_u / V) * 1                / (1 + (L_u/V)*s)
 *   H_v(s) = sigma_v * sqrt(2 L_v / V) * (1 + sqrt(3)*(L_v/V)*s) / (1 + (L_v/V)*s)^2
 *   H_w(s) = sigma_w * sqrt(2 L_w / V) * (1 + sqrt(3)*(L_w/V)*s) / (1 + (L_w/V)*s)^2
 *
 * Driven by N(0, 1/dt) white noise (so the discrete PSD of the noise
 * input is unity). Gain calibration: for first-order y' = -y/tau + K*u
 * with white-noise input variance 1/dt, steady-state sigma_y^2 = K^2/(2 tau+dt).
 * Setting K = sigma*sqrt(2L/V) makes sigma_y -> sigma as dt -> 0. The textbook form
 * `K = sigma*sqrt(2L/(pi V))` corresponds to a different definition of "unit"
 * white-noise PSD (variance per Hz vs. variance per rad/s); we calibrate
 * to match the discrete realization here.
 *
 * The u_g / v_g / w_g body-axis gust-velocity components follow the Dryden
 * form standardized in MIL-HDBK-1797 Sec. A.6.1.
 *
 * Scale lengths above 1750 ft AGL (per MIL-HDBK-1797):
 *   L_u = L_v = L_w = 533 m (1750 ft)        -- isotropic above the
 *                                              boundary layer
 * Below 1750 ft the scale lengths shrink (boundary-layer mixing).
 *
 * RMS intensities (light/moderate/severe per MIL-HDBK-1797):
 *   light:    sigma_w ~ 0.5 m/s, sigma_u = sigma_v ~ 1.0 m/s
 *   moderate: sigma_w ~ 1.5 m/s, sigma_u = sigma_v ~ 3.0 m/s
 *   severe:   sigma_w ~ 4.5 m/s, sigma_u = sigma_v ~ 6.0 m/s
 *
 * At 12 km cruise, "light" intensities are realistic (high altitude,
 * smooth air outside convective cells).
 */

#include <cmath>
#include <random>

namespace sim::dynamics::disturbance {

/* ----------------------------- Params ----------------------------- */

struct DrydenTurbulenceParams {
  // ---- Scale lengths (m) ----
  double L_u_m = 533.0; // longitudinal scale length (~1750 ft above 533 m AGL)
  double L_v_m = 533.0; // lateral
  double L_w_m = 533.0; // vertical (= L_u/L_v above 1750 ft per MIL-HDBK-1797)

  // ---- RMS intensities (m/s) -- "light" defaults at high altitude ----
  double sigma_u_m_s = 1.0;
  double sigma_v_m_s = 1.0;
  double sigma_w_m_s = 0.5;

  // ---- RNG seed ----
  /// Seed for the std::mt19937 white-noise generator. Reproducible across
  /// runs when set explicitly; otherwise default-seeded (deterministic
  /// for tests).
  unsigned int seed = 42u;
};

/* ----------------------------- State ----------------------------- */

/**
 * Internal filter state. Encodes the discrete-time first-order (u_g)
 * and second-order (v_g, w_g) Dryden filters.
 *
 * For 1st-order filter (u_g): `u_state` holds the filter output.
 * For 2nd-order filters (v_g, w_g): two-state structure (numerator
 * has a zero, so we use the controllable canonical form with
 * intermediate state for the integrator).
 */
struct DrydenTurbulenceState {
  // u_g: 1st-order filter, single state = output.
  double u_g = 0.0;

  // v_g, w_g: 2nd-order filters with one zero. Realized as two cascaded
  // first-order filters, since (1 + a*s) / (1 + b*s)^2 = (1 + a*s) * (1/(1+b*s))^2
  // We use two state variables per axis: x1 (intermediate) + x2 (output).
  double v_x1 = 0.0;
  double v_x2 = 0.0; // v_g output
  double w_x1 = 0.0;
  double w_x2 = 0.0; // w_g output
};

/* ----------------------------- Result ----------------------------- */

struct DrydenTurbulenceResult {
  double u_g_m_s; // longitudinal gust velocity (added to body x airflow)
  double v_g_m_s; // lateral
  double w_g_m_s; // vertical
};

/* ----------------------------- RNG holder ----------------------------- */

/**
 * Lightweight RNG state holder. Kept separate from DrydenTurbulenceState
 * so the integrator's State doesn't need an std::mt19937 (which is
 * non-trivially-copyable in some configs).
 *
 * Distribution is per-instance (not `static thread_local`) so two
 * DrydenRng objects with different seeds produce independent streams.
 * std::normal_distribution holds internal state (Box-Muller carries
 * one extra sample between calls); a shared distribution would
 * non-deterministically interleave its consumers.
 */
class DrydenRng {
public:
  explicit DrydenRng(unsigned int seed = 42u) : gen_(seed), dist_(0.0, 1.0) {}

  /// Returns N(0, 1) sample.
  double normal01() { return dist_(gen_); }

  void reseed(unsigned int seed) {
    gen_.seed(seed);
    dist_.reset(); // clear any stored Box-Muller carry
  }

private:
  std::mt19937 gen_;
  std::normal_distribution<double> dist_;
};

/* ----------------------------- Driver ----------------------------- */

/**
 * Advance one tick of the Dryden filter.
 *
 * Discrete-time realization via backward-Euler (semi-implicit):
 *
 *   For H = K / (1 + tau*s):
 *     y(t+dt) = y(t) + dt/(tau + dt) * (K * u_input - y(t))
 *
 *   For H = K * (1 + a*s) / (1 + tau*s)^2:
 *     Realized as cascade of (K * (1 + a*s) / (1 + tau*s)) and 1/(1 + tau*s).
 *     The first factor is a lead-lag; we approximate with backward-Euler
 *     of the same form.
 *
 * The white-noise input must have zero mean and standard deviation
 * 1/sqrt(dt) (so its PSD equals 1). We sample N(0, 1) and divide by sqrt(dt).
 *
 * @param  s          state (mutated in place)
 * @param  p          parameters
 * @param  rng        RNG (mutated)
 * @param  V_m_s      true airspeed (m/s); turbulence frequency content
 *                    scales with airspeed via the Taylor frozen-field hypothesis
 * @param  dt_s       step (s)
 * @return body-frame gust velocity components
 */
inline DrydenTurbulenceResult stepDryden(DrydenTurbulenceState& s, const DrydenTurbulenceParams& p,
                                         DrydenRng& rng, double V_m_s, double dt_s) {
  if (V_m_s < 1.0 || dt_s <= 0.0) {
    return DrydenTurbulenceResult{s.u_g, s.v_x2, s.w_x2}; // freeze when V too low
  }

  const double sqrt_dt_inv = 1.0 / std::sqrt(dt_s);

  // ---- u_g: 1st-order Dryden ----
  // Discrete: y(t+dt) = (1-k)*y(t) + k*K*w_in,  k = dt/(tau+dt)
  // Steady-state sigma_y^2 = K^2*k / (dt*(2-k)) = K^2 / (2 tau + dt) -> K = sigma*sqrt(2L/V).
  {
    const double tau_u = p.L_u_m / V_m_s;
    const double K_u = p.sigma_u_m_s * std::sqrt(2.0 * p.L_u_m / V_m_s);
    const double w_in = rng.normal01() * sqrt_dt_inv; // N(0, 1/dt)
    const double k = dt_s / (tau_u + dt_s);
    s.u_g = s.u_g + k * (K_u * w_in - s.u_g);
  }

  // ---- v_g, w_g: 2nd-order Dryden, two cascaded 1st-order LP filters ----
  // The standard form has a lead-lag (1 + sqrt(3)*tau*s) in the numerator that
  // boosts high-frequency content. We approximate by a cascade of two
  // identical 1st-order LPFs, which gives sigma^2-correct steady-state RMS
  // but slightly under-emphasizes high frequencies. Acceptable for
  // demo-level disturbance modeling -- a rigorous Tustin-discretized
  // 2nd-order state-space realization would be more accurate.
  //
  // Calibration: stage 1 (white-noise driven, gain K) reaches steady-state
  // variance K^2/(2*tau) as dt->0 (same formula as the u_g 1st-order filter).
  // Stage 2 (unit-gain, same pole) filtering that AR(1) input halves the
  // variance, so the output variance is K^2/(4*tau). The gain is therefore
  // K = sigma * sqrt(4 L / V) (the 4, not 8, accounts for the second stage's
  // halving) so the output RMS -> sigma as dt->0; a sqrt(8 L / V) gain would
  // overstate the lateral/vertical gust RMS by sqrt(2).
  {
    const double tau_v = p.L_v_m / V_m_s;
    const double K_v = p.sigma_v_m_s * std::sqrt(4.0 * p.L_v_m / V_m_s);
    const double w_in = rng.normal01() * sqrt_dt_inv;
    const double k = dt_s / (tau_v + dt_s);
    s.v_x1 = s.v_x1 + k * (K_v * w_in - s.v_x1);
    s.v_x2 = s.v_x2 + k * (s.v_x1 - s.v_x2);
  }
  {
    const double tau_w = p.L_w_m / V_m_s;
    const double K_w = p.sigma_w_m_s * std::sqrt(4.0 * p.L_w_m / V_m_s);
    const double w_in = rng.normal01() * sqrt_dt_inv;
    const double k = dt_s / (tau_w + dt_s);
    s.w_x1 = s.w_x1 + k * (K_w * w_in - s.w_x1);
    s.w_x2 = s.w_x2 + k * (s.w_x1 - s.w_x2);
  }

  return DrydenTurbulenceResult{s.u_g, s.v_x2, s.w_x2};
}

} // namespace sim::dynamics::disturbance

#endif // APEX_SIM_DYNAMICS_DISTURBANCE_DRYDEN_TURBULENCE_HPP
