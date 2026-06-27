#ifndef APEX_SIM_AERODYNAMICS_STABILITY_DERIVATIVE_AERO_HPP
#define APEX_SIM_AERODYNAMICS_STABILITY_DERIVATIVE_AERO_HPP
/**
 * @file StabilityDerivativeAero.hpp
 * @brief Linearized stability-derivative aerodynamics.
 *
 * Computes body-frame forces and moments on an aircraft using the standard
 * small-perturbation stability-derivative form:
 *
 *   CL = CL_0 + CL_a*alpha + CL_q*(q*c/2V) + CL_de*de
 *   CD = CD_0 + CL^2/(pi*e*AR)            (parabolic polar; alpha-coupled)
 *   CY = CY_b*beta + CY_p*(p*b/2V) + CY_r*(r*b/2V) + CY_da*da + CY_dr*dr
 *
 *   Cl = Cl_b*beta + Cl_p*(p*b/2V) + Cl_r*(r*b/2V) + Cl_da*da + Cl_dr*dr  (roll)
 *   Cm = Cm_0 + Cm_a*alpha + Cm_q*(q*c/2V) + Cm_de*de                     (pitch)
 *   Cn = Cn_b*beta + Cn_p*(p*b/2V) + Cn_r*(r*b/2V) + Cn_da*da + Cn_dr*dr  (yaw)
 *
 *   Forces (wind axes):  L = q*S*CL,  D = q*S*CD,  Y = q*S*CY
 *   Moments (body axes): l = q*S*b*Cl,  m = q*S*c*Cm,  n = q*S*b*Cn
 *
 *   q = 0.5*rho*V^2  (dynamic pressure)
 *
 * Wind->body transform:
 *
 *      [ x_b ]   [ cosa cosb   -cosa sinb   -sina ]   [ x_w ]
 *      [ y_b ] = [   sinb          cosb        0   ] . [ y_w ]
 *      [ z_b ]   [ sina cosb   -sina sinb    cosa ]   [ z_w ]
 *
 *   F_wind = (-D, Y, -L)            (drag -x_w, side +y_w, lift -z_w)
 *   F_body = R_bw . F_wind
 *
 * Sign conventions:
 *   - Positive alpha: nose up relative to V, w_body > 0 (with u_body > 0)
 *   - Positive beta: V vector to the right of body x-z plane, v_body > 0
 *   - Positive p: right-wing-down roll
 *   - Positive q: nose-up pitch
 *   - Positive r: nose-right yaw
 *   - Positive elevator de: trailing edge down (nose-down pitch moment)
 *   - Positive aileron da: right-wing-down (positive roll moment)
 *   - Positive rudder dr: trailing edge left (positive yaw moment)
 *
 * The alpha-dot derivative (CL_adot, Cm_adot) is intentionally omitted:
 * tracking alpha-dot as integrated state requires either a delayed-feedback
 * approximation or an extra DAE constraint. In the cruise regime the
 * contribution is small and the dynamics are correct without it.
 *
 * The parameterized model carries illustrative jet-transport defaults; a
 * specific aircraft supplies its own derivatives and reference geometry.
 */

#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp" // sim::dynamics::rigid_body::Vec3

#include <cmath>

namespace sim::aerodynamics {

/* ---------------------------- Control inputs ---------------------------- */

/** Trailing-edge-deflection control surface inputs (radians). */
struct ControlInputs {
  double elevator_rad = 0.0; // de - positive trailing-edge down (nose-down pitch)
  double aileron_rad = 0.0;  // da - positive right-wing-down roll
  double rudder_rad = 0.0;   // dr - positive trailing-edge left (positive yaw)
};

/* ---------------------------- Parameters ---------------------------- */

/**
 * Stability derivatives + reference geometry.
 *
 * The defaults are illustrative jet-transport values; a specific aircraft's
 * cruise derivatives are supplied by the application that instantiates it.
 *
 * Units:
 *   - Force/moment derivatives w.r.t. angles:        per radian
 *   - Force/moment derivatives w.r.t. nondim. rates: per (rate*length/2V)
 *     i.e. qhat = q*c/(2V), phat = p*b/(2V), rhat = r*b/(2V)
 */
struct StabilityDerivativeAeroParams {
  // ---- Reference geometry (large-transport class) ----
  double S_m2 = 510.0; // wing reference area
  double c_m = 8.32;   // mean aerodynamic chord
  double b_m = 59.64;  // wingspan

