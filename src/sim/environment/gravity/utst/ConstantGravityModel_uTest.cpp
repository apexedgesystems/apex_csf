/**
 * @file ConstantGravityModel_uTest.cpp
 * @brief Unit tests for sim::environment::gravity::ConstantGravityModel.
 *
 * Notes:
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 */

#include "src/sim/environment/gravity/inc/ConstantGravityModel.hpp"

#include <gtest/gtest.h>

using sim::environment::gravity::ConstantGravityModel;
using sim::environment::gravity::DEFAULT_G0;

/* ----------------------------- API Tests ----------------------------- */

/** @test Acceleration direction matches radially inward, magnitude matches g0. */
TEST(ConstantGravityModelTest, AccelMatchesDirectionAndMagnitude) {
  ConstantGravityModel model;
  const double G0 = 9.81;
  ASSERT_TRUE(model.init(G0));

  // Along +X
  {
    const double R[3] = {1000.0, 0.0, 0.0};
    double a[3] = {};
    ASSERT_TRUE(model.acceleration(R, a));
    EXPECT_NEAR(a[0], -G0, 1e-12);
    EXPECT_NEAR(a[1], 0., 1e-12);
    EXPECT_NEAR(a[2], 0., 1e-12);
  }

  // Arbitrary direction (3-4-0 triangle)
  {
    const double R[3] = {300.0, 400.0, 0.0};
    double a[3] = {};
    ASSERT_TRUE(model.acceleration(R, a));
    // r_hat = (0.6, 0.8, 0)
    EXPECT_NEAR(a[0], -G0 * 0.6, 1e-12);
    EXPECT_NEAR(a[1], -G0 * 0.8, 1e-12);
    EXPECT_NEAR(a[2], 0.0, 1e-12);
  }
}

/** @test Potential equals g0 times radius. */
TEST(ConstantGravityModelTest, PotentialIsg0TimesRadius) {
  ConstantGravityModel model;
  const double G0 = 9.81;
  ASSERT_TRUE(model.init(G0));

  const double R[3] = {300.0, 400.0, 0.0}; // |r| = 500
  double v = 0.0;
  ASSERT_TRUE(model.potential(R, v));
  EXPECT_NEAR(v, G0 * 500.0, 1e-12);
}

/** @test Default g0 is standard Earth gravity. */
TEST(ConstantGravityModelTest, DefaultG0IsStandard) {
  const ConstantGravityModel MODEL{};
  const double R[3] = {1.0, 0.0, 0.0};
  double v = 0.0;
  ASSERT_TRUE(MODEL.potential(R, v));
  EXPECT_NEAR(v, DEFAULT_G0, 1e-12);
}

/** @test Zero position returns false for acceleration. */
TEST(ConstantGravityModelTest, ZeroPositionReturnsFalse) {
  ConstantGravityModel model;
  ASSERT_TRUE(model.init(9.81));

  const double R[3] = {0.0, 0.0, 0.0};
  double a[3] = {};
  EXPECT_FALSE(model.acceleration(R, a));
}

/** @test maxDegree returns 0 for constant model. */
TEST(ConstantGravityModelTest, MaxDegreeIsZero) {
  const ConstantGravityModel MODEL{};
  EXPECT_EQ(MODEL.maxDegree(), 0);
}
