/**
 * @file FuelBurnMassProperties_uTest.cpp
 * @brief Unit tests for the fuel-tank-as-DynamicMassSource model.
 *
 * Coverage (all tests use explicit, vehicle-agnostic params):
 *   1. Zero / negative thrust -> no fuel burn (clamp, no refill)
 *   2. Constant thrust over dt -> fuel decreases by TSFC*T*dt (exact)
 *   3. Fuel runs out: fuel_kg -> 0, m_dot -> 0, no negative fuel (clamp)
 *   4. current() mass = current fuel, CG = tank location
 *   5. current() inertia scales linearly with fuel fraction
 *   6. Whole-vehicle mass props come from a MassAccumulator of {dry, tank}
 *
 * Proof test 8 (hand-computed, tol 1e-9) lives at the bottom.
 */

#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp"
#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::dynamics::mass_properties::FuelTankMassSource;
using sim::dynamics::mass_properties::MassAccumulator;
using sim::dynamics::mass_properties::StaticMassSource;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::Vec3;

namespace {
constexpr double kTol = 1e-9;
} // namespace

/* ----------------------------- Negative thrust clamp ----------------------------- */

/** @test Negative total thrust is clamped to zero burn (no fuel "regained"). */
TEST(FuelBurnTest, NegativeThrustDoesNotBurnOrRefill) {
  FuelTankMassSource tank;
  const double fuel0 = tank.fuel_kg;

  const double m_dot = tank.step(/*thrust*/ -50000.0, /*dt*/ 1.0);
  EXPECT_DOUBLE_EQ(m_dot, 0.0);
  EXPECT_DOUBLE_EQ(tank.fuel_kg, fuel0); // no burn, and no spurious refill
  EXPECT_DOUBLE_EQ(tank.fuel_burned_total_kg, 0.0);
}

/** @test A zero fuel-capacity config yields a 0 fuel fraction (no divide-by-zero). */
TEST(FuelBurnTest, ZeroCapacityGivesZeroFraction) {
  FuelTankMassSource tank;
  tank.params.fuel_capacity_kg = 0.0; // exercise the capacity-guard else branch
  tank.fuel_kg = 0.0;

  EXPECT_DOUBLE_EQ(tank.fuelFraction(), 0.0);
  const auto c = tank.current();
  EXPECT_TRUE(std::isfinite(c.cg_m.x));
  EXPECT_TRUE(std::isfinite(c.inertia_about_own_cg.Ixx));
}

/* ----------------------------- Zero thrust ----------------------------- */

TEST(FuelBurnTest, ZeroThrustNoBurn) {
  FuelTankMassSource tank;
  const double fuel0 = tank.fuel_kg;

  for (int i = 0; i < 1000; ++i) {
    tank.step(/*thrust*/ 0.0, /*dt*/ 0.1);
  }
  EXPECT_DOUBLE_EQ(tank.fuel_kg, fuel0);
  EXPECT_DOUBLE_EQ(tank.fuel_burned_total_kg, 0.0);
}

/* ----------------------------- TSFC closed-form ----------------------------- */

TEST(FuelBurnTest, ConstantThrustBurnsAtTSFCTimesT) {
  // mdot_fuel = TSFC * T, so over n constant-thrust steps the fuel burned is
  // exactly TSFC * T * (n*dt). Verify the integrator against that closed form.
  FuelTankMassSource tank;
  tank.params.TSFC_kg_per_N_s = 2.0e-5; // pick a concrete number

  const double T_total_N = 200000.0; // N
  const double dt = 0.1;
  const int n_steps = 1000; // 100 sec total

  for (int i = 0; i < n_steps; ++i) {
    tank.step(T_total_N, dt);
  }
  // Expected fuel burned = TSFC * T * t = 2e-5 * 200000 * 100 = 400 kg
  const double expected_burned = tank.params.TSFC_kg_per_N_s * T_total_N * dt * n_steps;
  EXPECT_NEAR(tank.fuel_burned_total_kg, expected_burned, 0.001);
  EXPECT_NEAR(tank.params.fuel_capacity_kg - tank.fuel_kg, expected_burned, 0.001);
}