  // ---- Longitudinal - lift CL ----
  double CL_0 = 0.20;
  double CL_a = 5.50;  // /rad
  double CL_q = 7.95;  // /qhat
  double CL_de = 0.34; // /rad elevator

  // ---- Longitudinal - drag CD (parabolic polar) ----
  double CD_0 = 0.020;
  double e_oswald = 0.80;
  double AR = 7.0; // = b^2 / S; caller may keep it consistent

  // ---- Longitudinal - pitch Cm ----
  double Cm_0 = 0.0;
  double Cm_a = -1.45;  // longitudinal static stability (negative => stable)
  double Cm_q = -25.0;  // pitch damping (heavy negative => short period damps)
  double Cm_de = -1.20; // elevator pitching effectiveness

  // ---- Side force CY ----
  double CY_b = -0.90; // /rad beta
  double CY_p = 0.0;   // /phat
  double CY_r = 0.50;  // /rhat
  double CY_da = 0.0;
  double CY_dr = 0.21; // /rad rudder

  // ---- Roll moment Cl (lowercase) ----
  double Cl_b = -0.16;  // dihedral effect (negative => stable: right slip rolls left)
  double Cl_p = -0.45;  // roll damping
  double Cl_r = 0.13;   // adverse yaw coupling
  double Cl_da = 0.18;  // aileron rolling effectiveness
  double Cl_dr = 0.018; // rudder cross-coupling

  // ---- Yaw moment Cn ----
  double Cn_b = 0.16; // weathervane stability (positive => stable)
  double Cn_p = -0.026;
  double Cn_r = -0.27;  // yaw damping
  double Cn_da = 0.023; // adverse yaw from aileron
  double Cn_dr = -0.10; // rudder yawing effectiveness
};

/* ---------------------------- Result ---------------------------- */

/** Output of a stability-derivative evaluation. */
struct StabilityDerivativeAeroResult {
  // Flight condition derived
  double V_m_s = 0.0;
  double alpha_rad = 0.0;
  double beta_rad = 0.0;
  double q_Pa = 0.0;

  // Aero coefficients (dimensionless)
  double CL = 0.0;
  double CD = 0.0;
  double CY = 0.0;
  double Cl_roll = 0.0;
  double Cm_pitch = 0.0;
  double Cn_yaw = 0.0;

  // Wind-axis forces (N)
  double L_N = 0.0;
  double D_N = 0.0;
  double Y_N = 0.0;

  // Body-frame forces (N) - feeds the 6-DOF net force
  sim::dynamics::rigid_body::Vec3 force_body{};

