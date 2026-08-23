/**
 * @file LateralControllers_uTest.cpp
 * @brief Tests for the lateral-directional loops: roll, heading, yaw damper.
 *
 * Covers sign, clamping, the shortest-path heading wrap, and the yaw-damper
 * washout (rejects a steady turn rate, passes the transient; tau = 0 disables it).
 */

#include "src/sim/gnc/aircraft/inc/LateralControllers.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::gnc::aircraft::HeadingHold;
using sim::gnc::aircraft::RollController;
using sim::gnc::aircraft::YawDamper;

/* ---------------------- RollController ---------------------- */

/** @test A positive bank error commands positive aileron (no sign flip). */
TEST(RollControllerTest, PositiveBankErrorProducesPositiveAileron) {
  RollController ctl;
  const double aileron = ctl.step(/*ref*/ 0.30, /*actual*/ 0.0, 0.04);
  EXPECT_GT(aileron, 0.0);
}

/** @test The aileron command clamps at the configured limit. */
TEST(RollControllerTest, AileronClampsAtLimit) {
  RollController ctl;
  ctl.setAileronLimit(0.10);
  const double aileron = ctl.step(2.0, 0.0, 0.04); // 2 rad bank error
  EXPECT_LE(aileron, 0.10 + 1e-12);
  EXPECT_GT(ctl.gains().Kp, 0.0);
}

/* ---------------------- HeadingHold ---------------------- */

/** @test A 358 deg error wraps to -2 deg, commanding a small left bank. */
TEST(HeadingHoldTest, ShortestPathWrapsLargePositiveError) {
  HeadingHold ctl;
  constexpr double kDeg = 3.14159265358979323846 / 180.0;
  const double bank = ctl.step(/*ref*/ 358.0 * kDeg, /*actual*/ 0.0, 0.04);
  EXPECT_LT(bank, 0.0);   // wraps to -2 deg -> left bank
  EXPECT_GT(bank, -0.20); // small magnitude
}

/** @test The bank reference clamps at the configured limit. */
TEST(HeadingHoldTest, BankReferenceClampsAtLimit) {
  HeadingHold ctl;
  ctl.setBankLimit(0.10);
  const double bank = ctl.step(/*ref*/ 1.57, /*actual*/ 0.0, 0.04); // 90 deg error
  EXPECT_NEAR(bank, 0.10, 1e-12);
  EXPECT_GT(ctl.gains().Kp, 0.0);
}

/* ---------------------- YawDamper ---------------------- */

/** @test A positive yaw rate commands positive rudder (opposing yaw moment). */
TEST(YawDamperTest, PositiveYawRateProducesPositiveRudder) {
  YawDamper ctl;
  ctl.setWashoutTau(3.0);
  EXPECT_GT(ctl.step(/*yaw_rate*/ 0.20, /*dt*/ 0.04), 0.0);
  EXPECT_GT(ctl.gain(), 0.0);
  EXPECT_GT(ctl.washoutTau(), 0.0);
}

/** @test A negative yaw rate commands negative rudder. */
TEST(YawDamperTest, NegativeYawRateProducesNegativeRudder) {
  YawDamper ctl;
  ctl.setWashoutTau(3.0);
  EXPECT_LT(ctl.step(-0.20, 0.04), 0.0);
}

/** @test The rudder command clamps at the configured limit. */
TEST(YawDamperTest, RudderClampsAtLimit) {
  YawDamper ctl;
  ctl.setGain(2.0);
  ctl.setRudderLimit(0.05);
  ctl.setWashoutTau(0.0); // disable washout: pure |K_r * r| saturation
  EXPECT_NEAR(ctl.step(/*yaw_rate*/ 5.0, 0.04), +0.05, 1e-12);
}

/** @test The washout rejects a constant yaw rate (steady turn) over time. */
TEST(YawDamperTest, WashoutRejectsConstantYawRate) {
  YawDamper ctl;
  ctl.setGain(1.0);
  ctl.setWashoutTau(3.0);
  const double dt = 0.04;
  const double r_const = 0.10;

  const double dr_t0 = ctl.step(r_const, dt);
  EXPECT_GT(dr_t0, 0.0); // first sample passes through

  double dr_ss = 0.0;
  for (int i = 0; i < 375; ++i) { // ~5 tau_w
    dr_ss = ctl.step(r_const, dt);
  }
  EXPECT_LT(std::abs(dr_ss), 0.01 * std::abs(dr_t0)); // <1% of the transient
}

