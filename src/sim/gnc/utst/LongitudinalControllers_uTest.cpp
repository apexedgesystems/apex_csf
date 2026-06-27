/**
 * @file LongitudinalControllers_uTest.cpp
 * @brief Tests for the longitudinal loops: pitch attitude, altitude, speed hold.
 *
 * Each loop is checked for sign (error -> correct-sign command), clamping, and
 * the throttle saturation floor/ceiling.
 */

#include "src/sim/gnc/inc/LongitudinalControllers.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::gnc::AltitudeHold;
using sim::gnc::PitchAttitudeHold;
using sim::gnc::SpeedHold;

/* ---------------------- PitchAttitudeHold ---------------------- */

/** @test A pitch-up error commands a negative (trailing-edge-up) elevator. */
TEST(PitchAttitudeHoldTest, PositiveErrorProducesNegativeElevator) {
  PitchAttitudeHold ctl;
  const double elevator = ctl.step(/*ref*/ 0.10, /*actual*/ 0.05, /*dt*/ 0.04);
  EXPECT_LT(elevator, 0.0);
}

/** @test On target (zero error, after settling) the elevator is zero. */
TEST(PitchAttitudeHoldTest, ZeroErrorSettlesToZeroElevator) {
  PitchAttitudeHold ctl;
  const double dt = 0.04;
  (void)ctl.step(0.10, 0.10, dt); // first call: no derivative history
  const double elevator = ctl.step(0.10, 0.10, dt);
  EXPECT_NEAR(elevator, 0.0, 1e-12);
}

/** @test The elevator command clamps at the configured limit. */
TEST(PitchAttitudeHoldTest, ElevatorClampsAtLimit) {
  PitchAttitudeHold ctl;
  ctl.setElevatorLimit(0.10);
  const double elevator = ctl.step(5.0, 0.0, 0.04); // huge error
  EXPECT_LE(std::abs(elevator), 0.10 + 1e-12);
  EXPECT_GT(ctl.gains().Kp, 0.0); // default gains populated
}

/* ---------------------- AltitudeHold ---------------------- */

/** @test Below the target altitude, the loop commands a positive pitch reference. */
TEST(AltitudeHoldTest, PositiveAltitudeErrorProducesPositivePitchRef) {
  AltitudeHold ctl;
  const double theta_ref = ctl.step(/*ref_m*/ 8200.0, /*actual_m*/ 8000.0, 0.04);
  EXPECT_GT(theta_ref, 0.0);
}

/** @test The pitch reference clamps at the configured limit. */
TEST(AltitudeHoldTest, PitchRefClampsAtLimit) {
  AltitudeHold ctl;
  ctl.setPitchLimit(0.05);
  const double theta_ref = ctl.step(13000.0, 8000.0, 0.04); // 5 km error
  EXPECT_NEAR(theta_ref, 0.05, 1e-12);
  EXPECT_GT(ctl.gains().Kp, 0.0);
}

/* ---------------------- SpeedHold ---------------------- */

/** @test A slower reference reduces throttle below trim (floored at 0). */
TEST(SpeedHoldTest, SlowReferenceReducesThrottleFromTrim) {
  SpeedHold ctl;
  ctl.setTrimThrottle(0.50);
  const double th = ctl.step(/*V_ref*/ 200.0, /*V_actual*/ 240.0, 0.04);
  EXPECT_LT(th, 0.50);
  EXPECT_GE(th, 0.0);
}

/** @test A faster reference increases throttle above trim (capped at 1). */
TEST(SpeedHoldTest, FastReferenceIncreasesThrottleFromTrim) {
  SpeedHold ctl;
  ctl.setTrimThrottle(0.50);
  const double th = ctl.step(/*V_ref*/ 280.0, /*V_actual*/ 240.0, 0.04);
  EXPECT_GT(th, 0.50);
  EXPECT_LE(th, 1.0);
}

/** @test A large overspeed saturates throttle to zero. */
TEST(SpeedHoldTest, ThrottleClampsToZeroOnLargeNegativeError) {
  SpeedHold ctl;
  ctl.setTrimThrottle(0.50);
  const double th = ctl.step(/*V_ref*/ 50.0, /*V_actual*/ 240.0, 0.04);
  EXPECT_NEAR(th, 0.0, 1e-12);
  EXPECT_GT(ctl.gains().Kp, 0.0);
}

/** @test A nose-down error saturates the elevator positive; reset clears state. */
TEST(PitchAttitudeHoldTest, NegativeErrorClampsPositiveAndResets) {
  PitchAttitudeHold ctl;
  ctl.setElevatorLimit(0.10);
  EXPECT_NEAR(ctl.step(-5.0, 0.0, 0.04), 0.10, 1e-12); // -error -> +elevator saturated
  ctl.reset();
  (void)ctl.step(0.0, 0.0, 0.04);
  EXPECT_NEAR(ctl.step(0.0, 0.0, 0.04), 0.0, 1e-12); // cleared: on-target -> 0
}

/** @test Above target, the pitch reference saturates negative; reset clears state. */
TEST(AltitudeHoldTest, AboveTargetClampsNegativeAndResets) {
  AltitudeHold ctl;
  ctl.setPitchLimit(0.05);
  EXPECT_NEAR(ctl.step(3000.0, 8000.0, 0.04), -0.05, 1e-12); // above target -> -pitch clamp
  ctl.reset();
  EXPECT_DOUBLE_EQ(ctl.step(0.0, 0.0, 0.04), 0.0);
}

/** @test reset() clears the auto-throttle state back to trim. */
TEST(SpeedHoldTest, ResetClearsState) {
  SpeedHold ctl;
  ctl.setTrimThrottle(0.50);
  (void)ctl.step(280.0, 240.0, 0.04);
  ctl.reset();
  EXPECT_NEAR(ctl.step(240.0, 240.0, 0.04), 0.50, 1e-9); // on-speed -> trim
}
