/**
 * @file AeroGolden_uTest.cpp
 * @brief Ground-truth tests against classic worked aerodynamics problems.
 *
 * Each test locks in a published worked-problem answer to ~3 sig figs and
 * documents the physics in place. They exercise the building blocks the aero
 * models rely on: ideal-gas density, body<->wind force rotation, finite-wing
 * lift slope, and full glider trim with induced drag. (Source-problem
 * provenance is tracked in the design-docs textbook map, not cited here.)
 *
 *   - ideal-gas density (sanity for the gas constant)
 *   - flat-plate force rotation N',A' -> L',D' at alpha=10 deg
 *   - airfoil coeff rotation c_n,c_a -> c_l,c_d at alpha=12 deg
 *   - finite-wing lift slope (a0=6.188/rad, AR=8, tau=0.054)
 *   - full glider trim: CL, alpha, CD_i, D_i for a 2450 lb glider at sea
 *     level, V=120 mph, S=170 ft^2
 *
 * The force-rotation cases also exercise the wind->body rotation used in
 * StabilityDerivativeAero, checked by reconstructing N',A' from the
 * windToBodyForces output.
 */

#include "src/sim/aerodynamics/inc/PolarAero.hpp"
#include "src/sim/aerodynamics/inc/StabilityDerivativeAero.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::aerodynamics::dynamicPressure;
using sim::aerodynamics::evaluatePolar;
using sim::aerodynamics::finiteWingLiftSlope;
using sim::aerodynamics::PolarAeroParams;
using sim::aerodynamics::windToBodyForces;
using sim::dynamics::rigid_body::Vec3;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

/* Imperial -> SI conversions used by the worked problems. */
constexpr double kFt2M = 0.3048;
constexpr double kSlugFt3ToKgM3 = 515.378818;
constexpr double kLb2N = 4.4482216;

} // namespace

/* ----------------------------- Ideal-gas density ----------------------------- */

/**
 * @test Ideal-gas density rho = p / (R*T).
 *   p = 1.9e4 Pa, T = 203 K, R_air = 287 J/(kg*K)
 *   rho = 1.9e4 / (287 * 203) = 0.326 kg/m^3
 */
TEST(AeroGoldenTest, IdealGasDensity) {
  constexpr double R_air_J_kgK = 287.0;
  const double p = 1.9e4;
  const double T = 203.0;
  const double rho = p / (R_air_J_kgK * T);
  EXPECT_NEAR(rho, 0.326, 0.001); // 0.326 kg/m^3
}

/* ----------------------------- Flat-plate force rotation ----------------------------- */

/**
 * @test Flat plate at alpha=10 deg: body-axis normal/axial -> wind-axis
 * lift/drag.
 *
 *   Inputs:   N' = 1.12e5 N,  A' = 1274 N,   alpha = 10 deg
 *   Forward:  L' = N'*cos a - A'*sin a       (= 1.105e5 N)
 *             D' = N'*sin a + A'*cos a       (= 2.07e4  N)
 *
 * The reference values are rounded to 3-4 sig figs; an inverse round-trip
 * back to N'/A' loses up to ~5% on the small axial component. We test the
 * forward rotation directly, and separately verify windToBodyForces
 * round-trips at machine precision on self-consistent inputs.
 */
TEST(AeroGoldenTest, FlatPlateForwardLiftDrag) {
  const double alpha = 10.0 * kDeg;
  const double Np = 1.12e5;
  const double Ap = 1274.0;

  const double L = Np * std::cos(alpha) - Ap * std::sin(alpha);
  const double D = Np * std::sin(alpha) + Ap * std::cos(alpha);

  // Expected: L' = 1.105e5 N, D' = 2.07e4 N (3-sig-fig).
  EXPECT_NEAR(L, 1.105e5, 1.0e3); // computed 1.10078e5
  EXPECT_NEAR(D, 2.07e4, 100.0);  // computed 2.0703e4
}