/* ----------------------------- Endurance closed-form ----------------------------- */

TEST(FuelBurnTest, EnduranceMatchesFuelOverBurnRate) {
  // With explicit, vehicle-agnostic params, time-to-empty should equal
  // fuel_capacity / (TSFC * T) for constant thrust. Verify the model.
  FuelTankMassSource tank;
  tank.params.TSFC_kg_per_N_s = 2.0e-5;
  tank.params.fuel_capacity_kg = 1000.0;
  tank.fuel_kg = tank.params.fuel_capacity_kg;

  const double T_total_N = 100000.0;
  const double dt = 1.0;
  // mdot = 2e-5 * 1e5 = 2.0 kg/s ; endurance = 1000 / 2.0 = 500 s.
  const double expected_s =
      tank.params.fuel_capacity_kg / (tank.params.TSFC_kg_per_N_s * T_total_N);

  int t_to_empty = 0;
  for (int t = 1; t <= 10000; ++t) {
    tank.step(T_total_N, dt);
    if (tank.fuel_kg <= 0.0) {
      t_to_empty = t;
      break;
    }
  }
  EXPECT_NEAR(static_cast<double>(t_to_empty), expected_s, 1.0);
}

/* ----------------------------- Fuel exhaustion ----------------------------- */

TEST(FuelBurnTest, FuelExhaustionStopsBurn) {
  // Drain fuel completely, then verify burn stops + fuel stays 0.
  FuelTankMassSource tank;
  tank.fuel_kg = 100.0; // start with very little fuel

  // Burn lots of fuel fast.
  const double T_total_N = 500000.0;
  const double dt = 1.0;
  for (int i = 0; i < 100; ++i) {
    tank.step(T_total_N, dt);
  }
  EXPECT_DOUBLE_EQ(tank.fuel_kg, 0.0);
  EXPECT_LE(tank.fuel_burned_total_kg, 100.0 + 1e-9); // can't burn more than was there

  // Continue burning -- should be a no-op now.
  const double m_dot = tank.step(T_total_N, dt);
  EXPECT_DOUBLE_EQ(m_dot, 0.0);
  EXPECT_DOUBLE_EQ(tank.fuel_kg, 0.0);
  EXPECT_DOUBLE_EQ(tank.current().mass_kg, 0.0);
}

TEST(FuelBurnTest, FuelCannotGoNegativeWithLargeDt) {
  FuelTankMassSource tank;
  // Single huge dt that would overshoot fuel_kg by 10x.
  tank.step(/*thrust*/ 1.0e6, /*dt*/ 100000.0);
  EXPECT_GE(tank.fuel_kg, 0.0);
  EXPECT_DOUBLE_EQ(tank.fuel_kg, 0.0);
}

/* ----------------------------- Contributor shape ----------------------------- */

TEST(FuelBurnTest, ContributorMassIsCurrentFuelAtTankLocation) {
  FuelTankMassSource tank;
  tank.params.cg_tank_m = Vec3{1.5, -0.25, 0.3};
  tank.fuel_kg = 0.5 * tank.params.fuel_capacity_kg;

  const auto c = tank.current();
  EXPECT_DOUBLE_EQ(c.mass_kg, tank.fuel_kg);
  EXPECT_DOUBLE_EQ(c.cg_m.x, 1.5);
  EXPECT_DOUBLE_EQ(c.cg_m.y, -0.25);
  EXPECT_DOUBLE_EQ(c.cg_m.z, 0.3);
}

/* ----------------------------- Inertia scaling ----------------------------- */

