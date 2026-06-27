/**
 * @file AeroModel_uTest.cpp
 * @brief Tests for the AeroModel fidelity ladder + the wrench contributor.
 *
 * Each rung is checked for consistency with the underlying evaluator, and the
 * AeroWrenchSource is checked for its WrenchKind tag, its null-input guard, and
 * that it stacks correctly into the dynamics WrenchAccumulator.
 */

#include "src/sim/aerodynamics/inc/AeroModel.hpp"

#include <cmath>
#include <memory>

#include <gtest/gtest.h>

using sim::aerodynamics::AeroWrenchSource;
using sim::aerodynamics::ConstantAero;
using sim::aerodynamics::ControlInputs;
using sim::aerodynamics::evaluatePolar;
using sim::aerodynamics::evaluateStabilityDerivative;
using sim::aerodynamics::PolarAeroModel;
using sim::aerodynamics::StabilityDerivativeModel;
using sim::aerodynamics::windToBodyForces;
using sim::dynamics::rigid_body::Vec3;
using sim::dynamics::wrench::AppliedWrench;
using sim::dynamics::wrench::Wrench;
using sim::dynamics::wrench::WrenchAccumulator;
using sim::dynamics::wrench::WrenchKind;

namespace {
constexpr double kTol = 1e-12;
} // namespace

/* ----------------------------- ConstantAero ----------------------------- */

/** @test ConstantAero returns its stored wrench regardless of state. */
TEST(AeroModelTest, ConstantAeroReturnsFixedWrench) {
  ConstantAero m;
  m.wrench_body.force = Vec3{10.0, 0.0, -20.0};
  m.wrench_body.moment = Vec3{1.0, 2.0, 3.0};

  const auto a = m.aeroWrench(Vec3{200.0, 0.0, 5.0}, Vec3{0.1, 0.0, 0.0}, ControlInputs{}, 0.5);
  EXPECT_NEAR(a.force.x, 10.0, kTol);
  EXPECT_NEAR(a.force.z, -20.0, kTol);
  EXPECT_NEAR(a.moment.y, 2.0, kTol);
}

/* ----------------------------- PolarAeroModel ----------------------------- */

/** @test PolarAeroModel force equals the wind->body rotation of evaluatePolar's L/D. */
TEST(AeroModelTest, PolarModelMatchesEvaluatePolar) {
  PolarAeroModel m; // illustrative defaults
  const double V = 240.0;
  const double alpha = 0.04;
  const Vec3 v_body{V * std::cos(alpha), 0.0, V * std::sin(alpha)};
  const double rho = 0.4135;

  const auto a = m.aeroWrench(v_body, Vec3{}, ControlInputs{}, rho);

  const auto r = evaluatePolar(m.params, alpha, rho, V);
  const Vec3 expected = windToBodyForces(r.L_N, r.D_N, 0.0, alpha, 0.0);
  EXPECT_NEAR(a.force.x, expected.x, 1e-6);
  EXPECT_NEAR(a.force.y, expected.y, 1e-9);
  EXPECT_NEAR(a.force.z, expected.z, 1e-6);
  // The polar carries no moment.
  EXPECT_NEAR(a.moment.x, 0.0, kTol);
  EXPECT_NEAR(a.moment.y, 0.0, kTol);
  EXPECT_NEAR(a.moment.z, 0.0, kTol);
}

/** @test PolarAeroModel returns zero below the 1 m/s floor. */
TEST(AeroModelTest, PolarModelZeroBelowFloor) {
  PolarAeroModel m;
  const auto a = m.aeroWrench(Vec3{0.5, 0.0, 0.0}, Vec3{}, ControlInputs{}, 0.4135);
  EXPECT_NEAR(a.force.x, 0.0, kTol);
  EXPECT_NEAR(a.force.z, 0.0, kTol);
}

/** @test A model owned through the AeroModel base destructs cleanly. */
TEST(AeroModelTest, ModelDestructsThroughBasePointer) {
  std::unique_ptr<sim::aerodynamics::AeroModel> m = std::make_unique<PolarAeroModel>();
  m.reset(); // exercises ~AeroModel through the base pointer
  EXPECT_EQ(m, nullptr);
}

