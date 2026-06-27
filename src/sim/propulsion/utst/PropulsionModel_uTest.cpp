/**
 * @file PropulsionModel_uTest.cpp
 * @brief Tests for the PropulsionModel ladder + the PropulsionSystem linker.
 *
 * The rungs are checked against their evaluators; PropulsionSystem is checked
 * for its WrenchKind tag, engine-count scaling, the thrust->fuel coupling (the
 * mass link), the gyroscopic moment, and stacking into a WrenchAccumulator.
 */

#include "src/sim/propulsion/inc/PropulsionModel.hpp"

#include <cmath>
#include <memory>

#include <gtest/gtest.h>

using sim::dynamics::mass_properties::FuelTankMassSource;
using sim::dynamics::rigid_body::Vec3;
using sim::dynamics::wrench::Wrench;
using sim::dynamics::wrench::WrenchAccumulator;
using sim::dynamics::wrench::WrenchKind;
using sim::propulsion::ConstantThrust;
using sim::propulsion::DensityScaledThrustModel;
using sim::propulsion::evaluateThrust;
using sim::propulsion::PropulsionSystem;
using sim::propulsion::stepTurbofan2Spool;
using sim::propulsion::Turbofan2SpoolModel;
using sim::propulsion::Turbofan2SpoolParams;
using sim::propulsion::Turbofan2SpoolState;

namespace {
constexpr double kTol = 1e-12;
} // namespace

/* ----------------------------- Ladder rungs ----------------------------- */

/** @test ConstantThrust returns its fixed thrust, zero rotor momentum. */
TEST(PropulsionModelTest, ConstantThrustReturnsFixed) {
  ConstantThrust m;
  m.thrust_N = 50000.0;
  const auto o = m.step(0.3, 0.4, 0.02);
  EXPECT_NEAR(o.thrust_N, 50000.0, kTol);
  EXPECT_NEAR(o.H_rotor_kgm2_s, 0.0, kTol);
}

/** @test DensityScaledThrustModel mirrors evaluateThrust. */
TEST(PropulsionModelTest, DensityScaledModelMatchesEvaluator) {
  DensityScaledThrustModel m;
  const double rho = 0.4135;
  const auto o = m.step(0.8, rho, 0.02);
  EXPECT_NEAR(o.thrust_N, evaluateThrust(m.params, 0.8, rho), kTol);
}

/** @test Turbofan2SpoolModel advances state and mirrors stepTurbofan2Spool. */
TEST(PropulsionModelTest, Turbofan2SpoolModelMatchesEvaluatorAndAdvances) {
  Turbofan2SpoolModel m;
  // Reference engine stepped the same way.
  Turbofan2SpoolParams p;
  Turbofan2SpoolState ref;
  const double rho = 0.4135;
  const double dt = 0.02;

  const auto o = m.step(0.9, rho, dt);
  const auto r = stepTurbofan2Spool(ref, p, 0.9, rho, dt);

  EXPECT_NEAR(o.thrust_N, r.thrust_N, kTol);
  EXPECT_NEAR(o.H_rotor_kgm2_s, r.H_rotor_kgm2_s, kTol);
  EXPECT_NEAR(m.state.N1_pct, ref.N1_pct, kTol); // state advanced
}

/* ----------------------------- PropulsionSystem: wrench face ----------------------------- */

/** @test PropulsionSystem is tagged Propulsion and scales thrust by engine_count. */
TEST(PropulsionSystemTest, TaggedPropulsionAndScalesByEngineCount) {
  ConstantThrust m;
  m.thrust_N = 100000.0;
  PropulsionSystem sys;
  sys.model = &m;
  sys.engine_count = 4;

  EXPECT_EQ(sys.kind, WrenchKind::Propulsion);

  sys.step(1.0, 1.225, 0.02);
  const auto w = sys.current();
  // Thrust along default body +x, 4 engines.
  EXPECT_NEAR(w.force.x, 4.0 * 100000.0, 1e-6);
  EXPECT_NEAR(w.force.y, 0.0, kTol);
  EXPECT_NEAR(w.force.z, 0.0, kTol);
}

/** @test An unwired PropulsionSystem (no model) contributes nothing. */
TEST(PropulsionSystemTest, UnwiredContributesZero) {
  PropulsionSystem sys;
  sys.step(1.0, 1.225, 0.02);
  const auto w = sys.current();
  EXPECT_NEAR(w.force.x, 0.0, kTol);
  EXPECT_NEAR(w.moment.x, 0.0, kTol);
}

/* ----------------------------- The mass link: thrust drives fuel ----------------------------- */

