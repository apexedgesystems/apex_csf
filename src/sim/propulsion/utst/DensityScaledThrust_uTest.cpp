/**
 * @file DensityScaledThrust_uTest.cpp
 * @brief Unit tests for the DensityScaledThrust empirical engine model.
 */

#include "src/sim/propulsion/inc/DensityScaledThrust.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::propulsion::DensityScaledThrustParams;
using sim::propulsion::evaluateThrust;

/* ----------------------------- Throttle linearity ----------------------------- */

/** @test At sea level, T scales linearly with throttle. */
TEST(DensityScaledThrustTest, LinearInThrottleAtSeaLevel) {
  DensityScaledThrustParams p{};
  p.T_max_sl_N = 1.0e6;
  p.rho_ref_kg_m3 = 1.225;
  p.n_density = 0.7;

  const double rho_sl = 1.225;

  // throttle = 0 -> T = 0
  EXPECT_NEAR(evaluateThrust(p, 0.0, rho_sl), 0.0, 1e-9);

  // throttle = 0.5 -> T = 0.5 * T_max
  EXPECT_NEAR(evaluateThrust(p, 0.5, rho_sl), 0.5e6, 1e-6);

  // throttle = 1.0 -> T = T_max
  EXPECT_NEAR(evaluateThrust(p, 1.0, rho_sl), 1.0e6, 1e-6);
}

/* ----------------------------- Density exponent ----------------------------- */

/** @test At rho=rho_ref, the density-scale factor is 1 regardless of n. */
TEST(DensityScaledThrustTest, ThrustEqualsTMaxAtReferenceDensity) {
  DensityScaledThrustParams p{};
  p.T_max_sl_N = 100.0;
  p.rho_ref_kg_m3 = 1.225;

  for (double n : {0.0, 0.5, 0.7, 1.0, 1.5}) {
    p.n_density = n;
    EXPECT_NEAR(evaluateThrust(p, 1.0, 1.225), 100.0, 1e-9) << "n=" << n;
  }
}

/** @test At half density with n=1, thrust halves. */
TEST(DensityScaledThrustTest, LinearDensityScalingWhenNIsOne) {
  DensityScaledThrustParams p{};
  p.T_max_sl_N = 1000.0;
  p.rho_ref_kg_m3 = 1.0;
  p.n_density = 1.0;

  EXPECT_NEAR(evaluateThrust(p, 1.0, 0.5), 500.0, 1e-9);
  EXPECT_NEAR(evaluateThrust(p, 1.0, 0.25), 250.0, 1e-9);
}

/** @test n=0.7 at rho=0.4135 (typical 10 km) gives the expected scale. */
TEST(DensityScaledThrustTest, CruiseAltitudeThrustScalesAsDensityRatioToTheNth) {
  DensityScaledThrustParams p{};
  p.T_max_sl_N = 1.0e6;
  p.rho_ref_kg_m3 = 1.225;
  p.n_density = 0.7;

  const double rho_alt = 0.4135; // ~10 km
  const double T = evaluateThrust(p, 1.0, rho_alt);
  const double expected = 1.0e6 * std::pow(rho_alt / 1.225, 0.7);
  EXPECT_NEAR(T, expected, 1e-3);

  // Sanity: at 10 km thrust is well below sea level (~40-45% for n=0.7).
  EXPECT_LT(T, 0.5e6);
  EXPECT_GT(T, 0.3e6);
}

/* ----------------------------- Boundary handling ----------------------------- */

/** @test Throttle below min is clamped (no negative thrust). */
TEST(DensityScaledThrustTest, ThrottleClampedToMin) {
  DensityScaledThrustParams p{};
  p.throttle_min = 0.05; // 5% idle
  p.T_max_sl_N = 1000.0;

  const double T = evaluateThrust(p, /*throttle*/ -0.5, /*rho*/ 1.225);
  // Clamped to throttle_min = 0.05 -> T = 0.05 * 1000 = 50 N
  EXPECT_NEAR(T, 50.0, 1e-9);
}

/** @test Throttle above max is clamped (no overdrive). */
TEST(DensityScaledThrustTest, ThrottleClampedToMax) {
  DensityScaledThrustParams p{};
  p.throttle_max = 1.0;
  p.T_max_sl_N = 1000.0;

  const double T = evaluateThrust(p, /*throttle*/ 1.5, /*rho*/ 1.225);
  EXPECT_NEAR(T, 1000.0, 1e-9);
}

/** @test Vacuum (rho=0) gives zero thrust -- air-breathing engines need air. */
TEST(DensityScaledThrustTest, VacuumGivesZeroThrust) {
  DensityScaledThrustParams p{};
  p.T_max_sl_N = 1.0e6;
  EXPECT_EQ(evaluateThrust(p, 1.0, 0.0), 0.0);
}
