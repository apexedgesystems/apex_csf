/**
 * @file PolarAero_uTest.cpp
 * @brief Unit tests for PolarAero (parabolic-polar aerodynamics).
 *
 * Two channels:
 *   1. Closed-form algebra: q = 0.5*rho*V^2; CL = CL0 + CL_a*alpha;
 *      CD = CD0 + CL^2/(pi*e*AR). Verified as exact arithmetic.
 *   2. Hand-computed worked example: at sea level, V = 100 m/s, with
 *      jet-transport coefficients, what does the polar predict? Cross-checked
 *      against a hand calculation in the test.
 */

#include "src/sim/aerodynamics/inc/PolarAero.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::aerodynamics::dynamicPressure;
using sim::aerodynamics::evaluatePolar;
using sim::aerodynamics::PolarAeroParams;
using sim::aerodynamics::PolarAeroResult;
using sim::aerodynamics::trimAlphaForLevelFlight;

constexpr double kPi = 3.14159265358979323846;

/* ----------------------------- Dynamic pressure ----------------------------- */

/** @test q = 0.5*rho*V^2 at known sea-level density. */
TEST(PolarAeroTest, DynamicPressureAtSeaLevel) {
  // rho_sl = 1.225 kg/m^3, V = 100 m/s
  // q = 0.5 * 1.225 * 100^2 = 6125 Pa
  EXPECT_NEAR(dynamicPressure(1.225, 100.0), 6125.0, 1e-9);
}

/** @test At zero airspeed, dynamic pressure is zero (boundary). */
TEST(PolarAeroTest, DynamicPressureAtRestIsZero) { EXPECT_EQ(dynamicPressure(1.225, 0.0), 0.0); }

/* ----------------------------- CL formula ----------------------------- */

/** @test CL = CL0 + CL_a * alpha (exact linear lift curve). */
TEST(PolarAeroTest, CLLinearInAlpha) {
  PolarAeroParams p{};
  p.CL0 = 0.20;
  p.CL_a = 5.50;

  // alpha = 0  -> CL = CL0
  auto r0 = evaluatePolar(p, 0.0, 1.225, 100.0);
  EXPECT_NEAR(r0.CL, 0.20, 1e-12);

  // alpha = 0.05 rad (2.86 deg) -> CL = 0.20 + 5.5*0.05 = 0.475
  auto r1 = evaluatePolar(p, 0.05, 1.225, 100.0);
  EXPECT_NEAR(r1.CL, 0.475, 1e-12);
}

/* ----------------------------- CD formula ----------------------------- */

/** @test CD = CD0 + CL^2/(pi*e*AR), exact at alpha=0 (CL=CL0). */
TEST(PolarAeroTest, CDQuadraticInCL) {
  PolarAeroParams p{};
  p.CL0 = 0.20;
  p.CL_a = 5.50;
  p.CD0 = 0.020;
  p.e = 0.80;
  p.AR = 7.00;

  auto r = evaluatePolar(p, 0.0, 1.225, 100.0);
  // CL = 0.20, induced = 0.04 / (pi * 0.8 * 7.0) = 0.04 / 17.5929... ~ 0.0022736
  // CD = 0.020 + 0.0022736 ~ 0.0222736
  const double CL = 0.20;
  const double CD_expected = 0.020 + (CL * CL) / (kPi * 0.80 * 7.00);
  EXPECT_NEAR(r.CD, CD_expected, 1e-12);
}

/* ----------------------------- L, D forces ----------------------------- */

/** @test Lift and drag forces at a hand-checkable cruise condition.
 *
 *  Setup: rho=1.225, V=100 m/s, S=510 m^2, alpha=0
 *         q = 0.5*1.225*10000 = 6125 Pa
 *         CL = CL0 = 0.20         -> L = 6125 * 510 * 0.20 = 624 750 N
 *         CD = 0.020 + 0.04/(pi*0.8*7) ~ 0.0222736
 *              -> D = 6125 * 510 * 0.0222736 ~ 69 590 N
 */
TEST(PolarAeroTest, LiftAndDragHandComputedExample) {
  PolarAeroParams p{};
  p.CL0 = 0.20;
  p.CL_a = 5.50;
  p.CD0 = 0.020;
  p.e = 0.80;
  p.AR = 7.00;
  p.S_m2 = 510.0;

  auto r = evaluatePolar(p, 0.0, 1.225, 100.0);

  EXPECT_NEAR(r.q_Pa, 6125.0, 1e-9);
  EXPECT_NEAR(r.L_N, 6125.0 * 510.0 * 0.20, 1e-6);

  const double CD = 0.020 + (0.20 * 0.20) / (kPi * 0.80 * 7.00);
  EXPECT_NEAR(r.D_N, 6125.0 * 510.0 * CD, 1e-6);
}

/* ----------------------------- Trim alpha for level flight ----------------------------- */

/** @test trimAlphaForLevelFlight reproduces alpha such that L = m*g.
 *
 *  Setup: m=300_000 kg (large transport), g=9.80665, rho=0.4135 (~10 km),
 *         V=240 m/s, S=510. The required CL solves L=m*g; alpha follows.
 *         Then forward-evaluate and check that L equals weight.
 */
TEST(PolarAeroTest, TrimAlphaProducesLiftEqualToWeight) {
  PolarAeroParams p{};
  p.CL0 = 0.10;
  p.CL_a = 5.50;
  p.CD0 = 0.020;
  p.e = 0.80;
  p.AR = 7.00;
  p.S_m2 = 510.0;

  const double mass = 300000.0;
  const double g = 9.80665;
  const double rho = 0.4135;
  const double V = 240.0;

  const double alpha_trim = trimAlphaForLevelFlight(p, rho, V, mass, g);
  ASSERT_FALSE(std::isnan(alpha_trim));

  auto r = evaluatePolar(p, alpha_trim, rho, V);
  // Lift should equal weight to floating-point precision.
  EXPECT_NEAR(r.L_N, mass * g, 1e-3);
}

/** @test trim with zero density returns NaN (no air -> no lift possible). */
TEST(PolarAeroTest, TrimReturnsNaNWhenNoLiftIsPossible) {
  PolarAeroParams p{};
  const double alpha = trimAlphaForLevelFlight(p, /*rho*/ 0.0, /*V*/ 200.0,
                                               /*m*/ 1000.0, /*g*/ 9.80665);
  EXPECT_TRUE(std::isnan(alpha));
}
