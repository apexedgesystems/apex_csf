/**
 * @file Dynamics_pTest.cpp
 * @brief Performance tests for the dynamics step primitives.
 *
 * Every benchmark here is an RT-path operation: the flight loop calls one of
 * these step functions on every tick (typically 50-100 Hz). Step throughput
 * is therefore the meaningful metric -- it sets the per-vehicle cost of the
 * integration stage of a frame.
 *
 * Measures (one step per lambda, per-op time derived from callsPerSecond):
 *  - RK4 scalar step           (4 derivative evaluations, the integrator core)
 *  - PointMass3D step          (6-state translational, RK4)
 *  - RigidBody6DOF step        (13-state: quaternion rate + Euler equations +
 *                               inertia solve + renormalize -- the heaviest)
 *  - FuelBurn step             (mass / CG / inertia update)
 *  - Dryden step               (3-axis PSD-shaped white-noise filter + RNG)
 *
 * Usage:
 *   ./SimDynamics_PTEST --gtest_list_tests
 *   ./SimDynamics_PTEST --quick --gtest_filter="*RigidBody6DOF*"
 *   ./SimDynamics_PTEST --profile perf --gtest_filter="*RK4*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/dynamics/disturbance/inc/DrydenTurbulence.hpp"
#include "src/sim/dynamics/integrators/inc/RK4.hpp"
#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

namespace ub = vernier::bench;

using sim::dynamics::disturbance::DrydenRng;
using sim::dynamics::disturbance::DrydenTurbulenceParams;
using sim::dynamics::disturbance::DrydenTurbulenceState;
using sim::dynamics::disturbance::stepDryden;
using sim::dynamics::integrators::stepRK4;
using sim::dynamics::mass_properties::FuelBurnMassPropertiesParams;
using sim::dynamics::mass_properties::FuelBurnMassPropertiesState;
using sim::dynamics::mass_properties::stepFuelBurn;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::PointMass3DState;
using sim::dynamics::rigid_body::RigidBody6DOFState;
using sim::dynamics::rigid_body::stepPointMass3D;
using sim::dynamics::rigid_body::stepRigidBody6DOF;
using sim::dynamics::rigid_body::Vec3;

namespace {

/// Minimal scalar state with the operators the integrators require.
struct Scalar {
  double v = 0.0;
  Scalar operator+(const Scalar& o) const { return {v + o.v}; }
  Scalar operator*(double k) const { return {v * k}; }
};

} // namespace

/* ----------------------------- RK4 core ----------------------------- */

// The integrator kernel by itself, with a cheap analytic derivative so the
// measured cost is the four-stage RK4 machinery, not the derivative.
PERF_TEST(RK4Step, ScalarThroughput) {
  UB_PERF_GUARD(perf);

  auto deriv = [](double t, Scalar) { return Scalar{std::cos(t)}; };
  const double dt = 0.01;

  perf.warmup([&] {
    Scalar y{0.0};
    for (int i = 0; i < perf.cycles(); ++i) {
      stepRK4(y, deriv, i * dt, dt);
    }
  });

  volatile double sink = 0.0;
  double t = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        Scalar y{1.0};
        stepRK4(y, deriv, t, dt);
        t += dt;
        sink += y.v;
      },
      "rk4_scalar_step");

  std::printf("\n[RK4] scalar step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- PointMass3D ----------------------------- */

PERF_TEST(PointMass3DStep, Throughput) {
  UB_PERF_GUARD(perf);

  auto force = [](double, const PointMass3DState&) { return Vec3{4500.0, 0.0, -9806.65}; };
  const double mass = 1500.0;
  const double dt = 0.01;

  perf.warmup([&] {
    PointMass3DState s;
    for (int i = 0; i < perf.cycles(); ++i) {
      stepPointMass3D(s, force, mass, i * dt, dt);
    }
  });

  volatile double sink = 0.0;
  PointMass3DState s;
  double t = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        stepPointMass3D(s, force, mass, t, dt);
        t += dt;
        sink += s.position.x;
      },
      "pointmass3d_step");

  std::printf("\n[PointMass3D] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- RigidBody6DOF ----------------------------- */

// The dominant RT cost: RK4 over 13 states, with a quaternion rate, the
// transport-theorem velocity term, Euler's equation (inertia solve), and a
// renormalize -- evaluated four times per step.
PERF_TEST(RigidBody6DOFStep, Throughput) {
  UB_PERF_GUARD(perf);

  auto force = [](double, const RigidBody6DOFState&) { return Vec3{1000.0, 0.0, 0.0}; };
  auto moment = [](double, const RigidBody6DOFState&) { return Vec3{10.0, 5.0, 2.0}; };
  const InertiaTensor I{24675886.0, 44877562.0, 67384138.0, 1315143.0};
  const double mass = 300000.0;
  const double dt = 0.02; // 50 Hz aircraft tick

  perf.warmup([&] {
    RigidBody6DOFState s;
    s.velocity_body = Vec3{235.0, 0.0, 0.0};
    for (int i = 0; i < perf.cycles(); ++i) {
      stepRigidBody6DOF(s, force, moment, mass, I, i * dt, dt);
    }
  });

  volatile double sink = 0.0;
  RigidBody6DOFState s;
  s.velocity_body = Vec3{235.0, 0.0, 0.0};
  double t = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        stepRigidBody6DOF(s, force, moment, mass, I, t, dt);
        t += dt;
        sink += s.attitude.w;
        // Keep the trajectory bounded over a long --repeats run: reset once
        // the attitude has tumbled far from identity so the state never
        // grows unbounded (and stays representative of a real flight step).
        if (t > 1000.0) {
          s = RigidBody6DOFState{};
          s.velocity_body = Vec3{235.0, 0.0, 0.0};
          t = 0.0;
        }
      },
      "rigidbody6dof_step");

  std::printf("\n[RigidBody6DOF] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- FuelBurn ----------------------------- */

PERF_TEST(FuelBurnStep, Throughput) {
  UB_PERF_GUARD(perf);

  FuelBurnMassPropertiesParams p;
  const double thrust = 290000.0;
  const double dt = 0.02;

  perf.warmup([&] {
    FuelBurnMassPropertiesState s;
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)stepFuelBurn(s, p, thrust, dt);
    }
  });

  volatile double sink = 0.0;
  FuelBurnMassPropertiesState s;
  auto result = perf.throughputLoop(
      [&] {
        const auto r = stepFuelBurn(s, p, thrust, dt);
        sink += r.m_total_kg;
        if (s.fuel_kg <= 0.0) {
          s = FuelBurnMassPropertiesState{}; // refuel so the workload is steady
        }
      },
      "fuelburn_step");

  std::printf("\n[FuelBurn] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- Dryden ----------------------------- */

// Includes the RNG draw (three normal samples per step) -- that is the bulk
// of the cost and is part of the real per-tick disturbance evaluation.
PERF_TEST(DrydenStep, Throughput) {
  UB_PERF_GUARD(perf);

  DrydenTurbulenceParams p;
  DrydenRng rng(42u);
  const double V = 235.0;
  const double dt = 0.02;

  perf.warmup([&] {
    DrydenTurbulenceState s;
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)stepDryden(s, p, rng, V, dt);
    }
  });

  volatile double sink = 0.0;
  DrydenTurbulenceState s;
  auto result = perf.throughputLoop(
      [&] {
        const auto r = stepDryden(s, p, rng, V, dt);
        sink += r.u_g_m_s + r.v_g_m_s + r.w_g_m_s;
      },
      "dryden_step");

  std::printf("\n[Dryden] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