TEST(FuelBurnTest, FuelInertiaScalesLinearlyWithFuelFraction) {
  FuelTankMassSource tank;
  tank.params.I_fuel_full = InertiaTensor{100.0, 200.0, 300.0, 0.0};
  // tank starts full

  // Full fuel (frac = 1): I = I_fuel_full
  auto c_full = tank.current();
  EXPECT_NEAR(c_full.inertia_about_own_cg.Ixx, tank.params.I_fuel_full.Ixx, kTol);
  EXPECT_NEAR(c_full.inertia_about_own_cg.Iyy, tank.params.I_fuel_full.Iyy, kTol);

  // Half fuel
  tank.fuel_kg = 0.5 * tank.params.fuel_capacity_kg;
  auto c_half = tank.current();
  EXPECT_NEAR(c_half.inertia_about_own_cg.Ixx, 0.5 * tank.params.I_fuel_full.Ixx, kTol);
  EXPECT_NEAR(c_half.inertia_about_own_cg.Izz, 0.5 * tank.params.I_fuel_full.Izz, kTol);

  // Empty
  tank.fuel_kg = 0.0;
  auto c_empty = tank.current();
  EXPECT_NEAR(c_empty.inertia_about_own_cg.Ixx, 0.0, kTol);
}

/* ----------------------------- Whole-vehicle via MassAccumulator ----------------------------- */

TEST(FuelBurnTest, WholeVehicleMassPropsFromAggregate) {
  // Dry structure: 1000 kg at origin, own inertia diagonal.
  StaticMassSource dry;
  dry.c.mass_kg = 1000.0;
  dry.c.cg_m = Vec3{0.0, 0.0, 0.0};
  dry.c.inertia_about_own_cg = InertiaTensor{2000.0, 4000.0, 6000.0, 0.0};

  FuelTankMassSource tank;
  tank.params.fuel_capacity_kg = 1000.0;
  tank.params.cg_tank_m = Vec3{2.0, 0.0, 0.0}; // tank 2 m forward of structure CG
  tank.params.I_fuel_full = InertiaTensor{100.0, 200.0, 300.0, 0.0};
  // tank full -> 1000 kg fuel

  MassAccumulator acc;
  acc.add(dry);
  acc.add(tank);
  const auto agg = acc.result();

  // Total mass = 1000 dry + 1000 fuel.
  EXPECT_NEAR(agg.mass_kg, 2000.0, kTol);
  // CG: (1000*0 + 1000*2) / 2000 = 1.0 m forward.
  EXPECT_NEAR(agg.cg_m.x, 1.0, kTol);
  EXPECT_NEAR(agg.cg_m.y, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.z, 0.0, kTol);

  // Izz about net CG: own (6000 + 300) + parallel-axis on x-offsets
  // dry at d.x=-1: 1000*1 = 1000 ; fuel at d.x=+1: 1000*1 = 1000.
  EXPECT_NEAR(agg.inertia_about_cg.Izz, 6000.0 + 300.0 + 2000.0, kTol);
}

/* ----------------------------- PROOF 8: fuel-fraction reflected after burn
 * ----------------------------- */

/**
 * @test Proof 8 (hand-computed, tol 1e-9).
 *
 * Tank: capacity 1000 kg, full (1000 kg), I_fuel_full = {100,200,300,...},
 * cg = (2,0,0). TSFC = 2e-5. Step thrust 100000 N for dt = 100 s:
 *   burned = 2e-5 * 100000 * 100 = 200 kg -> fuel_kg = 800 kg, frac = 0.8.
 *
 * After the burn:
 *   current().mass_kg   == 800
 *   current().inertia   == 0.8 * I_fuel_full  (Ixx=80, Iyy=160, Izz=240)
 * Full vs partially-burned differ exactly by the fraction.
 *
 * Whole vehicle = MassAccumulator of {dry, tank}, hand-computed below.
 */
