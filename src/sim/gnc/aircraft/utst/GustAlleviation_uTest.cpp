/**
 * @file GustAlleviation_uTest.cpp
 * @brief Tests for the longitudinal gust-alleviation feedforward.
 *
 * Covers zero/positive/negative gust sign, linearity in w_g and inverse in V,
 * saturation, the authority scale, and the low-V divide guard.
 */

#include "src/sim/gnc/aircraft/inc/GustAlleviation.hpp"

#include <gtest/gtest.h>

using sim::gnc::aircraft::GustAlleviation;
using sim::gnc::aircraft::GustAlleviationParams;

/** @test Zero gust gives zero command. */
TEST(GustAlleviationTest, ZeroGustGivesZeroOutput) {
  GustAlleviation ga;
  EXPECT_DOUBLE_EQ(ga.step(/*w_g*/ 0.0, /*V*/ 235.0), 0.0);
}

/** @test An upward gust commands a negative (trailing-edge-up) elevator. */
TEST(GustAlleviationTest, PositiveGustProducesNegativeElevator) {
  GustAlleviation ga;
  EXPECT_LT(ga.step(/*w_g*/ +2.0, /*V*/ 235.0), 0.0);
}

/** @test A downward gust commands a positive elevator. */
TEST(GustAlleviationTest, NegativeGustProducesPositiveElevator) {
  GustAlleviation ga;
  EXPECT_GT(ga.step(/*w_g*/ -2.0, /*V*/ 235.0), 0.0);
}

/** @test Output scales linearly with w_g and inversely with V. */
TEST(GustAlleviationTest, OutputLinearInWgAndInverseInV) {
  GustAlleviation ga;
  const double de1 = ga.step(1.0, 200.0);
  const double de2 = ga.step(2.0, 200.0);
  const double de3 = ga.step(1.0, 100.0);
  EXPECT_NEAR(de2 / de1, 2.0, 1e-9); // doubling w_g -> 2x magnitude
  EXPECT_NEAR(de3 / de1, 2.0, 1e-9); // halving V    -> 2x magnitude
}

/** @test The command saturates at the elevator limit. */
TEST(GustAlleviationTest, SaturatesAtLimit) {
  GustAlleviationParams p;
  p.elevator_limit_rad = 0.10;
  GustAlleviation ga(p);
  EXPECT_NEAR(ga.step(/*w_g*/ 10.0, /*V*/ 50.0), -0.10, 1e-12);
  EXPECT_DOUBLE_EQ(ga.params().elevator_limit_rad, 0.10);
}

/** @test Zero authority disables the feedforward regardless of gust. */
TEST(GustAlleviationTest, ZeroAuthorityDisablesFeedforward) {
  GustAlleviationParams p;
  p.gust_authority_pct = 0.0;
  GustAlleviation ga(p);
  EXPECT_DOUBLE_EQ(ga.step(5.0, 235.0), 0.0);
  EXPECT_DOUBLE_EQ(ga.step(-5.0, 235.0), 0.0);
}

/** @test A vanishing airspeed yields zero (guards the divide). */
TEST(GustAlleviationTest, LowVGivesZeroToAvoidDivideBlowup) {
  GustAlleviation ga;
  EXPECT_DOUBLE_EQ(ga.step(5.0, 0.5), 0.0);
}

/** @test A strong downward gust saturates the elevator at the positive limit. */
TEST(GustAlleviationTest, SaturatesPositiveOnDownGust) {
  GustAlleviationParams p;
  p.elevator_limit_rad = 0.10;
  GustAlleviation ga(p);
  EXPECT_NEAR(ga.step(/*w_g*/ -10.0, /*V*/ 50.0), 0.10, 1e-12); // down gust -> +elevator clamp
}

/** @test setParams swaps the active parameter set. */
TEST(GustAlleviationTest, SetParamsUpdatesGain) {
  GustAlleviation ga;
  GustAlleviationParams p;
  p.K_alpha_over_delta = 20.0;
  ga.setParams(p);
  EXPECT_DOUBLE_EQ(ga.params().K_alpha_over_delta, 20.0);
}