  // Body-frame moments (N*m) - feeds the 6-DOF net moment
  sim::dynamics::rigid_body::Vec3 moment_body{};
};

/* ---------------------------- Internals ---------------------------- */

/** Dynamic pressure  q = 0.5*rho*V^2  (Pa). */
inline double dynamicPressureQ(double rho_kg_m3, double V_m_s) {
  return 0.5 * rho_kg_m3 * V_m_s * V_m_s;
}

/**
 * Convert wind-axis aerodynamic forces (drag, side, lift) to body-axis.
 *
 * Wind-axis convention: F_wind = (-D, Y, -L), i.e. drag along -x_w, side
 * along +y_w, lift along -z_w.
 *
 * @param  L_N        lift in N (positive up in lift plane)
 * @param  D_N        drag in N (positive aft along V)
 * @param  Y_N        side force in N (positive right in side-slip plane)
 * @param  alpha_rad  angle of attack
 * @param  beta_rad   sideslip angle
 * @return body-frame force vector
 */
inline sim::dynamics::rigid_body::Vec3 windToBodyForces(double L_N, double D_N, double Y_N,
                                                        double alpha_rad, double beta_rad) {
  const double ca = std::cos(alpha_rad);
  const double sa = std::sin(alpha_rad);
  const double cb = std::cos(beta_rad);
  const double sb = std::sin(beta_rad);

  const double Fx_w = -D_N;
  const double Fy_w = Y_N;
  const double Fz_w = -L_N;

  // R_bw . F_w
  return sim::dynamics::rigid_body::Vec3{ca * cb * Fx_w - ca * sb * Fy_w - sa * Fz_w,
                                         sb * Fx_w + cb * Fy_w,
                                         sa * cb * Fx_w - sa * sb * Fy_w + ca * Fz_w};
}

/* ---------------------------- Evaluator ---------------------------- */

/**
 * Evaluate stability-derivative aerodynamics for the current flight state.
 *
 * @param  p         aero parameters (geometry + derivatives)
 * @param  v_body    body-frame velocity (u, v, w) in m/s
 * @param  w_body    body-frame angular rates (p, q, r) in rad/s
 * @param  delta     control deflections in radians
 * @param  rho_kg_m3 freestream density
 * @return forces + moments in body frame, plus diagnostic intermediates
 *
 * Returns zero forces+moments if airspeed V < 1 m/s (avoids divide-by-zero
 * on the nondimensional rates; the aircraft is effectively at rest
 * aerodynamically).
 */
inline StabilityDerivativeAeroResult evaluateStabilityDerivative(
    const StabilityDerivativeAeroParams& p, const sim::dynamics::rigid_body::Vec3& v_body,
    const sim::dynamics::rigid_body::Vec3& w_body, const ControlInputs& delta, double rho_kg_m3) {
  StabilityDerivativeAeroResult r;

  // True airspeed and aero angles.
  const double u = v_body.x;
  const double v = v_body.y;
  const double w = v_body.z;
  const double V = std::sqrt(u * u + v * v + w * w);

  if (V < 1.0) {
    // Below 1 m/s the linearization breaks down; bail out cleanly.
    r.V_m_s = V;
    return r;
  }

  const double alpha = std::atan2(w, u);
  const double beta = std::asin(v / V);

  // Nondimensional rates.
  const double half_chord_over_V = p.c_m / (2.0 * V);
  const double half_span_over_V = p.b_m / (2.0 * V);
  const double pHat = w_body.x * half_span_over_V;
  const double qHat = w_body.y * half_chord_over_V;
  const double rHat = w_body.z * half_span_over_V;

  // Coefficients.
  const double CL = p.CL_0 + p.CL_a * alpha + p.CL_q * qHat + p.CL_de * delta.elevator_rad;
  const double CD = p.CD_0 + (CL * CL) / (M_PI * p.e_oswald * p.AR);
  const double CY = p.CY_b * beta + p.CY_p * pHat + p.CY_r * rHat + p.CY_da * delta.aileron_rad +
                    p.CY_dr * delta.rudder_rad;

  const double Cl_roll = p.Cl_b * beta + p.Cl_p * pHat + p.Cl_r * rHat +
                         p.Cl_da * delta.aileron_rad + p.Cl_dr * delta.rudder_rad;
  const double Cm_pitch = p.Cm_0 + p.Cm_a * alpha + p.Cm_q * qHat + p.Cm_de * delta.elevator_rad;
  const double Cn_yaw = p.Cn_b * beta + p.Cn_p * pHat + p.Cn_r * rHat +
                        p.Cn_da * delta.aileron_rad + p.Cn_dr * delta.rudder_rad;

  // Forces (wind axes) and moments (body axes).
  const double q_bar = dynamicPressureQ(rho_kg_m3, V);
  const double qS = q_bar * p.S_m2;

  const double L_N = qS * CL;
  const double D_N = qS * CD;
  const double Y_N = qS * CY;

  const double L_roll_Nm = qS * p.b_m * Cl_roll;
  const double M_pitch_Nm = qS * p.c_m * Cm_pitch;
  const double N_yaw_Nm = qS * p.b_m * Cn_yaw;

  // Pack result.
  r.V_m_s = V;
  r.alpha_rad = alpha;
  r.beta_rad = beta;
  r.q_Pa = q_bar;
  r.CL = CL;
  r.CD = CD;
  r.CY = CY;
  r.Cl_roll = Cl_roll;
  r.Cm_pitch = Cm_pitch;
  r.Cn_yaw = Cn_yaw;
  r.L_N = L_N;
  r.D_N = D_N;
  r.Y_N = Y_N;
  r.force_body = windToBodyForces(L_N, D_N, Y_N, alpha, beta);
  r.moment_body = sim::dynamics::rigid_body::Vec3{L_roll_Nm, M_pitch_Nm, N_yaw_Nm};
  return r;
}

} // namespace sim::aerodynamics

#endif // APEX_SIM_AERODYNAMICS_STABILITY_DERIVATIVE_AERO_HPP