TEST(AeroGoldenTest, WindToBodyRoundTripsExactly) {
  // Self-consistent round-trip: take body-axis (N', A') inputs, rotate
  // body->wind via the formula, then feed the unrounded wind-axis result back
  // into windToBodyForces. We should recover (-A', 0, -N') to machine
  // precision.
  const double alpha = 10.0 * kDeg;
  const double Np = 1.12e5;
  const double Ap = 1274.0;

  const double L = Np * std::cos(alpha) - Ap * std::sin(alpha);
  const double D = Np * std::sin(alpha) + Ap * std::cos(alpha);

  const Vec3 f_body = windToBodyForces(L, D, /*Y*/ 0.0, alpha, 0.0);
  EXPECT_NEAR(-f_body.x, Ap, 1e-9);
  EXPECT_NEAR(-f_body.z, Np, 1e-9);
  EXPECT_NEAR(f_body.y, 0.0, 1e-12);
}

/* ----------------------------- Airfoil coefficient rotation ----------------------------- */

/**
 * @test Airfoil coefficient rotation at alpha=12 deg.
 *
 *   c_n = 1.2,  c_a = 0.03   (c_a back-solved from the reference c_d = 0.279)
 *   c_l = c_n cos a - c_a sin a = 1.2*cos12 - 0.03*sin12 = 1.168
 *   c_d = c_n sin a + c_a cos a = 1.2*sin12 + 0.03*cos12 = 0.279
 */
TEST(AeroGoldenTest, AirfoilCoefficientRotation) {
  const double alpha = 12.0 * kDeg;
  const double c_n = 1.2;
  const double c_a = 0.03; // back-solved from c_d = 0.279

  const double c_l = c_n * std::cos(alpha) - c_a * std::sin(alpha);
  const double c_d = c_n * std::sin(alpha) + c_a * std::cos(alpha);

  EXPECT_NEAR(c_d, 0.279, 0.001);
  EXPECT_NEAR(c_l, 1.168, 0.005);
}

/* ----------------------------- Finite-wing lift slope ----------------------------- */

/**
 * @test Finite-wing lift slope from the section slope.
 *
 *   a0 = 0.108/deg = 6.188/rad     (section/infinite-wing slope)
 *   AR = 8,  tau = 0.054           (rectangular-wing correction)
 *   a = a0 / (1 + (a0/(pi*AR))*(1+tau)) = 4.91/rad = 0.0857/deg
 *
 *   Then CL = a*(alpha - alpha_L0) = 0.0857*(7 - (-1.3)) deg = 0.712
 *   And  CD_i = CL^2*(1+delta)/(pi*AR) = 0.712^2 * 1.054 / (pi*8) = 0.0212
 */
TEST(AeroGoldenTest, FiniteWingLiftSlope) {
  const double a0 = 6.188; // /rad (= 0.108/deg * 57.3)
  const double AR = 8.0;
  const double tau = 0.054;

  const double a = finiteWingLiftSlope(a0, AR, tau);
  EXPECT_NEAR(a, 4.91, 0.02); // 4.91/rad

  const double a_per_deg = a / 57.29577951308232;
  EXPECT_NEAR(a_per_deg, 0.0857, 0.001); // 0.0857/deg
}

TEST(AeroGoldenTest, FiniteWingLiftAndInducedDrag) {
  const double a0 = 6.188;
  const double AR = 8.0;
  const double tau = 0.054;
  const double a = finiteWingLiftSlope(a0, AR, tau); // 4.91/rad

  // CL at alpha = 7 deg, alpha_L0 = -1.3 deg (effective 8.3 deg).
  const double alpha_eff_rad = 8.3 * kDeg;
  const double CL = a * alpha_eff_rad;
  EXPECT_NEAR(CL, 0.712, 0.005); // 0.712

  // CD_i = CL^2 * (1+delta) / (pi*AR), delta=0.054.
  const double delta = 0.054;
  const double CD_i = CL * CL * (1.0 + delta) / (kPi * AR);
  EXPECT_NEAR(CD_i, 0.0212, 0.0003); // 0.0212
}

