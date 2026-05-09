/**
 * @file Amplifiers_pTest.cpp
 * @brief Performance tests for amplifier circuit models.
 *
 * Measures circuit construction + DC operating-point solve throughput for
 * the small-signal amplifier circuits in `sim_electronics_topologies_amplifiers`.
 *
 * Usage:
 *   ./Amplifiers_PTEST --csv results.csv
 *   ./Amplifiers_PTEST --quick
 *   ./Amplifiers_PTEST --profile gperf --gtest_filter="*BjtCe*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/topologies/amplifiers/inc/BjtCommonEmitter.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;
using sim::electronics::topologies::amplifiers::BjtCommonEmitter;
using sim::electronics::devices::nonlinear::BjtEbersMollParams;

/* ----------------------------- BjtCeBuild ----------------------------- */

/**
 * @brief Construct a BjtCommonEmitter (allocate 3 nets + register stamp).
 *
 * Measures the cost of circuit construction in isolation: net allocation,
 * stamp callback registration. Per-instance overhead.
 */
PERF_TEST(AmplifiersPerf, BjtCeBuild) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  std::printf("\n=== BjtCeBuild (3-net BJT amplifier construction) ===\n");

  perf.warmup([&] {
    BjtCommonEmitter amp(12.0, 1e3, 100e3);
    sink = amp.vcc();
  });

  auto result = perf.throughputLoop(
      [&] {
        BjtCommonEmitter amp(12.0, 1e3, 100e3);
        sink = amp.vcc();
      },
      "amp_ce_build");

  std::printf("  3-net amp build: %.2f us/build  (CV=%.1f%%)\n",
              result.stats.median, result.stats.cv * 100.0);
}

/* ----------------------------- BjtCeDcOp ----------------------------- */

/**
 * @brief Solve the DC operating point of a BjtCommonEmitter.
 *
 * Production hot path: `computeDC()` runs Newton-Raphson on the linearized
 * BJT stamp until convergence. This is the cost users pay per
 * static-analysis call.
 */
PERF_TEST(AmplifiersPerf, BjtCeDcOp) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  std::printf("\n=== BjtCeDcOp (BJT amplifier DC operating-point) ===\n");

  perf.warmup([&] {
    BjtCommonEmitter amp(12.0, 1e3, 100e3);
    if (amp.computeDC()) {
      sink = amp.collectorVoltage();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        BjtCommonEmitter amp(12.0, 1e3, 100e3);
        if (amp.computeDC()) {
          sink = amp.collectorVoltage();
        }
      },
      "amp_ce_dc_op");

  std::printf("  3-net amp DC solve: %.2f us/solve  (CV=%.1f%%)\n",
              result.stats.median, result.stats.cv * 100.0);
}

PERF_MAIN()