TEST(FuelBurnProof, FuelFractionReflectedAfterBurnAndWholeVehicle) {
  FuelTankMassSource tank;
  tank.params.TSFC_kg_per_N_s = 2.0e-5;
  tank.params.fuel_capacity_kg = 1000.0;
  tank.params.cg_tank_m = Vec3{2.0, 0.0, 0.0};
  tank.params.I_fuel_full = InertiaTensor{100.0, 200.0, 300.0, 0.0};

  // Snapshot full state before burning.
  const auto c_full = tank.current();
  EXPECT_NEAR(c_full.mass_kg, 1000.0, kTol);
  EXPECT_NEAR(c_full.inertia_about_own_cg.Ixx, 100.0, kTol);
  EXPECT_NEAR(c_full.inertia_about_own_cg.Iyy, 200.0, kTol);
  EXPECT_NEAR(c_full.inertia_about_own_cg.Izz, 300.0, kTol);

  // Burn 200 kg: thrust 100000 N for 100 s.
  tank.step(/*thrust*/ 100000.0, /*dt*/ 100.0);
  EXPECT_NEAR(tank.fuel_kg, 800.0, kTol);
  EXPECT_NEAR(tank.fuelFraction(), 0.8, kTol);

  const auto c_part = tank.current();
  EXPECT_NEAR(c_part.mass_kg, 800.0, kTol);
  EXPECT_NEAR(c_part.inertia_about_own_cg.Ixx, 80.0, kTol);  // 0.8 * 100
  EXPECT_NEAR(c_part.inertia_about_own_cg.Iyy, 160.0, kTol); // 0.8 * 200
  EXPECT_NEAR(c_part.inertia_about_own_cg.Izz, 240.0, kTol); // 0.8 * 300

  // Full vs partial differ exactly by the burned fraction.
  EXPECT_NEAR(c_full.mass_kg - c_part.mass_kg, 200.0, kTol);
  EXPECT_NEAR(c_part.inertia_about_own_cg.Izz, 0.8 * c_full.inertia_about_own_cg.Izz, kTol);

  // Whole vehicle: dry 1000 kg at origin + tank 800 kg at x=2.
  StaticMassSource dry;
  dry.c.mass_kg = 1000.0;
  dry.c.cg_m = Vec3{0.0, 0.0, 0.0};
  dry.c.inertia_about_own_cg = InertiaTensor{2000.0, 4000.0, 6000.0, 0.0};

  MassAccumulator acc;
  acc.add(dry);
  acc.add(tank);
  const auto agg = acc.result();

  // Mass = 1800. CG.x = (1000*0 + 800*2) / 1800 = 1600/1800 = 0.888888...
  EXPECT_NEAR(agg.mass_kg, 1800.0, kTol);
  EXPECT_NEAR(agg.cg_m.x, 1600.0 / 1800.0, kTol);
  EXPECT_NEAR(agg.cg_m.y, 0.0, kTol);
  EXPECT_NEAR(agg.cg_m.z, 0.0, kTol);

  // Izz about net CG:
  //   own: 6000 (dry) + 240 (tank, scaled) = 6240
  //   parallel-axis on x offsets:
  //     dry  at d.x = 0 - 8/9 = -8/9 : 1000 * (8/9)^2
  //     tank at d.x = 2 - 8/9 = 10/9 :  800 * (10/9)^2
  const double cgx = 1600.0 / 1800.0; // = 8/9
  const double dry_dx = 0.0 - cgx;
  const double tank_dx = 2.0 - cgx;
  const double expected_Izz = 6240.0 + 1000.0 * dry_dx * dry_dx + 800.0 * tank_dx * tank_dx;
  EXPECT_NEAR(agg.inertia_about_cg.Izz, expected_Izz, kTol);

  // Equivalence: the source path matches feeding sampled values as fixed
  // contributors to a MassAccumulator.
  MassAccumulator acc_values;
  acc_values.add(dry.current());
  acc_values.add(tank.current());
  const auto agg_values = acc_values.result();
  EXPECT_NEAR(agg.mass_kg, agg_values.mass_kg, kTol);
  EXPECT_NEAR(agg.cg_m.x, agg_values.cg_m.x, kTol);
  EXPECT_NEAR(agg.inertia_about_cg.Izz, agg_values.inertia_about_cg.Izz, kTol);
}
