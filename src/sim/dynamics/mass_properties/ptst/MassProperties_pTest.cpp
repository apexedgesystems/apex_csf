/**
 * @file MassProperties_pTest.cpp
 * @brief Performance tests for the mass-properties RT path.
 *
 * RT-path: a vehicle re-samples its mass each tick. Throughput sets the
 * per-tick cost of the fuel-burn update and the whole-body mass stack (one op
 * per lambda; per-op time derived from callsPerSecond).
 *
 *  - FuelBurn step          (mass / CG / inertia update)
 *  - MassAccumulator result (parallel-axis combine over a five-part vehicle)
 *
 * Usage:
 *   ./SimDynamicsMassProperties_PTEST --gtest_list_tests
 *   ./SimDynamicsMassProperties_PTEST --profile perf --gtest_filter="*MassAccumulator*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/dynamics/mass_properties/inc/FuelBurnMassProperties.hpp"
#include "src/sim/dynamics/mass_properties/inc/MassProperties.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"   // Vec3
#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp" // InertiaTensor

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;

using sim::dynamics::mass_properties::AggregateMassProperties;
using sim::dynamics::mass_properties::FuelTankMassSource;
using sim::dynamics::mass_properties::MassAccumulator;
using sim::dynamics::mass_properties::MassContributor;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::Vec3;

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

PERF_MAIN()