/** @test step() burns fuel from the produced thrust (the wrench<->mass link). */
TEST(PropulsionSystemTest, ThrustDrivesFuelBurn) {
  ConstantThrust m;
  m.thrust_N = 1.0e5;
  FuelTankMassSource tank; // default fuel_kg = 1000, TSFC = 2.0e-5
  const double fuel_before = tank.fuel_kg;

  PropulsionSystem sys;
  sys.model = &m;
  sys.fuel = &tank;
  sys.engine_count = 1;

  const double dt = 0.1;
  sys.step(1.0, 1.225, dt);

  // mdot = TSFC * thrust_total = 2.0e-5 * 1e5 = 2 kg/s; burned = 0.2 kg.
  const double expected_burn = tank.params.TSFC_kg_per_N_s * 1.0e5 * dt;
  EXPECT_NEAR(fuel_before - tank.fuel_kg, expected_burn, 1e-9);
  EXPECT_NEAR(expected_burn, 0.2, 1e-9);
}

/** @test With no fuel tank wired, step() still produces thrust (no burn). */
TEST(PropulsionSystemTest, NoFuelTankStillThrusts) {
  ConstantThrust m;
  m.thrust_N = 1.0e5;
  PropulsionSystem sys;
  sys.model = &m; // fuel == nullptr
  sys.step(1.0, 1.225, 0.1);
  EXPECT_NEAR(sys.current().force.x, 1.0e5, 1e-6);
}

/* ----------------------------- Gyroscopic moment ----------------------------- */

/** @test The gyroscopic moment is H x omega_body (H along the spin axis). */
TEST(PropulsionSystemTest, GyroscopicMomentIsHCrossOmega) {
  Turbofan2SpoolModel m;
  const Vec3 omega{0.1, 0.2, 0.3};

  PropulsionSystem sys;
  sys.model = &m;
  sys.omega_body = &omega;
  sys.engine_count = 2;

  sys.step(1.0, 1.225, 0.02);
  const auto w = sys.current();

  // H = {H_total, 0, 0} along body x; cross({H,0,0}, {wx,wy,wz}) = {0, -H*wz, H*wy}.
  const double H = sys.H_rotor_total;
  EXPECT_GT(H, 0.0); // turbofan spins
  EXPECT_NEAR(w.moment.x, 0.0, 1e-9);
  EXPECT_NEAR(w.moment.y, -H * omega.z, 1e-6);
  EXPECT_NEAR(w.moment.z, H * omega.y, 1e-6);
}

/** @test No omega_body wired -> no gyroscopic moment. */
TEST(PropulsionSystemTest, NoOmegaNoGyroMoment) {
  Turbofan2SpoolModel m;
  PropulsionSystem sys;
  sys.model = &m; // omega_body == nullptr
  sys.step(1.0, 1.225, 0.02);
  const auto w = sys.current();
  EXPECT_NEAR(w.moment.x, 0.0, kTol);
  EXPECT_NEAR(w.moment.y, 0.0, kTol);
  EXPECT_NEAR(w.moment.z, 0.0, kTol);
}

/* ----------------------------- Accumulator integration ----------------------------- */

/** @test The system stacks into a WrenchAccumulator like any contributor. */
TEST(PropulsionSystemTest, StacksIntoWrenchAccumulator) {
  ConstantThrust m;
  m.thrust_N = 80000.0;
  PropulsionSystem sys;
  sys.model = &m;
  sys.mount_m = Vec3{-5.0, 0.0, 1.0}; // engine aft + below the CG
  sys.step(1.0, 1.225, 0.02);

  WrenchAccumulator acc;
  acc.add(sys);
  const Wrench net = acc.resultAbout(Vec3{});

  // Force is the thrust; an offset mount induces a moment (point - about) x F.
  EXPECT_NEAR(net.force.x, 80000.0, 1e-6);
  // (-5,0,1) x (80000,0,0) = (0*0 - 1*0, 1*80000 - (-5)*0, (-5)*0 - 0*80000) = (0, 80000, 0)
  EXPECT_NEAR(net.moment.y, 80000.0, 1e-3);
}

/* ----------------------------- Lifecycle ----------------------------- */

/** @test A model owned through the PropulsionModel base destructs cleanly. */
TEST(PropulsionModelTest, ModelDestructsThroughBasePointer) {
  std::unique_ptr<sim::propulsion::PropulsionModel> m = std::make_unique<Turbofan2SpoolModel>();
  m.reset(); // exercises ~PropulsionModel through the base pointer
  EXPECT_EQ(m, nullptr);
}