/** @test tau = 0 disables the washout (pure proportional feedback, no decay). */
TEST(YawDamperTest, WashoutDisabledWhenTauZero) {
  YawDamper ctl;
  ctl.setGain(0.80);
  ctl.setWashoutTau(0.0);
  const double r = 0.20;
  EXPECT_NEAR(ctl.step(r, 0.04), 0.80 * r, 1e-12);
  EXPECT_NEAR(ctl.step(r, 0.04), 0.80 * r, 1e-12); // no decay
}

/** @test A negative bank error saturates the aileron negative; reset clears state. */
TEST(RollControllerTest, NegativeBankErrorClampsNegativeAndResets) {
  RollController ctl;
  ctl.setAileronLimit(0.10);
  EXPECT_NEAR(ctl.step(-2.0, 0.0, 0.04), -0.10, 1e-12);
  ctl.reset();
  EXPECT_DOUBLE_EQ(ctl.step(0.0, 0.0, 0.04), 0.0);
}

/** @test A -358 deg error wraps to +2 deg (the other wrap branch), commanding right bank. */
TEST(HeadingHoldTest, ShortestPathWrapsLargeNegativeError) {
  HeadingHold ctl;
  constexpr double kDeg = 3.14159265358979323846 / 180.0;
  const double bank = ctl.step(/*ref*/ 0.0, /*actual*/ 358.0 * kDeg, 0.04);
  EXPECT_GT(bank, 0.0); // wraps to +2 deg -> right bank
  EXPECT_LT(bank, 0.20);
}

/** @test The bank reference saturates negative; reset clears state. */
TEST(HeadingHoldTest, NegativeBankClampAndReset) {
  HeadingHold ctl;
  ctl.setBankLimit(0.10);
  EXPECT_NEAR(ctl.step(/*ref*/ -1.57, /*actual*/ 0.0, 0.04), -0.10, 1e-12); // -90 deg error
  ctl.reset();
  EXPECT_DOUBLE_EQ(ctl.step(0.0, 0.0, 0.04), 0.0);
}

/** @test A negative yaw rate saturates the rudder negative; reset clears the washout. */
TEST(YawDamperTest, NegativeRudderClampAndReset) {
  YawDamper ctl;
  ctl.setGain(2.0);
  ctl.setRudderLimit(0.05);
  ctl.setWashoutTau(0.0);
  EXPECT_NEAR(ctl.step(-5.0, 0.04), -0.05, 1e-12);
  ctl.reset();
  EXPECT_NEAR(ctl.step(0.0, 0.04), 0.0, 1e-12);
}

/* ---------------------- Capture anti-windup ---------------------- */

/** @test A 90-degree heading capture settles with bounded overshoot:
 *  the bank clamp is plumbed into the PID, so the integral cannot wind
 *  while the command is pinned during the long turn. The plant is the
 *  coordinated-turn kinematic (psi_dot = g tan(phi) / V) with a
 *  first-order roll response — the same structure the demo flies.
 *  An unplumbed clamp winds ~3 rad of integral across this capture
 *  and overshoots by tens of degrees before unwinding. */
TEST(HeadingHoldTest, LongCaptureSettlesWithoutWindupOvershoot) {
  sim::gnc::aircraft::HeadingHold hold; // default gains + 0.45 rad bank limit

  constexpr double kPi = 3.14159265358979323846;
  constexpr double DT = 1.0 / 25.0; // the demo's controller rate
  constexpr double V = 235.9;
  constexpr double G = 9.80665;
  constexpr double PSI_REF = 135.0 * kPi / 180.0;

  double psi = 45.0 * kPi / 180.0; // boot heading
  double phi = 0.0;                // bank, first-order toward phi_ref
  constexpr double ROLL_TAU_S = 1.0;

  double max_psi = psi;
  for (int i = 0; i < 25 * 240; ++i) { // 240 s, generous
    const double PHI_REF = hold.step(PSI_REF, psi, DT);
    phi += (PHI_REF - phi) * (DT / ROLL_TAU_S);
    psi += (G * std::tan(phi) / V) * DT;
    if (psi > max_psi) {
      max_psi = psi;
    }
  }

  // Overshoot bounded to a few degrees (was tens of degrees unplumbed).
  EXPECT_LT(max_psi - PSI_REF, 8.0 * kPi / 180.0)
      << "overshoot " << (max_psi - PSI_REF) * 180.0 / kPi << " deg";
  // And the capture actually settled on target.
  EXPECT_NEAR(psi, PSI_REF, 1.0 * kPi / 180.0);
}
