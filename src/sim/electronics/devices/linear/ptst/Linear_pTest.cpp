/**
 * @file Linear_pTest.cpp
 * @brief Performance tests for linear device models (resistor, capacitor, inductor).
 *
 * Linear devices are simple 1/R conductance stamps. These benchmarks establish
 * that per-element stamping cost is negligible compared to the solver.
 *
 * Usage:
 *   ./Linear_PTEST --csv results.csv
 *   ./Linear_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>

namespace ub = vernier::bench;
using sim::electronics::algorithms::mna::MnaSystemSparse;
using sim::electronics::devices::linear::ResistorModel;

/* ----------------------------- Resistor Stamping ----------------------------- */

/**
 * @brief Resistor stamp throughput into sparse MNA system.
 *
 * Stamps 1000 resistors in a chain (typical circuit topology) to measure
 * the per-resistor cost including the MNA conductance stamp overhead.
 */
PERF_TEST(LinearPerf, ResistorStamp_1000) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 1000;
  volatile double sink = 0.0;

  std::printf("\n=== Resistor Stamp Throughput (%zu resistors) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      MnaSystemSparse mna(COUNT + 1);
      for (std::size_t j = 0; j < COUNT; ++j) {
        ResistorModel::stamp(mna, static_cast<uint32_t>(j), static_cast<uint32_t>(j + 1), 1e3);
      }
      sink = static_cast<double>(mna.nnz());
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        MnaSystemSparse mna(COUNT + 1);
        for (std::size_t j = 0; j < COUNT; ++j) {
          ResistorModel::stamp(mna, static_cast<uint32_t>(j), static_cast<uint32_t>(j + 1), 1e3);
        }
        sink = static_cast<double>(mna.nnz());
      },
      "resistor_1000");

  double perResistor = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu resistors: %8.0f batches/s  (%.1f ns/resistor)\n", COUNT,
              result.callsPerSecond, perResistor);
}

/**
 * @brief Conductance calculation throughput (pure arithmetic, no MNA).
 */
PERF_TEST(LinearPerf, ConductanceCalc) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;
  const std::size_t COUNT = 10000;

  std::printf("\n=== Conductance Calculation (%zu evaluations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (std::size_t j = 1; j <= COUNT; ++j) {
        sum += ResistorModel::conductance(static_cast<double>(j));
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (std::size_t j = 1; j <= COUNT; ++j) {
          sum += ResistorModel::conductance(static_cast<double>(j));
        }
        sink = sum;
      },
      "conductance_10k");

  double perEval = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu evals: %8.0f batches/s  (%.2f ns/eval)\n", COUNT, result.callsPerSecond,
              perEval);
}

PERF_MAIN()