/* ----------------------------- StabilityDerivativeModel ----------------------------- */

/** @test StabilityDerivativeModel mirrors evaluateStabilityDerivative's body force/moment. */
TEST(AeroModelTest, StabilityDerivativeModelMatchesEvaluator) {
  StabilityDerivativeModel m; // illustrative defaults
  const Vec3 v_body{240.0, 2.0, 8.0};
  const Vec3 w_body{0.02, 0.01, 0.015};
  ControlInputs d;
  d.elevator_rad = 0.02;
  d.aileron_rad = 0.01;
  d.rudder_rad = 0.005;
  const double rho = 0.5258;

  const auto a = m.aeroWrench(v_body, w_body, d, rho);
  const auto r = evaluateStabilityDerivative(m.params, v_body, w_body, d, rho);

  EXPECT_NEAR(a.force.x, r.force_body.x, kTol);
  EXPECT_NEAR(a.force.y, r.force_body.y, kTol);
  EXPECT_NEAR(a.force.z, r.force_body.z, kTol);
  EXPECT_NEAR(a.moment.x, r.moment_body.x, kTol);
  EXPECT_NEAR(a.moment.y, r.moment_body.y, kTol);
  EXPECT_NEAR(a.moment.z, r.moment_body.z, kTol);
}

/* ----------------------------- AeroWrenchSource ----------------------------- */

/** @test AeroWrenchSource is tagged Aerodynamic and samples its model. */
TEST(AeroWrenchSourceTest, TaggedAerodynamicAndSamplesModel) {
  StabilityDerivativeModel model;
  const Vec3 v_body{240.0, 2.0, 8.0};
  const Vec3 w_body{0.02, 0.01, 0.015};
  const ControlInputs d{};
  const double rho = 0.5258;

  AeroWrenchSource src;
  src.model = &model;
  src.v_body = &v_body;
  src.w_body = &w_body;
  src.controls = &d;
  src.rho_kg_m3 = &rho;

  EXPECT_EQ(src.kind, WrenchKind::Aerodynamic);

  const auto a = src.current();
  const auto m = model.aeroWrench(v_body, w_body, d, rho);
  EXPECT_NEAR(a.force.x, m.force.x, kTol);
  EXPECT_NEAR(a.moment.z, m.moment.z, kTol);
}

/** @test An unwired AeroWrenchSource contributes nothing. */
TEST(AeroWrenchSourceTest, UnwiredContributesZero) {
  AeroWrenchSource src; // no model / inputs wired
  const auto a = src.current();
  EXPECT_NEAR(a.force.x, 0.0, kTol);
  EXPECT_NEAR(a.force.y, 0.0, kTol);
  EXPECT_NEAR(a.force.z, 0.0, kTol);
  EXPECT_NEAR(a.moment.x, 0.0, kTol);
}

/** @test The aero source stacks into a WrenchAccumulator like any contributor. */
TEST(AeroWrenchSourceTest, StacksIntoWrenchAccumulator) {
  StabilityDerivativeModel model;
  const Vec3 v_body{240.0, 0.0, 8.0};
  const Vec3 w_body{};
  const ControlInputs d{};
  const double rho = 0.5258;

  AeroWrenchSource aero;
  aero.model = &model;
  aero.v_body = &v_body;
  aero.w_body = &w_body;
  aero.controls = &d;
  aero.rho_kg_m3 = &rho;

  WrenchAccumulator acc;
  acc.add(aero);
  const Wrench net = acc.resultAbout(Vec3{});

  // About the origin (aero force at origin), the net equals the model's wrench.
  const auto m = model.aeroWrench(v_body, w_body, d, rho);
  EXPECT_NEAR(net.force.x, m.force.x, 1e-9);
  EXPECT_NEAR(net.force.z, m.force.z, 1e-9);
  EXPECT_NEAR(net.moment.y, m.moment.y, 1e-9);
}
