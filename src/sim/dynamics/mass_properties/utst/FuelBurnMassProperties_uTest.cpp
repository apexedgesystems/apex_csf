/**
 * @file FuelBurnMassProperties_uTest.cpp
 * @brief Unit tests for fuel-burn-driven mass / CG / inertia.
 *
 * Coverage (all tests use explicit, vehicle-agnostic params):
 *   1. Zero thrust -> no fuel burn (and m / CG / I unchanged)
 *   2. Constant thrust over dt -> fuel decreases by TSFC*T*dt
 *   3. Fuel runs out: fuel_kg -> 0, m_dot -> 0, no negative fuel
 *   4. CG migrates linearly with fuel fraction
 *   5. Inertia tensor scales linearly with fuel fraction
 *   6. m_total = m_empty + fuel at all times
 *   7. Fuel can't go negative even with very large dt (clamp)
 */

#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::dynamics::mass_properties::FuelBurnMassPropertiesParams;
using sim::dynamics::mass_properties::FuelBurnMassPropertiesResult;
using sim::dynamics::mass_properties::FuelBurnMassPropertiesState;
using sim::dynamics::mass_properties::stepFuelBurn;
using sim::dynamics::rigid_body::Vec3;

/* ----------------------------- Negative thrust clamp ----------------------------- */

/** @test Negative total thrust is clamped to zero burn (no fuel "regained"). */
TEST(FuelBurnTest, NegativeThrustDoesNotBurnOrRefill) {
  FuelBurnMassPropertiesParams p;
  FuelBurnMassPropertiesState s;
  const double fuel0 = s.fuel_kg;

  const auto r = stepFuelBurn(s, p, /*T*/ -50000.0, /*dt*/ 1.0);
  EXPECT_DOUBLE_EQ(r.m_dot_fuel_kg_s, 0.0);
  EXPECT_DOUBLE_EQ(s.fuel_kg, fuel0); // no burn, and no spurious refill
  EXPECT_DOUBLE_EQ(s.fuel_burned_total_kg, 0.0);
}

/** @test A zero fuel-capacity config yields a 0 fuel fraction (no divide-by-zero). */
TEST(FuelBurnTest, ZeroCapacityGivesZeroFraction) {
  FuelBurnMassPropertiesParams p;
  p.fuel_capacity_kg = 0.0; // exercise the capacity-guard else branch
  FuelBurnMassPropertiesState s;
  s.fuel_kg = 0.0;

  const auto r = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0);
  EXPECT_DOUBLE_EQ(r.fuel_fraction, 0.0);
  EXPECT_TRUE(std::isfinite(r.cg_offset_m.x));
  EXPECT_TRUE(std::isfinite(r.I_body.Ixx));
}

/* ----------------------------- Zero thrust ----------------------------- */

TEST(FuelBurnTest, ZeroThrustNoBurn) {
  FuelBurnMassPropertiesParams p;
  FuelBurnMassPropertiesState s;
  const double fuel0 = s.fuel_kg;

  for (int i = 0; i < 1000; ++i) {
    stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.1);
  }
  EXPECT_DOUBLE_EQ(s.fuel_kg, fuel0);
  EXPECT_DOUBLE_EQ(s.fuel_burned_total_kg, 0.0);
}

/* ----------------------------- TSFC closed-form ----------------------------- */

TEST(FuelBurnTest, ConstantThrustBurnsAtTSFCTimesT) {
  // mdot_fuel = TSFC * T, so over n constant-thrust steps the fuel burned is
  // exactly TSFC * T * (n*dt). Verify the integrator against that closed form.
  FuelBurnMassPropertiesParams p;
  p.TSFC_kg_per_N_s = 2.0e-5; // pick a concrete number
  FuelBurnMassPropertiesState s;

  const double T_total_N = 200000.0; // N
  const double dt = 0.1;
  const int n_steps = 1000; // 100 sec total

  for (int i = 0; i < n_steps; ++i) {
    stepFuelBurn(s, p, T_total_N, dt);
  }
  // Expected fuel burned = TSFC * T * t = 2e-5 * 200000 * 100 = 400 kg
  const double expected_burned = p.TSFC_kg_per_N_s * T_total_N * dt * n_steps;
  EXPECT_NEAR(s.fuel_burned_total_kg, expected_burned, 0.001);
  EXPECT_NEAR(p.fuel_capacity_kg - s.fuel_kg, expected_burned, 0.001);
}

/* ----------------------------- Endurance closed-form ----------------------------- */

TEST(FuelBurnTest, EnduranceMatchesFuelOverBurnRate) {
  // With explicit, vehicle-agnostic params, time-to-empty should equal
  // fuel_capacity / (TSFC * T) for constant thrust. Verify the model.
  FuelBurnMassPropertiesParams p;
  p.TSFC_kg_per_N_s = 2.0e-5;
  p.fuel_capacity_kg = 1000.0;
  FuelBurnMassPropertiesState s;
  s.fuel_kg = p.fuel_capacity_kg;

  const double T_total_N = 100000.0;
  const double dt = 1.0;
  // mdot = 2e-5 * 1e5 = 2.0 kg/s ; endurance = 1000 / 2.0 = 500 s.
  const double expected_s = p.fuel_capacity_kg / (p.TSFC_kg_per_N_s * T_total_N);

  int t_to_empty = 0;
  for (int t = 1; t <= 10000; ++t) {
    stepFuelBurn(s, p, T_total_N, dt);
    if (s.fuel_kg <= 0.0) {
      t_to_empty = t;
      break;
    }
  }
  EXPECT_NEAR(static_cast<double>(t_to_empty), expected_s, 1.0);
}

