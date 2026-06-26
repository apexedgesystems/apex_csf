/**
 * @file Aerodynamics_pTest.cpp
 * @brief Performance tests for the aerodynamic evaluators.
 *
 * RT-path: a vehicle evaluates its aerodynamics once per tick. Throughput sets
 * the per-tick cost of the aero stage (one evaluation per lambda; per-op time
 * derived from callsPerSecond).
 *
 *  - PolarAero               (parabolic polar: a few multiply-adds)
 *  - StabilityDerivativeAero (~30 derivative terms + wind->body rotation)
 *
 * Usage:
 *   ./SimAerodynamics_PTEST --gtest_list_tests
 *   ./SimAerodynamics_PTEST --profile perf --gtest_filter="*StabilityDerivative*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/aerodynamics/inc/PolarAero.hpp"
#include "src/sim/aerodynamics/inc/StabilityDerivativeAero.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;

using sim::aerodynamics::ControlInputs;
using sim::aerodynamics::evaluatePolar;
using sim::aerodynamics::evaluateStabilityDerivative;
using sim::aerodynamics::PolarAeroParams;
using sim::aerodynamics::StabilityDerivativeAeroParams;
using sim::dynamics::rigid_body::Vec3;

/* ----------------------------- PolarAero ----------------------------- */

PERF_TEST(PolarAeroEval, Throughput) {
  UB_PERF_GUARD(perf);

  const PolarAeroParams p{};
  const double rho = 0.4135;
  const double V = 240.0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)evaluatePolar(p, 0.04, rho, V);
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        const auto r = evaluatePolar(p, 0.04, rho, V);
        sink += r.L_N + r.D_N;
      },
      "polar_eval");

  std::printf("\n[PolarAero] evaluate: %.0f evals/s (%.4f us/eval)\n", result.callsPerSecond,
              1.0e6 / result.callsPerSecond);
}

/* ----------------------------- StabilityDerivativeAero ----------------------------- */

PERF_TEST(StabilityDerivativeAeroEval, Throughput) {
  UB_PERF_GUARD(perf);

  const StabilityDerivativeAeroParams p{};
  const double rho = 0.5258;
  const Vec3 v_body{240.0, 2.0, 8.0};
  const Vec3 w_body{0.02, 0.01, 0.015};
  ControlInputs delta;
  delta.elevator_rad = 0.02;
  delta.aileron_rad = 0.01;
  delta.rudder_rad = 0.005;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)evaluateStabilityDerivative(p, v_body, w_body, delta, rho);
    }
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        const auto r = evaluateStabilityDerivative(p, v_body, w_body, delta, rho);
        sink += r.force_body.x + r.moment_body.y;
      },
      "stability_derivative_eval");

  std::printf("\n[StabilityDerivativeAero] evaluate: %.0f evals/s (%.4f us/eval)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
