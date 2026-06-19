/**
 * @file ConstantAtmosphere_uTest.cpp
 * @brief Tests for the constant-state atmosphere model + vacuum sentinel.
 */

#include "src/sim/environment/atmosphere/inc/ConstantAtmosphere.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::atmosphere::AtmosphereState;
using sim::environment::atmosphere::ConstantAtmosphere;
using sim::environment::atmosphere::isSuccess;
using sim::environment::atmosphere::Status;

namespace {

/* ----------------------------- Default Construction ----------------------------- */

TEST(ConstantAtmosphere, DefaultIsVacuum) {
  ConstantAtmosphere a;
  EXPECT_TRUE(a.isVacuum());
  EXPECT_DOUBLE_EQ(a.rho(), 0.0);
  EXPECT_DOUBLE_EQ(a.P(), 0.0);
  EXPECT_DOUBLE_EQ(a.T(), 0.0);

  AtmosphereState s;
  // A vacuum query still returns a filled (zero) state, flagged with the
  // WARN_VACUUM_QUERY status so a drag consumer can short-circuit.
  EXPECT_EQ(a.query(0.0, 0.0, 0.0, s), Status::WARN_VACUUM_QUERY);
  EXPECT_DOUBLE_EQ(s.rho, 0.0);
  EXPECT_DOUBLE_EQ(s.P, 0.0);
  EXPECT_DOUBLE_EQ(s.T, 0.0);
  EXPECT_DOUBLE_EQ(s.a, 0.0); // T = 0 -> sound speed undefined, zeroed
}

TEST(ConstantAtmosphere, ConstructorSetsState) {
  // Sea-level Earth ISA values.
  ConstantAtmosphere a(1.225, 288.15, 101325.0);
  EXPECT_FALSE(a.isVacuum());
  EXPECT_DOUBLE_EQ(a.rho(), 1.225);
  EXPECT_DOUBLE_EQ(a.T(), 288.15);
  EXPECT_DOUBLE_EQ(a.P(), 101325.0);
}

TEST(ConstantAtmosphere, NegativeRhoClampedToVacuum) {
  ConstantAtmosphere a(-1.0, 200.0, 50000.0);
  EXPECT_TRUE(a.isVacuum());
  EXPECT_DOUBLE_EQ(a.rho(), 0.0);
}

TEST(ConstantAtmosphere, NegativeTempAndPressureClampedToZero) {
  // Each negative input is independently clamped to zero in both the
  // constructor and setState.
  ConstantAtmosphere a(1.0, -5.0, -10.0);
  EXPECT_DOUBLE_EQ(a.rho(), 1.0);
  EXPECT_DOUBLE_EQ(a.T(), 0.0);
  EXPECT_DOUBLE_EQ(a.P(), 0.0);

  a.setState(-2.0, -3.0, -4.0);
  EXPECT_DOUBLE_EQ(a.rho(), 0.0);
  EXPECT_DOUBLE_EQ(a.T(), 0.0);
  EXPECT_DOUBLE_EQ(a.P(), 0.0);
}

TEST(ConstantAtmosphere, AccessorsExposeGasConstants) {
  ConstantAtmosphere a(1.0, 300.0, 90000.0, 1.33, 191.0);
  EXPECT_DOUBLE_EQ(a.gamma(), 1.33);
  EXPECT_DOUBLE_EQ(a.gasConstant(), 191.0);
}

/* ----------------------------- Method Tests ----------------------------- */

TEST(ConstantAtmosphere, QueryReturnsSameAtAnyAltitude) {
  ConstantAtmosphere a(1.0, 250.0, 80000.0);
  AtmosphereState s0, s_high;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s0)));
  ASSERT_TRUE(isSuccess(a.query(1.0e6, 0.0, 0.0, s_high))); // 1000 km up
  EXPECT_DOUBLE_EQ(s0.rho, s_high.rho);
  EXPECT_DOUBLE_EQ(s0.P, s_high.P);
  EXPECT_DOUBLE_EQ(s0.T, s_high.T);
  EXPECT_DOUBLE_EQ(s0.a, s_high.a);
}

TEST(ConstantAtmosphere, SoundSpeedFromIdealGas) {
  // a = sqrt(gamma * R * T) -- check dry-air sea-level value (~340 m/s).
  ConstantAtmosphere a(1.225, 288.15, 101325.0);
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s)));
  const double EXPECTED = std::sqrt(1.4 * 287.058 * 288.15);
  EXPECT_NEAR(s.a, EXPECTED, 1e-9);
  EXPECT_NEAR(s.a, 340.294, 0.5); // sanity check vs textbook value
}

TEST(ConstantAtmosphere, SetStateUpdates) {
  ConstantAtmosphere a;
  a.setState(0.5, 200.0, 30000.0);
  EXPECT_FALSE(a.isVacuum());
  EXPECT_DOUBLE_EQ(a.rho(), 0.5);
  EXPECT_DOUBLE_EQ(a.T(), 200.0);
  EXPECT_DOUBLE_EQ(a.P(), 30000.0);
}

/* ----------------------------- API Tests ----------------------------- */

TEST(ConstantAtmosphere, GlobalAltitudeValidity) {
  ConstantAtmosphere a;
  EXPECT_TRUE(std::isinf(a.minAltitudeM()));
  EXPECT_TRUE(std::isinf(a.maxAltitudeM()));
  EXPECT_TRUE(a.isInValidRange(0.0));
  EXPECT_TRUE(a.isInValidRange(-1e9));
  EXPECT_TRUE(a.isInValidRange(1e12));
}

TEST(ConstantAtmosphere, ConvenienceAccessorsAgreeWithQuery) {
  ConstantAtmosphere a(0.7, 230.0, 40000.0);
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(1000.0, 0.1, 0.2, s)));

  double rho = -1.0, P = -1.0, T = -1.0;
  ASSERT_TRUE(isSuccess(a.density(1000.0, 0.1, 0.2, rho)));
  ASSERT_TRUE(isSuccess(a.pressure(1000.0, 0.1, 0.2, P)));
  ASSERT_TRUE(isSuccess(a.temperature(1000.0, 0.1, 0.2, T)));
  EXPECT_DOUBLE_EQ(rho, s.rho);
  EXPECT_DOUBLE_EQ(P, s.P);
  EXPECT_DOUBLE_EQ(T, s.T);
}

} // namespace
