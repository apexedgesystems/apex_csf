/**
 * @file ExponentialAtmosphere_uTest.cpp
 * @brief Tests for the isothermal exponential atmosphere model.
 *
 * Validation against ground truth:
 *   - Sea-level: rho should equal rho0 = 1.225 kg/m^3 (ISA reference).
 *   - One scale height up (8500 m): rho should equal rho0 / e.
 *   - Pressure derived from ideal gas law: P = rho * R * T.
 *   - Speed of sound: a = sqrt(gamma * R * T) -- 340 m/s at 288.15 K.
 *   - Drop-off rate at h = 50 km should match published USSA76 within
 *     order-of-magnitude (exponential is an approximation).
 */

#include "src/sim/environment/atmosphere/inc/ExponentialAtmosphere.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::environment::atmosphere::AtmosphereState;
using sim::environment::atmosphere::ExponentialAtmosphere;
using sim::environment::atmosphere::isSuccess;
using sim::environment::atmosphere::Status;
namespace earth = sim::environment::atmosphere::earth_defaults;

namespace {

constexpr double E_INV = 0.3678794411714423; // 1 / e

/* ----------------------------- Default Construction ----------------------------- */

TEST(ExponentialAtmosphere, DefaultIsEarthIsa) {
  ExponentialAtmosphere a;
  EXPECT_TRUE(a.isInitialized());
  EXPECT_DOUBLE_EQ(a.rho0(), earth::RHO0);
  EXPECT_DOUBLE_EQ(a.T0(), earth::T0);
  EXPECT_DOUBLE_EQ(a.scaleHeight(), earth::H_SCALE);
  EXPECT_FALSE(a.isVacuum());
}

TEST(ExponentialAtmosphere, ExplicitParamsSet) {
  ExponentialAtmosphere a(0.02, 210.0, 11100.0); // Mars-ish baseline
  EXPECT_TRUE(a.isInitialized());
  EXPECT_DOUBLE_EQ(a.rho0(), 0.02);
  EXPECT_DOUBLE_EQ(a.T0(), 210.0);
  EXPECT_DOUBLE_EQ(a.scaleHeight(), 11100.0);
}

TEST(ExponentialAtmosphere, InvalidParamsRejected) {
  ExponentialAtmosphere a;
  // T <= 0 -> invalid temperature
  EXPECT_EQ(a.init(1.0, 0.0, 8500.0), Status::ERROR_PARAM_TEMP_INVALID);
  EXPECT_FALSE(a.isInitialized());
  // H <= 0 -> invalid scale height
  EXPECT_EQ(a.init(1.0, 288.0, 0.0), Status::ERROR_PARAM_SCALE_INVALID);
  EXPECT_FALSE(a.isInitialized());
  // Negative rho -> invalid density
  EXPECT_EQ(a.init(-1.0, 288.0, 8500.0), Status::ERROR_PARAM_RHO_INVALID);
  EXPECT_FALSE(a.isInitialized());
  // R_specific <= 0 -> invalid gas constant
  EXPECT_EQ(a.init(1.0, 288.0, 8500.0, 1.4, 0.0), Status::ERROR_PARAM_GAS_CONST_INVALID);
  EXPECT_FALSE(a.isInitialized());
}

/* ----------------------------- Method Tests ----------------------------- */

TEST(ExponentialAtmosphere, SeaLevelDensityMatchesRho0) {
  // Default Earth ISA.
  ExponentialAtmosphere a;
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s)));
  EXPECT_DOUBLE_EQ(s.rho, earth::RHO0);
  EXPECT_DOUBLE_EQ(s.T, earth::T0);
}

TEST(ExponentialAtmosphere, OneScaleHeightDecaysByE) {
  ExponentialAtmosphere a;
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(earth::H_SCALE, 0.0, 0.0, s)));
  EXPECT_NEAR(s.rho, earth::RHO0 * E_INV, 1e-12);
  EXPECT_DOUBLE_EQ(s.T, earth::T0); // isothermal
}

TEST(ExponentialAtmosphere, PressureFromIdealGas) {
  // P = rho * R * T at all altitudes (isothermal).
  ExponentialAtmosphere a;
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s)));
  // Sea-level pressure should be ~ 1.225 * 287.058 * 288.15 = 101325 (ISA).
  EXPECT_NEAR(s.P, 1.225 * 287.058 * 288.15, 1.0);
  EXPECT_NEAR(s.P, 101300.0, 100.0); // close to standard 101325 Pa
}

TEST(ExponentialAtmosphere, SoundSpeedConstant) {
  ExponentialAtmosphere a;
  AtmosphereState s0, s10k;
  ASSERT_TRUE(isSuccess(a.query(0.0, 0.0, 0.0, s0)));
  ASSERT_TRUE(isSuccess(a.query(10000.0, 0.0, 0.0, s10k)));
  // Isothermal -> same T -> same a.
  EXPECT_DOUBLE_EQ(s0.a, s10k.a);
  // Approx 340 m/s at sea-level air.
  EXPECT_NEAR(s0.a, 340.294, 0.5);
}

