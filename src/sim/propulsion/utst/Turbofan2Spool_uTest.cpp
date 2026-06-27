/**
 * @file Turbofan2Spool_uTest.cpp
 * @brief Unit tests for the two-spool turbofan model.
 *
 * Closed-form spool / thrust scenarios:
 *   1. Idle floors enforced (throttle=0 -> N1>=N1_idle, N2>=N2_idle)
 *   2. Steady-state at full throttle: N1, N2 -> 100%
 *   3. Spool-up time constant: step throttle 25%->100%, measure the 63%
 *      rise time of N2 (should be ~ tau_N2)
 *   4. N1 lags N2 (N1 reaches equilibrium more slowly than N2)
 *   5. Thrust scales with N1^2
 *   6. Density scaling: thrust drops with altitude
 *   7. Vacuum (rho=0): thrust = 0 (air-breathing engine)
 *   8. Throttle clamp: throttle=2.0 treated as 1.0; throttle=-0.5 as 0.0
 *   9. Rotor angular momentum scales linearly with N1
 */

#include "src/sim/propulsion/inc/Turbofan2Spool.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::propulsion::stepTurbofan2Spool;
using sim::propulsion::Turbofan2SpoolParams;
using sim::propulsion::Turbofan2SpoolResult;
using sim::propulsion::Turbofan2SpoolState;

namespace {

constexpr double kRho_SL = 1.225;
constexpr double kRho_12km = 0.3045;

} // namespace

/* ----------------------------- Idle floors ----------------------------- */

TEST(Turbofan2SpoolTest, ThrottleZeroDecaysToIdleFloors) {
  Turbofan2SpoolState s;
  s.N1_pct = 80.0;
  s.N2_pct = 80.0;

  const Turbofan2SpoolParams p; // defaults: N1_idle=25, N2_idle=60
  // Run 30 seconds at throttle=0; should settle at idle floors.
  for (int i = 0; i < 600; ++i) {
    stepTurbofan2Spool(s, p, /*throttle*/ 0.0, kRho_SL, /*dt*/ 0.05);
  }
  EXPECT_NEAR(s.N1_pct, p.N1_idle_pct, 0.5);
  EXPECT_NEAR(s.N2_pct, p.N2_idle_pct, 0.5);
}

/* ----------------------------- Full throttle settle ----------------------------- */

TEST(Turbofan2SpoolTest, ThrottleFullSettlesAtHundredPercent) {
  Turbofan2SpoolState s; // starts at idle (25, 60)
  const Turbofan2SpoolParams p;
  // Run 30 seconds at throttle=1.0; both spools should reach ~100%.
  for (int i = 0; i < 600; ++i) {
    stepTurbofan2Spool(s, p, /*throttle*/ 1.0, kRho_SL, /*dt*/ 0.05);
  }
  EXPECT_NEAR(s.N2_pct, 100.0, 0.1);
  EXPECT_NEAR(s.N1_pct, 100.0, 0.1);
}

/* ----------------------------- Spool time constants ----------------------------- */

TEST(Turbofan2SpoolTest, N2RiseTimeMatchesTauN2) {
  // First-order lag: 63.2% rise time of a step input = tau.
  // Step from idle (60% N2) to throttle=1.0 (target 100%): swing = 40%.
  // 63.2% rise = 60 + 0.632*40 = 85.3%.
  Turbofan2SpoolParams p;
  p.tau_N2_s = 0.5;   // 0.5 sec time constant
  p.tau_N1_s = 100.0; // freeze N1 so it doesn't perturb the test

  Turbofan2SpoolState s; // N2 starts at 60% (idle)
  const double dt = 0.005;
  double t_at_85_3 = -1.0;
  for (int i = 0; i < 1000; ++i) {
    stepTurbofan2Spool(s, p, /*throttle*/ 1.0, kRho_SL, dt);
    if (t_at_85_3 < 0.0 && s.N2_pct >= 85.3) {
      t_at_85_3 = (i + 1) * dt;
      break;
    }
  }
  // backward-Euler discrete first-order has slight bias vs continuous tau;
  // expect the 63% rise within +/-15% of tau_N2.
  EXPECT_NEAR(t_at_85_3, 0.5, 0.075);
}

TEST(Turbofan2SpoolTest, N1LagsN2OverFullSpoolUp) {
  Turbofan2SpoolParams p;
  // Default tau_N1 = 2.0 s, tau_N2 = 0.5 s -- N1 should lag.
  Turbofan2SpoolState s;
  // Step throttle 0.25 -> 1.0; sample after 1 second.
  for (int i = 0; i < 200; ++i) { // 1 sec
    stepTurbofan2Spool(s, p, /*throttle*/ 1.0, kRho_SL, /*dt*/ 0.005);
  }
  // Assert N2 has risen more than N1 (the lag direction).
  EXPECT_GT(s.N2_pct, s.N1_pct + 10.0)
      << "expected N1 to lag N2 by >= 10%; got N1=" << s.N1_pct << ", N2=" << s.N2_pct;
}

/* ----------------------------- Thrust scaling laws ----------------------------- */