/* ----------------------------- Fuel exhaustion ----------------------------- */

TEST(FuelBurnTest, FuelExhaustionStopsBurn) {
  // Drain fuel completely, then verify burn stops + fuel stays 0.
  FuelBurnMassPropertiesParams p;
  FuelBurnMassPropertiesState s;
  s.fuel_kg = 100.0; // start with very little fuel

  // Burn lots of fuel fast.
  const double T_total_N = 500000.0;
  const double dt = 1.0;
  for (int i = 0; i < 100; ++i) {
    stepFuelBurn(s, p, T_total_N, dt);
  }
  EXPECT_DOUBLE_EQ(s.fuel_kg, 0.0);
  EXPECT_LE(s.fuel_burned_total_kg, 100.0 + 1e-9); // can't burn more than was there

  // Continue burning -- should be a no-op now.
  const auto r = stepFuelBurn(s, p, T_total_N, dt);
  EXPECT_DOUBLE_EQ(r.m_dot_fuel_kg_s, 0.0);
  EXPECT_DOUBLE_EQ(r.fuel_kg, 0.0);
}

TEST(FuelBurnTest, FuelCannotGoNegativeWithLargeDt) {
  FuelBurnMassPropertiesParams p;
  FuelBurnMassPropertiesState s;
  // Single huge dt that would overshoot fuel_kg by 10x.
  stepFuelBurn(s, p, /*T*/ 1.0e6, /*dt*/ 100000.0);
  EXPECT_GE(s.fuel_kg, 0.0);
  EXPECT_DOUBLE_EQ(s.fuel_kg, 0.0);
}

/* ----------------------------- CG migration ----------------------------- */

TEST(FuelBurnTest, CGMigratesLinearlyWithFuelFraction) {
  FuelBurnMassPropertiesParams p;
  // Distinct full / empty CGs so we can detect the interpolation.
  p.cg_full_m = Vec3{0.0, 0.0, 0.0};
  p.cg_empty_m = Vec3{1.0, 0.0, 0.0}; // 1 m forward shift when empty
  FuelBurnMassPropertiesState s;      // starts at full

  // Full fuel (frac=1): CG = cg_full = (0, 0, 0)
  auto r0 = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0); // no burn
  EXPECT_NEAR(r0.cg_offset_m.x, 0.0, 1e-12);

  // Burn down to half fuel.
  s.fuel_kg = 0.5 * p.fuel_capacity_kg;
  auto r_half = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0);
  EXPECT_NEAR(r_half.cg_offset_m.x, 0.5, 1e-12); // halfway between 0 and 1

  // Empty.
  s.fuel_kg = 0.0;
  auto r_empty = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0);
  EXPECT_NEAR(r_empty.cg_offset_m.x, 1.0, 1e-12);
}

/* ----------------------------- Inertia scaling ----------------------------- */

TEST(FuelBurnTest, InertiaScalesLinearlyWithFuelFraction) {
  FuelBurnMassPropertiesParams p;
  // Distinct full / empty tensors so the interpolation is observable.
  p.I_full = sim::dynamics::rigid_body::InertiaTensor{100.0, 200.0, 300.0, 0.0};
  p.I_empty = sim::dynamics::rigid_body::InertiaTensor{10.0, 20.0, 30.0, 0.0};
  FuelBurnMassPropertiesState s; // starts full

  // Full fuel (default state): I = I_full
  auto r0 = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0);
  EXPECT_NEAR(r0.I_body.Ixx, p.I_full.Ixx, 1e-6);
  EXPECT_NEAR(r0.I_body.Iyy, p.I_full.Iyy, 1e-6);

  // Empty
  s.fuel_kg = 0.0;
  auto r_empty = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0);
  EXPECT_NEAR(r_empty.I_body.Ixx, p.I_empty.Ixx, 1e-6);
  EXPECT_NEAR(r_empty.I_body.Iyy, p.I_empty.Iyy, 1e-6);

  // Half fuel
  s.fuel_kg = 0.5 * p.fuel_capacity_kg;
  auto r_half = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0);
  EXPECT_NEAR(r_half.I_body.Ixx, 0.5 * (p.I_full.Ixx + p.I_empty.Ixx), 1e-6);
  EXPECT_NEAR(r_half.I_body.Iyy, 0.5 * (p.I_full.Iyy + p.I_empty.Iyy), 1e-6);
}

/* ----------------------------- m = m_empty + fuel invariant ----------------------------- */

TEST(FuelBurnTest, TotalMassEqualsEmptyPlusFuel) {
  FuelBurnMassPropertiesParams p;
  p.m_empty_kg = 5000.0;
  p.fuel_capacity_kg = 2000.0;
  FuelBurnMassPropertiesState s;

  for (double burn_amount : {0.0, 500.0, 1500.0, 2000.0}) {
    s.fuel_kg = p.fuel_capacity_kg - burn_amount;
    if (s.fuel_kg < 0.0)
      s.fuel_kg = 0.0;
    auto r = stepFuelBurn(s, p, /*T*/ 0.0, /*dt*/ 0.0);
    EXPECT_NEAR(r.m_total_kg, p.m_empty_kg + s.fuel_kg, 1e-9);
  }
}
