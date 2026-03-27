/**
 * @file RegulatorModel_uTest.cpp
 * @brief Unit tests for analog::RegulatorModel.
 *
 * Notes:
 *  - Tests verify physics correctness with known component values.
 *  - Tolerance checks use EXPECT_NEAR for floating-point stability.
 */

#include "src/sim/analog/regulator/inc/RegulatorModel.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::analog::RegulatorParams;
using sim::analog::RegulatorResult;
using sim::analog::simulate;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Nominal params produce 3.3V output */
TEST(RegulatorModelTest, NominalOutputVoltage) {
  RegulatorParams params;
  auto result = simulate(params);

  // V_out = 1.25 * (1 + 100k/60.606k) = 1.25 * 2.6500... = 3.3125
  // Close to 3.3V (exact depends on R2 value chosen)
  EXPECT_NEAR(result.vOut, 3.3, 0.1);
}

/** @test Nominal params are in spec */
TEST(RegulatorModelTest, NominalInSpec) {
  RegulatorParams params;
  auto result = simulate(params);

  EXPECT_TRUE(result.inSpec);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Output voltage scales with R1/R2 ratio */
TEST(RegulatorModelTest, OutputScalesWithRatio) {
  RegulatorParams params;

  // Double R1 -> roughly double the (1 + R1/R2) factor increase
  params.r1 = 200000.0;
  auto result = simulate(params);

  // V_out = 1.25 * (1 + 200k/60.606k) = 1.25 * 4.3 = ~5.375
  EXPECT_GT(result.vOut, 5.0);
}

/** @test Output voltage scales with V_ref */
TEST(RegulatorModelTest, OutputScalesWithVref) {
  RegulatorParams params;

  // Higher V_ref -> higher V_out
  params.vRef = 2.5;
  auto resultHigh = simulate(params);

  params.vRef = 0.8;
  auto resultLow = simulate(params);

  EXPECT_GT(resultHigh.vOut, resultLow.vOut);
}

/** @test Ripple increases with ESR */
TEST(RegulatorModelTest, RippleIncreasesWithEsr) {
  RegulatorParams params;
  params.esr = 0.001;
  auto resultLow = simulate(params);

  params.esr = 0.100;
  auto resultHigh = simulate(params);

  EXPECT_GT(resultHigh.ripple, resultLow.ripple);
}

/** @test Ripple increases with load current */
TEST(RegulatorModelTest, RippleIncreasesWithLoad) {
  RegulatorParams params;
  params.iLoad = 0.010;
  auto resultLow = simulate(params);

  params.iLoad = 1.000;
  auto resultHigh = simulate(params);

  EXPECT_GT(resultHigh.ripple, resultLow.ripple);
}

/** @test Ripple is positive */
TEST(RegulatorModelTest, RipplePositive) {
  RegulatorParams params;
  auto result = simulate(params);

  EXPECT_GT(result.ripple, 0.0);
}

/** @test Settling time is positive */
TEST(RegulatorModelTest, SettlingTimePositive) {
  RegulatorParams params;
  auto result = simulate(params);

  EXPECT_GT(result.settlingTime, 0.0);
}

/** @test Larger capacitor increases settling time */
TEST(RegulatorModelTest, LargerCapSlowerSettling) {
  RegulatorParams params;
  params.cOut = 1.0e-6;
  auto resultSmall = simulate(params);

  params.cOut = 100.0e-6;
  auto resultLarge = simulate(params);

  EXPECT_GT(resultLarge.settlingTime, resultSmall.settlingTime);
}

/** @test Phase margin is in valid range */
TEST(RegulatorModelTest, PhaseMarginInRange) {
  RegulatorParams params;
  auto result = simulate(params);

  EXPECT_GE(result.phaseMargin, 0.0);
  EXPECT_LE(result.phaseMargin, 180.0);
}

/** @test Out of spec when R1/R2 ratio is far off */
TEST(RegulatorModelTest, OutOfSpecWithBadRatio) {
  RegulatorParams params;
  params.r1 = 200000.0; // Way too high -> V_out >> 3.3V
  auto result = simulate(params);

  EXPECT_FALSE(result.inSpec);
}

/** @test Out of spec with extreme V_ref */
TEST(RegulatorModelTest, OutOfSpecWithExtremeVref) {
  RegulatorParams params;
  params.vRef = 2.0; // Too high -> V_out >> 3.3V
  auto result = simulate(params);

  EXPECT_FALSE(result.inSpec);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Same params always produce same result */
TEST(RegulatorModelTest, Deterministic) {
  RegulatorParams params;
  auto a = simulate(params);
  auto b = simulate(params);

  EXPECT_DOUBLE_EQ(a.vOut, b.vOut);
  EXPECT_DOUBLE_EQ(a.ripple, b.ripple);
  EXPECT_DOUBLE_EQ(a.settlingTime, b.settlingTime);
  EXPECT_DOUBLE_EQ(a.phaseMargin, b.phaseMargin);
  EXPECT_EQ(a.inSpec, b.inSpec);
}
