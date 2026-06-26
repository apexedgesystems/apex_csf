#ifndef APEX_SIM_AERODYNAMICS_POLAR_AERO_HPP
#define APEX_SIM_AERODYNAMICS_POLAR_AERO_HPP
/**
 * @file PolarAero.hpp
 * @brief Parabolic-polar airplane aerodynamics (lift + drag).
 *
 * Computes lift and drag on an aircraft from total angle of attack alpha,
 * dynamic pressure q, wing area S, and a small set of empirical
 * coefficients:
 *
 *   CL(alpha) = CL0 + CL_a * alpha                   (linear lift curve)
 *   CD(CL)    = CD0 + CL^2 / (pi * e * AR)           (parabolic drag polar)
 *
 *   q = 0.5 * rho * V^2                              (dynamic pressure)
 *   L = q * S * CL                                   (lift, perpendicular to V)
 *   D = q * S * CD                                   (drag, opposite V)
 *
 * Variables:
 *   CL0    - lift coefficient at alpha=0 (camber + flap setting)
 *   CL_a   - per-radian lift slope (finite-wing; see finiteWingLiftSlope)
 *   CD0    - parasite drag coefficient (skin friction + form drag)
 *   e      - Oswald efficiency (~0.7-0.85 for typical airplanes)
 *   AR     - aspect ratio = b^2 / S
 *
 * The parameterized model carries illustrative jet-transport defaults; a
 * specific aircraft supplies its own coefficients and reference area.
 */

namespace sim::aerodynamics {

/** Configurable polar coefficients. All dimensionless except as noted. */
struct PolarAeroParams {
  double CL0 = 0.20;   // lift at alpha=0 (typical jet transport ~0.1-0.3)
  double CL_a = 5.50;  // dCL/dalpha per rad (2*pi for an ideal flat plate)
  double CD0 = 0.020;  // parasite drag (jet-transport cruise ~0.018-0.025)
  double e = 0.80;     // Oswald efficiency (typical 0.7-0.85)
  double AR = 7.00;    // aspect ratio (jet transport ~7, glider ~25)
  double S_m2 = 510.0; // wing reference area, m^2 (large-transport class)
};

/** Result of a polar evaluation. Forces in Newtons. */
struct PolarAeroResult {
  double CL = 0.0;   // dimensionless lift coeff
  double CD = 0.0;   // dimensionless drag coeff
  double L_N = 0.0;  // lift, N (perpendicular to V, in lift plane)
  double D_N = 0.0;  // drag, N (opposite V)
  double q_Pa = 0.0; // dynamic pressure, Pa
};

/** Dynamic pressure  q = 0.5 * rho * V^2  (Pa). */
inline double dynamicPressure(double rho_kg_m3, double V_m_s) {
  return 0.5 * rho_kg_m3 * V_m_s * V_m_s;
}

/**
 * Finite-wing lift-curve slope from the section (infinite-wing) slope:
 *
 *   a = a0 / ( 1 + (a0 / (pi*AR)) * (1 + tau) )
 *
 * where a0 is the section lift slope per rad and tau is the spanwise-loading
 * correction (0 for elliptic loading). This is algebraically equivalent to
 * the Oswald form `CL_a = a0 / (1 + a0/(pi*e*AR))` with `e = 1/(1+tau)`.
 * Worked check: a0 = 6.188/rad, AR = 8, tau = 0.054 gives 4.91/rad.
 */
inline double finiteWingLiftSlope(double a0_per_rad, double AR, double tau) {
  constexpr double kPi = 3.14159265358979323846;
  return a0_per_rad / (1.0 + (a0_per_rad / (kPi * AR)) * (1.0 + tau));
}

/**
 * Evaluate the polar at a flight condition.
 *
 * @param  p             coefficients + reference area
 * @param  alpha_rad     angle of attack (rad)
 * @param  rho_kg_m3     freestream density (kg/m^3)
 * @param  V_m_s         true airspeed (m/s)
 * @return CL, CD, L, D, q
 *
 *   CL = CL0 + CL_a * alpha
 *   CD = CD0 + CL^2 / (pi * e * AR)
 */
inline PolarAeroResult evaluatePolar(const PolarAeroParams& p, double alpha_rad, double rho_kg_m3,
                                     double V_m_s) {
  constexpr double kPi = 3.14159265358979323846;
  PolarAeroResult r;
  r.CL = p.CL0 + p.CL_a * alpha_rad;
  r.CD = p.CD0 + (r.CL * r.CL) / (kPi * p.e * p.AR);
  r.q_Pa = dynamicPressure(rho_kg_m3, V_m_s);
  r.L_N = r.q_Pa * p.S_m2 * r.CL;
  r.D_N = r.q_Pa * p.S_m2 * r.CD;
  return r;
}

/**
 * Trim alpha for level flight: lift = weight.
 *   L = q * S * CL = m * g  ->  CL_required = m*g / (q*S)
 *   alpha_trim = (CL_required - CL0) / CL_a
 *
 * Returns NaN if q*S = 0 (zero airspeed or zero density).
 */
inline double trimAlphaForLevelFlight(const PolarAeroParams& p, double rho_kg_m3, double V_m_s,
                                      double mass_kg, double g_m_s2) {
  const double q = dynamicPressure(rho_kg_m3, V_m_s);
  const double qS = q * p.S_m2;
  if (qS <= 0.0) {
    return 0.0 / 0.0; // NaN
  }
  const double CL_req = (mass_kg * g_m_s2) / qS;
  return (CL_req - p.CL0) / p.CL_a;
}

} // namespace sim::aerodynamics

#endif // APEX_SIM_AERODYNAMICS_POLAR_AERO_HPP
