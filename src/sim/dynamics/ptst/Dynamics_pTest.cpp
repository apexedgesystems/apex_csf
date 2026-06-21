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
#include "src/sim/dynamics/force_moment/inc/ForceMoment.hpp"
#include "src/sim/dynamics/inc/VehicleStep.hpp"
#include "src/sim/dynamics/integrators/inc/RK4.hpp"
#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp"
#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"
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
using sim::dynamics::force_moment::AppliedForce;
using sim::dynamics::force_moment::ForceMoment;
using sim::dynamics::force_moment::ForceMomentAccumulator;
using sim::dynamics::integrators::stepRK4;
using sim::dynamics::mass_properties::AggregateMassProperties;
using sim::dynamics::mass_properties::FuelTankMassSource;
using sim::dynamics::mass_properties::MassAccumulator;
using sim::dynamics::mass_properties::MassContributor;
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

  const double thrust = 290000.0;
  const double dt = 0.02;

  perf.warmup([&] {
    FuelTankMassSource tank;
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)tank.step(thrust, dt);
    }
  });

  volatile double sink = 0.0;
  FuelTankMassSource tank;
  auto result = perf.throughputLoop(
      [&] {
        tank.step(thrust, dt);
        sink += tank.fuel_kg;
        if (tank.fuel_kg <= 0.0) {
          tank = FuelTankMassSource{}; // refuel so the workload is steady
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

/* ----------------------- Compositional accumulation ----------------------- */

// A representative small vehicle's mass build-up: dry structure plus four
// offset contributors (two engines, payload, a draining fuel tank). The
// per-tick cost is one `result()` -- re-sample the sources and run the
// parallel-axis combine to whole-vehicle mass / CG / inertia. The fuel tank
// is a live source so the stack genuinely re-evaluates each tick.
PERF_TEST(MassAccumulatorResult, Throughput) {
  UB_PERF_GUARD(perf);

  FuelTankMassSource tank;
  tank.params.cg_tank_m = Vec3{-2.0, 0.0, 0.5};

  const MassContributor structure{8000.0, Vec3{0.5, 0.0, 0.0},
                                  InertiaTensor{4.0e4, 6.0e4, 8.0e4, 0.0}};
  const MassContributor engineL{1200.0, Vec3{-3.0, -4.0, 1.0},
                                InertiaTensor{300.0, 300.0, 200.0, 0.0}};
  const MassContributor engineR{1200.0, Vec3{-3.0, 4.0, 1.0},
                                InertiaTensor{300.0, 300.0, 200.0, 0.0}};
  const MassContributor payload{2500.0, Vec3{1.5, 0.0, 0.2},
                                InertiaTensor{5.0e3, 5.0e3, 5.0e3, 0.0}};

  auto build = [&] {
    MassAccumulator acc;
    acc.add(structure);
    acc.add(engineL);
    acc.add(engineR);
    acc.add(payload);
    acc.add(tank);
    return acc;
  };

  perf.warmup([&] {
    auto acc = build();
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto mp = acc.result();
      (void)mp;
    }
  });

  volatile double sink = 0.0;
  auto acc = build();
  auto result = perf.throughputLoop(
      [&] {
        const AggregateMassProperties mp = acc.result();
        sink += mp.mass_kg + mp.cg_m.x + mp.inertia_about_cg.Ixx;
      },
      "mass_accumulate");

  std::printf("\n[MassAccumulator] result (5 parts): %.0f stacks/s (%.4f us/stack)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

// The force side of a tick: net force + moment about the CG from five applied
// loads (gravity, aero at the aerodynamic center, two engine thrusts at their
// mount points, a control-surface couple). Each `resultAbout()` runs the
// force analogue of parallel-axis -- moment transfer of every offset force.
PERF_TEST(ForceMomentAccumulatorResult, Throughput) {
  UB_PERF_GUARD(perf);

  const Vec3 cg{0.3, 0.0, 0.1};
  const AppliedForce gravity{Vec3{0.0, 0.0, 1.3e5}, cg, Vec3{}};
  const AppliedForce aero{Vec3{-4.0e4, 0.0, -2.0e5}, Vec3{0.6, 0.0, 0.0}, Vec3{}};
  const AppliedForce thrustL{Vec3{6.0e4, 0.0, 0.0}, Vec3{-3.0, -4.0, 1.0}, Vec3{}};
  const AppliedForce thrustR{Vec3{6.0e4, 0.0, 0.0}, Vec3{-3.0, 4.0, 1.0}, Vec3{}};
  const AppliedForce surface{Vec3{}, Vec3{}, Vec3{0.0, 1.5e4, 0.0}}; // pure couple

  auto build = [&] {
    ForceMomentAccumulator acc;
    acc.add(gravity);
    acc.add(aero);
    acc.add(thrustL);
    acc.add(thrustR);
    acc.add(surface);
    return acc;
  };

  perf.warmup([&] {
    auto acc = build();
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto fm = acc.resultAbout(cg);
      (void)fm;
    }
  });

  volatile double sink = 0.0;
  auto acc = build();
  auto result = perf.throughputLoop(
      [&] {
        const ForceMoment fm = acc.resultAbout(cg);
        sink += fm.force.x + fm.moment.y;
      },
      "force_moment_accumulate");

  std::printf("\n[ForceMomentAccumulator] resultAbout (5 loads): %.0f stacks/s (%.4f us/stack)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

// The end-to-end per-tick path through the compositional facade: re-stack the
// mass, re-stack the forces about the current CG, then take one 6-DOF RK4 step
// from the two aggregates. The vehicle is wired once (the intended usage); the
// tick re-samples the sources, so this measures the steady-state per-tick cost
// of the stitch-and-stack path, which is allocation-free.
PERF_TEST(VehicleStepComposed, Throughput) {
  UB_PERF_GUARD(perf);

  FuelTankMassSource tank;
  tank.params.cg_tank_m = Vec3{-2.0, 0.0, 0.5};
  const MassContributor structure{8000.0, Vec3{0.5, 0.0, 0.0},
                                  InertiaTensor{4.0e4, 6.0e4, 8.0e4, 0.0}};
  const MassContributor payload{2500.0, Vec3{1.5, 0.0, 0.2},
                                InertiaTensor{5.0e3, 5.0e3, 5.0e3, 0.0}};

  const AppliedForce gravity{Vec3{0.0, 0.0, 1.3e5}, Vec3{}, Vec3{}};
  const AppliedForce thrust{Vec3{1.2e5, 0.0, 0.0}, Vec3{-3.0, 0.0, 1.0}, Vec3{}};
  const AppliedForce aero{Vec3{-4.0e4, 0.0, -2.0e5}, Vec3{0.6, 0.0, 0.0}, Vec3{}};

  const double dt = 0.01;

  // Wire the vehicle once; the fuel tank is a live source the tick re-samples.
  MassAccumulator mass;
  mass.add(structure);
  mass.add(payload);
  mass.add(tank);

  ForceMomentAccumulator forces;
  forces.add(gravity);
  forces.add(thrust);
  forces.add(aero);

  auto step = [&](RigidBody6DOFState& s, double t) {
    const AggregateMassProperties mp = mass.result();
    const ForceMoment fm = forces.resultAbout(mp.cg_m);
    stepRigidBody6DOF(s, mp, fm, t, dt);
  };

  perf.warmup([&] {
    RigidBody6DOFState s;
    for (int i = 0; i < perf.cycles(); ++i) {
      step(s, i * dt);
    }
  });

  RigidBody6DOFState s;
  auto result = perf.throughputLoop(
      [&, t = 0.0]() mutable {
        step(s, t);
        t += dt;
        if (!std::isfinite(s.position_inertial.x)) {
          s = RigidBody6DOFState{};
          t = 0.0;
        }
      },
      "vehicle_step_composed");

  std::printf("\n[VehicleStep] composed tick (stack mass+forces, 6DOF step): "
              "%.0f ticks/s (%.4f us/tick)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
