/**
 * @file Propulsion_pTest.cpp
 * @brief Performance tests for the propulsion models.
 *
 * RT-path: a vehicle evaluates/steps its engines once per tick. Throughput
 * sets the per-tick cost of the propulsion stage (one op per lambda; per-op
 * time derived from callsPerSecond).
 *
 *  - DensityScaledThrust (one pow + a few multiplies)
 *  - Turbofan2Spool      (two first-order spool updates + thrust pow)
 *
 * Usage:
 *   ./SimPropulsion_PTEST --gtest_list_tests
 *   ./SimPropulsion_PTEST --profile perf --gtest_filter="*Turbofan*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/propulsion/inc/DensityScaledThrust.hpp"
#include "src/sim/propulsion/inc/PropulsionModel.hpp"
#include "src/sim/propulsion/inc/Turbofan2Spool.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;

using sim::propulsion::DensityScaledThrustParams;
using sim::propulsion::evaluateThrust;
using sim::propulsion::stepTurbofan2Spool;
using sim::propulsion::Turbofan2SpoolParams;
using sim::propulsion::Turbofan2SpoolState;

/* ----------------------------- DensityScaledThrust ----------------------------- */

PERF_TEST(DensityScaledThrustEval, Throughput) {
  UB_PERF_GUARD(perf);

  const DensityScaledThrustParams p{};
  const double rho = 0.4135;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)evaluateThrust(p, 0.9, rho);
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop([&] { sink += evaluateThrust(p, 0.9, rho); }, "density_thrust");

  std::printf("\n[DensityScaledThrust] evaluate: %.0f evals/s (%.4f us/eval)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

/* ----------------------------- Turbofan2Spool ----------------------------- */

PERF_TEST(Turbofan2SpoolStep, Throughput) {
  UB_PERF_GUARD(perf);

  const Turbofan2SpoolParams p{};
  const double rho = 0.4135;
  const double dt = 0.02;

  perf.warmup([&] {
    Turbofan2SpoolState s;
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)stepTurbofan2Spool(s, p, 0.9, rho, dt);
    }
  });

  volatile double sink = 0.0;
  Turbofan2SpoolState s;
  auto result = perf.throughputLoop(
      [&] {
        const auto r = stepTurbofan2Spool(s, p, 0.9, rho, dt);
        sink += r.thrust_N + r.H_rotor_kgm2_s;
      },
      "turbofan_step");

  std::printf("\n[Turbofan2Spool] step: %.0f steps/s (%.4f us/step)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- PropulsionSystem (full tick) ----------------------------- */

// The end-to-end per-tick path: step the turbofan, drive fuel burn, and sample
// the wrench (thrust + gyroscopic moment) -- as a vehicle would each tick.
PERF_TEST(PropulsionSystemStep, Throughput) {
  UB_PERF_GUARD(perf);

  sim::propulsion::Turbofan2SpoolModel engine;
  sim::dynamics::mass_properties::FuelTankMassSource tank;
  const sim::dynamics::rigid_body::Vec3 omega{0.02, 0.01, 0.015};

  sim::propulsion::PropulsionSystem prop;
  prop.model = &engine;
  prop.fuel = &tank;
  prop.omega_body = &omega;
  prop.engine_count = 4;

  const double rho = 0.4135;
  const double dt = 0.02;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      prop.step(0.9, rho, dt);
      if (tank.fuel_kg <= 0.0) {
        tank = sim::dynamics::mass_properties::FuelTankMassSource{};
      }
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        prop.step(0.9, rho, dt);
        const auto w = prop.current();
        sink += w.force.x + w.moment.y;
        if (tank.fuel_kg <= 0.0) {
          tank = sim::dynamics::mass_properties::FuelTankMassSource{};
        }
      },
      "propulsion_system_step");

  std::printf("\n[PropulsionSystem] step+sample: %.0f ticks/s (%.4f us/tick)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
