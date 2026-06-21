/**
 * @file Vehicle_pTest.cpp
 * @brief Performance test for the vehicle-level composed 6-DOF step.
 *
 * Measures the end-to-end per-tick cost through the compositional facade:
 * re-stack the mass, re-stack the forces about the current CG, then take one
 * 6-DOF RK4 step from the two aggregates (one tick per lambda; per-op time
 * derived from callsPerSecond).
 *
 * Usage:
 *   ./SimDynamicsVehicle_PTEST --gtest_list_tests
 *   ./SimDynamicsVehicle_PTEST --profile perf --gtest_filter="*VehicleStep*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/dynamics/force_moment/inc/ForceMoment.hpp"
#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp"
#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"
#include "src/sim/dynamics/vehicle/inc/VehicleStep.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

namespace ub = vernier::bench;

using sim::dynamics::force_moment::AppliedForce;
using sim::dynamics::force_moment::ForceMoment;
using sim::dynamics::force_moment::ForceMomentAccumulator;
using sim::dynamics::mass_properties::AggregateMassProperties;
using sim::dynamics::mass_properties::FuelTankMassSource;
using sim::dynamics::mass_properties::MassAccumulator;
using sim::dynamics::mass_properties::MassContributor;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::RigidBody6DOFState;
using sim::dynamics::rigid_body::Vec3;

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

  auto stepTick = [&](RigidBody6DOFState& s, double t) {
    const AggregateMassProperties mp = mass.result();
    const ForceMoment fm = forces.resultAbout(mp.cg_m);
    sim::dynamics::vehicle::step(s, mp, fm, t, dt);
  };

  perf.warmup([&] {
    RigidBody6DOFState s;
    for (int i = 0; i < perf.cycles(); ++i) {
      stepTick(s, i * dt);
    }
  });

  RigidBody6DOFState s;
  auto result = perf.throughputLoop(
      [&, t = 0.0]() mutable {
        stepTick(s, t);
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
