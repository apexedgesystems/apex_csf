/**
 * @file Disturbance_pTest.cpp
 * @brief Performance test for the Dryden turbulence step.
 *
 * RT-path: a vehicle samples atmospheric disturbance each tick. Throughput
 * sets the per-tick cost of the gust update (one step per lambda; per-op time
 * derived from callsPerSecond). The step includes the three Gaussian RNG
 * draws, which dominate the cost.
 *
 * Usage:
 *   ./SimDynamicsDisturbance_PTEST --gtest_list_tests
 *   ./SimDynamicsDisturbance_PTEST --profile perf --gtest_filter="*Dryden*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/dynamics/disturbance/inc/DrydenTurbulence.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;

using sim::dynamics::disturbance::DrydenRng;
using sim::dynamics::disturbance::DrydenTurbulenceParams;
using sim::dynamics::disturbance::DrydenTurbulenceState;
using sim::dynamics::disturbance::stepDryden;

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
