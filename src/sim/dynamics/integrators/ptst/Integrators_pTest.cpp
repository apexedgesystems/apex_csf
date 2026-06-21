/**
 * @file Integrators_pTest.cpp
 * @brief Performance test for the integrator kernels.
 *
 * RT-path: the flight loop drives an integrator on every tick. Step
 * throughput sets the per-vehicle cost of the integration stage (one step per
 * lambda; per-op time derived from callsPerSecond).
 *
 * Usage:
 *   ./SimDynamicsIntegrators_PTEST --gtest_list_tests
 *   ./SimDynamicsIntegrators_PTEST --profile perf --gtest_filter="*RK4*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/dynamics/integrators/inc/RK4.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

namespace ub = vernier::bench;

using sim::dynamics::integrators::stepRK4;

namespace {

/// Minimal scalar state with the operators the integrators require.
struct Scalar {
  double v = 0.0;
  Scalar operator+(const Scalar& o) const { return {v + o.v}; }
  Scalar operator*(double k) const { return {v * k}; }
};

} // namespace

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

PERF_MAIN()