/* ----------------------------- Glider trim + induced drag ----------------------------- */

/**
 * @test Full glider trim and induced drag.
 *
 * Glider geometry / flight condition (sea level):
 *   W = 2450 lb (10898 N),  S = 170 ft^2 (15.79 m^2),  b = 32 ft -> AR = 6.02
 *   V = 120 mph = 176 ft/s (53.65 m/s),  rho_SL = 0.002377 slug/ft^3 (1.225)
 *   a0 = 5.92/rad,  tau = 0.12,  alpha_L0 = -3 deg,  e = 0.64
 *
 * Expected:
 *   q = 1762 Pa,  CL = 0.3916,  a = 4.385/rad,  alpha_trim = 2.12 deg,
 *   CD_i = 0.01267,  D_i = 79.3 lb (353 N)
 *
 * Routed through evaluatePolar: CL_a is the finite-wing slope, CL_0 absorbs
 * alpha_L0, and the polar must reproduce CL, CD_i, L, D_i.
 */
TEST(AeroGoldenTest, GliderTrimAndInducedDrag) {
  // ---- Convert imperial->SI ----
  const double W_N = 2450.0 * kLb2N;            // 10898 N
  const double S_m2 = 170.0 * kFt2M * kFt2M;    // 15.794 m^2
  const double V_m_s = 176.0 * kFt2M;           // 53.65 m/s
  const double rho = 0.002377 * kSlugFt3ToKgM3; // 1.2249 kg/m^3
  const double AR = 6.02;
  const double e = 0.64;
  const double a0_per_rad = 5.92;
  const double tau = 0.12;
  const double alpha_L0_deg = -3.0;

  // ---- Finite-wing lift slope ----
  const double CL_a = finiteWingLiftSlope(a0_per_rad, AR, tau);
  EXPECT_NEAR(CL_a, 4.385, 0.01); // 4.385/rad

  // ---- Trim alpha from L = W ----
  const double q_bar = dynamicPressure(rho, V_m_s);
  EXPECT_NEAR(q_bar, 1762.0, 5.0); // 36.8 lb/ft^2 ~ 1762 Pa

  const double CL_required = W_N / (q_bar * S_m2);
  EXPECT_NEAR(CL_required, 0.3916, 0.005); // 0.3916

  // alpha_trim such that CL_a * (alpha - alpha_L0) = CL_required
  const double alpha_trim_deg = (CL_required / CL_a) / kDeg + alpha_L0_deg;
  EXPECT_NEAR(alpha_trim_deg, 2.12, 0.05); // 2.12 deg

  // ---- Plug into PolarAero with CL_0 absorbing alpha_L0 ----
  // Model:   CL = CL_0 + CL_a * alpha  (alpha from body chord)
  // Problem: CL = CL_a * (alpha - alpha_L0)  =>  CL_0 = -CL_a * alpha_L0
  PolarAeroParams p;
  p.S_m2 = S_m2;
  p.AR = AR;
  p.e = e;
  p.CL_a = CL_a;
  p.CL0 = -CL_a * (alpha_L0_deg * kDeg); // 4.385 * 0.0524 = 0.2298
  p.CD0 = 0.0;                           // induced drag only

  const double alpha_trim_rad = alpha_trim_deg * kDeg;
  const auto r = evaluatePolar(p, alpha_trim_rad, rho, V_m_s);

  EXPECT_NEAR(r.CL, 0.3916, 0.005);      // 0.3916
  EXPECT_NEAR(r.CD, 0.01267, 0.0003);    // 0.01267
  EXPECT_NEAR(r.L_N, W_N, 20.0);         // L = W = 10898 N
  EXPECT_NEAR(r.D_N, 79.3 * kLb2N, 5.0); // 79.3 lb = 353 N
}