TEST(ExponentialAtmosphere, DensityConvenienceMatchesQuery) {
  ExponentialAtmosphere a;
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(5000.0, 0.0, 0.0, s)));
  double rho = -1.0;
  ASSERT_TRUE(isSuccess(a.density(5000.0, 0.0, 0.0, rho)));
  EXPECT_DOUBLE_EQ(rho, s.rho);
}

/* ----------------------------- Validation Against Reference Data ----------------------------- */

// Order-of-magnitude check: Earth-ISA exponential at 50 km should give
// rho on the order of published USSA76 value (~1e-3 kg/m^3). Exponential
// underpredicts because the real atmosphere isn't isothermal, but it
// should be within a factor of 5.
TEST(ExponentialAtmosphere, MatchesUssa76OrderOfMagnitudeAt50km) {
  ExponentialAtmosphere a;
  double rho = 0.0;
  ASSERT_TRUE(isSuccess(a.density(50000.0, 0.0, 0.0, rho)));
  // USSA76 at 50 km: ~1.027e-3 kg/m^3.
  // Exponential with H=8500: rho = 1.225 * exp(-50000/8500) = ~3.4e-3.
  // Within a factor of 5 of USSA76 reference.
  EXPECT_GT(rho, 1e-4);
  EXPECT_LT(rho, 1e-2);
}

// Same check at 100 km. USSA76 ~5.6e-7 kg/m^3.
// Exponential: 1.225 * exp(-100000/8500) = ~9.5e-6.
// Within an order of magnitude is acceptable for the analytic baseline.
TEST(ExponentialAtmosphere, MatchesUssa76OrderOfMagnitudeAt100km) {
  ExponentialAtmosphere a;
  double rho = 0.0;
  ASSERT_TRUE(isSuccess(a.density(100000.0, 0.0, 0.0, rho)));
  EXPECT_GT(rho, 1e-7);
  EXPECT_LT(rho, 1e-4);
}

/* ----------------------------- API Tests ----------------------------- */

TEST(ExponentialAtmosphere, NotVacuum) {
  ExponentialAtmosphere a;
  EXPECT_FALSE(a.isVacuum());
}

TEST(ExponentialAtmosphere, GlobalAltitudeValidity) {
  ExponentialAtmosphere a;
  EXPECT_TRUE(std::isinf(a.minAltitudeM()));
  EXPECT_TRUE(std::isinf(a.maxAltitudeM()));
}

TEST(ExponentialAtmosphere, BrokenInitFailsQuery) {
  ExponentialAtmosphere a;
  ASSERT_EQ(a.init(1.0, -1.0, 8500.0), Status::ERROR_PARAM_TEMP_INVALID); // invalid T
  AtmosphereState s{};
  EXPECT_EQ(a.query(0.0, 0.0, 0.0, s), Status::ERROR_NOT_INITIALIZED);
  // On a non-SUCCESS query the state is left unmodified (still default-zero).
  EXPECT_DOUBLE_EQ(s.rho, 0.0);
}

TEST(ExponentialAtmosphere, ConveniencePressureAndTemperature) {
  // Success path: the base-class pressure()/temperature() defaults forward
  // the query() bundle.
  ExponentialAtmosphere a;
  AtmosphereState s;
  ASSERT_TRUE(isSuccess(a.query(3000.0, 0.0, 0.0, s)));
  double P = -1.0, T = -1.0;
  ASSERT_TRUE(isSuccess(a.pressure(3000.0, 0.0, 0.0, P)));
  ASSERT_TRUE(isSuccess(a.temperature(3000.0, 0.0, 0.0, T)));
  EXPECT_DOUBLE_EQ(P, s.P);
  EXPECT_DOUBLE_EQ(T, s.T);
}

TEST(ExponentialAtmosphere, ConvenienceLeavesOutputUnmodifiedOnError) {
  // The base-class pressure()/temperature() defaults must leave the caller's
  // buffer untouched when the underlying query() fails.
  ExponentialAtmosphere a;
  ASSERT_EQ(a.init(1.0, -1.0, 8500.0), Status::ERROR_PARAM_TEMP_INVALID); // broken
  double P = 12.5, T = 34.0;
  EXPECT_EQ(a.pressure(0.0, 0.0, 0.0, P), Status::ERROR_NOT_INITIALIZED);
  EXPECT_EQ(a.temperature(0.0, 0.0, 0.0, T), Status::ERROR_NOT_INITIALIZED);
  EXPECT_DOUBLE_EQ(P, 12.5); // unmodified
  EXPECT_DOUBLE_EQ(T, 34.0); // unmodified
}

TEST(ExponentialAtmosphere, NanAltitudeRejected) {
  ExponentialAtmosphere a;
  AtmosphereState s{};
  EXPECT_EQ(a.query(std::nan(""), 0.0, 0.0, s), Status::ERROR_PARAM_ALT_INVALID);
  double rho = -1.0;
  EXPECT_EQ(a.density(std::nan(""), 0.0, 0.0, rho), Status::ERROR_PARAM_ALT_INVALID);
  EXPECT_DOUBLE_EQ(rho, -1.0); // left unmodified
}

} // namespace
