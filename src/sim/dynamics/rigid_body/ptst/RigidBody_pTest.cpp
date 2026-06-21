/**
 * @file RigidBody_pTest.cpp
 * @brief Performance tests for the rigid-body step primitives.
 *
 * RT-path: the flight loop integrates one of these every tick. Step
 * throughput sets the per-vehicle cost of the rigid-body stage (one step per
 * lambda; per-op time derived from callsPerSecond).
 *
 *  - PointMass3D step   (6-state translational, RK4)
 *  - RigidBody6DOF step (13-state: quaternion rate + Euler equations +
 *                        inertia solve + renormalize -- the heaviest)
 *
 * Usage:
 *   ./SimDynamicsRigidBody_PTEST --gtest_list_tests
 *   ./SimDynamicsRigidBody_PTEST --profile perf --gtest_filter="*RigidBody6DOF*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;

using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::PointMass3DState;
using sim::dynamics::rigid_body::RigidBody6DOFState;
using sim::dynamics::rigid_body::stepPointMass3D;
using sim::dynamics::rigid_body::stepRigidBody6DOF;
using sim::dynamics::rigid_body::Vec3;

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

PERF_MAIN()
