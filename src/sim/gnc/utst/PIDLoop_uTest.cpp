/**
 * @file PIDLoop_uTest.cpp
 * @brief Unit tests for the PIDLoop primitive.
 */

#include "src/sim/gnc/inc/PIDLoop.hpp"

#include <gtest/gtest.h>

using sim::gnc::PIDGains;
using sim::gnc::PIDLimits;
using sim::gnc::PIDLoop;

/* ----------------------------- Pure proportional ----------------------------- */

/** @test Kp only: u = Kp * error, no time dependence. */
TEST(PIDLoopTest, ProportionalOnlyMatchesKpTimesError) {
  PIDLoop pid({/*Kp*/ 2.0, /*Ki*/ 0.0, /*Kd*/ 0.0});
  EXPECT_DOUBLE_EQ(pid.step(/*err*/ 5.0, /*dt*/ 0.1), 10.0);
  EXPECT_DOUBLE_EQ(pid.step(-3.0, 0.1), -6.0);
}

/* ----------------------------- Pure integral ----------------------------- */

/** @test Ki only: the integral tracks the running sum * dt. */
TEST(PIDLoopTest, IntegralAccumulatesOverTime) {
  PIDLoop pid({0.0, 1.0, 0.0});
  // Constant error = 1 for 10 steps of dt = 0.1 -> integral = 1.0.
  for (int i = 0; i < 10; ++i) {
    (void)pid.step(1.0, 0.1);
  }
  EXPECT_NEAR(pid.integral(), 1.0, 1e-12);
}

/* ----------------------------- Pure derivative ----------------------------- */

/** @test Kd only: derivative = (e - e_prev) / dt; first step is 0 (no history). */
TEST(PIDLoopTest, DerivativeIsZeroOnFirstStepThenTracksRate) {
  PIDLoop pid({0.0, 0.0, 4.0});
  EXPECT_DOUBLE_EQ(pid.step(10.0, 0.1), 0.0); // first step: no prev error
  // err jumps 10 -> 14, derivative = 40, u = Kd * 40 = 160.
  EXPECT_DOUBLE_EQ(pid.step(14.0, 0.1), 160.0);
}

/* ----------------------------- Anti-windup ----------------------------- */

/** @test When the output saturates, the integral does not keep growing. */
TEST(PIDLoopTest, AntiWindupFreezesIntegralAtSaturation) {
  PIDLoop pid({0.0, 1.0, 0.0}, PIDLimits{/*u_min*/ -1.0, /*u_max*/ 1.0});
  // Constant large error: without anti-windup the integral grows unbounded.
  for (int i = 0; i < 100; ++i) {
    (void)pid.step(100.0, 0.1);
  }
  // Held at the saturation boundary (Ki*integral = u_max), not far beyond.
  EXPECT_LE(pid.integral(), 1.5);
}

/* ----------------------------- Reset ----------------------------- */

/** @test reset() clears the integral and the derivative history. */
TEST(PIDLoopTest, ResetClearsState) {
  PIDLoop pid({1.0, 1.0, 1.0});
  (void)pid.step(5.0, 0.1);
  (void)pid.step(7.0, 0.1);
  pid.reset();
  EXPECT_DOUBLE_EQ(pid.integral(), 0.0);
  // First step after reset is proportional + integral (no derivative history).
  EXPECT_DOUBLE_EQ(pid.step(2.0, 0.1), /*Kp*err*/ 2.0 + /*Ki*integral*/ 1.0 * (2.0 * 0.1));
}

/* ----------------------------- Output clamps ----------------------------- */

/** @test The output respects the u_min / u_max ceilings. */
TEST(PIDLoopTest, OutputClampsToLimits) {
  PIDLoop pid({100.0, 0.0, 0.0}, PIDLimits{-1.0, 1.0});
  EXPECT_EQ(pid.step(10.0, 0.01), 1.0);
  EXPECT_EQ(pid.step(-10.0, 0.01), -1.0);
}

/** @test The limits accessor reports the configured clamps. */
TEST(PIDLoopTest, LimitsAccessorReportsClamps) {
  PIDLoop pid({1.0, 0.0, 0.0}, PIDLimits{-2.0, 3.0});
  EXPECT_DOUBLE_EQ(pid.limits().u_min, -2.0);
  EXPECT_DOUBLE_EQ(pid.limits().u_max, 3.0);
}