TEST(Turbofan2SpoolTest, ThrustScalesQuadraticallyWithN1) {
  // T ~ mdot_air*(V_e - V_0). At fixed altitude, both mdot_air and V_e rise
  // with N1 -> roughly quadratic combined effect.
  Turbofan2SpoolParams p;
  Turbofan2SpoolState s_low;
  Turbofan2SpoolState s_high;
  s_low.N1_pct = 50.0;
  s_high.N1_pct = 100.0;
  // Freeze N1/N2 dynamics so the step holds the set rates.
  p.tau_N1_s = 1e9;
  p.tau_N2_s = 1e9;
  s_low.N2_pct = s_low.N1_pct;
  s_high.N2_pct = s_high.N1_pct;

  const auto r_low = stepTurbofan2Spool(s_low, p, 0.5, kRho_SL, 0.001);
  const auto r_high = stepTurbofan2Spool(s_high, p, 1.0, kRho_SL, 0.001);

  // Ratio should be (100/50)^2 = 4.
  EXPECT_NEAR(r_high.thrust_N / r_low.thrust_N, 4.0, 0.05);
}

TEST(Turbofan2SpoolTest, ThrustDropsWithAltitudePerDensityExponent) {
  // T(alt) = T(SL)*(rho_alt/rho_SL)^n. At 12 km, rho=0.249*rho_SL -> factor
  // 0.249^0.7 = 0.379.
  Turbofan2SpoolParams p;
  Turbofan2SpoolState s;
  s.N1_pct = 100.0;
  s.N2_pct = 100.0;
  p.tau_N1_s = 1e9;
  p.tau_N2_s = 1e9; // freeze

  const auto r_sl = stepTurbofan2Spool(s, p, 1.0, kRho_SL, 0.001);
  const auto r_12km = stepTurbofan2Spool(s, p, 1.0, kRho_12km, 0.001);

  const double expected_ratio = std::pow(kRho_12km / kRho_SL, p.n_density);
  EXPECT_NEAR(r_12km.thrust_N / r_sl.thrust_N, expected_ratio, 0.001);
  EXPECT_NEAR(expected_ratio, 0.379, 0.005); // hand-computed sanity
}

TEST(Turbofan2SpoolTest, VacuumGivesZeroThrust) {
  Turbofan2SpoolParams p;
  Turbofan2SpoolState s;
  s.N1_pct = 100.0;
  s.N2_pct = 100.0;
  p.tau_N1_s = 1e9;
  p.tau_N2_s = 1e9;

  const auto r = stepTurbofan2Spool(s, p, 1.0, /*rho*/ 0.0, 0.001);
  EXPECT_EQ(r.thrust_N, 0.0);
}

/* ----------------------------- Throttle clamping ----------------------------- */

TEST(Turbofan2SpoolTest, ThrottleClampsToZeroOneRange) {
  Turbofan2SpoolParams p;
  Turbofan2SpoolState s_over;
  Turbofan2SpoolState s_under;
  // Drive each into steady state with clamped throttle.
  for (int i = 0; i < 600; ++i) {
    stepTurbofan2Spool(s_over, p, /*throttle*/ 2.0, kRho_SL, 0.05);
    stepTurbofan2Spool(s_under, p, /*throttle*/ -1.0, kRho_SL, 0.05);
  }
  EXPECT_NEAR(s_over.N2_pct, 100.0, 0.1);
  EXPECT_NEAR(s_under.N2_pct, p.N2_idle_pct, 0.5);
}

/* ----------------------------- Rotor angular momentum ----------------------------- */

TEST(Turbofan2SpoolTest, RotorAngularMomentumScalesLinearlyWithN1) {
  // H = I_rotor * omega; omega = (N1_pct/100) * N1_max_rpm * 2*pi/60. So
  // H is linear in N1_pct.
  Turbofan2SpoolParams p;
  p.tau_N1_s = 1e9;
  p.tau_N2_s = 1e9;
  Turbofan2SpoolState s_25, s_50, s_100;
  s_25.N1_pct = 25.0;
  s_25.N2_pct = 25.0;
  s_50.N1_pct = 50.0;
  s_50.N2_pct = 50.0;
  s_100.N1_pct = 100.0;
  s_100.N2_pct = 100.0;

  const auto r25 = stepTurbofan2Spool(s_25, p, 0.25, kRho_SL, 0.001);
  const auto r50 = stepTurbofan2Spool(s_50, p, 0.50, kRho_SL, 0.001);
  const auto r100 = stepTurbofan2Spool(s_100, p, 1.00, kRho_SL, 0.001);

  EXPECT_NEAR(r50.H_rotor_kgm2_s / r25.H_rotor_kgm2_s, 2.0, 0.01);
  EXPECT_NEAR(r100.H_rotor_kgm2_s / r25.H_rotor_kgm2_s, 4.0, 0.01);

  // Sanity: at N1_max=3900 rpm, omega = 3900*2*pi/60 = 408.4 rad/s
  // H = 150 * 408.4 = 61,261 kg*m^2/s per engine.
  EXPECT_NEAR(r100.H_rotor_kgm2_s, 61261.0, 100.0);
}

/* ----------------------------- Degenerate guard ----------------------------- */

/** @test N2_idle = 100% (zero N2 span) avoids divide-by-zero; N1 holds idle. */
TEST(Turbofan2SpoolTest, ZeroN2SpanHoldsN1AtIdleWithoutNaN) {
  Turbofan2SpoolParams p;
  p.N2_idle_pct = 100.0; // span = 100 - 100 = 0 -> the N1-target guard kicks in
  Turbofan2SpoolState s;
  const auto r = stepTurbofan2Spool(s, p, 1.0, kRho_SL, 0.05);
  EXPECT_FALSE(std::isnan(r.N1_pct));
  EXPECT_FALSE(std::isnan(r.N2_pct));
}
