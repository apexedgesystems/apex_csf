/**
 * @file Composite_pTest.cpp
 * @brief Performance tests for CMOS composite gate models.
 *
 * Measures truth table evaluation and MNA stamp throughput for CMOS gates.
 * Truth table functions are constexpr -- expected to be trivial (sub-nanosecond).
 * Stamp functions involve MNA system interaction (same overhead as resistor stamp).
 *
 * Usage:
 *   ./Composite_PTEST --csv results.csv
 *   ./Composite_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>

namespace ub = vernier::bench;
using sim::electronics::devices::composite::CmosInverter;
using sim::electronics::devices::composite::CmosNand;
using sim::electronics::devices::composite::CmosNor;

/* ----------------------------- Truth Table Evaluation ----------------------------- */

/**
 * @brief Truth table evaluation throughput (all 3 gate types).
 *
 * These are constexpr functions -- the compiler may optimize them entirely.
 * This benchmark establishes the floor: gate logic is free compared to stamping.
 */
PERF_TEST(CompositePerf, TruthTableEval_10000) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 10000;
  volatile int sink = 0;

  std::printf("\n=== Truth Table Evaluation (%zu iterations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      int sum = 0;
      for (std::size_t j = 0; j < COUNT; ++j) {
        int a = static_cast<int>(j & 1);
        int b = static_cast<int>((j >> 1) & 1);
        sum += CmosInverter::truthTable(a);
        sum += CmosNand::truthTable(a, b);
        sum += CmosNor::truthTable(a, b);
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        int sum = 0;
        for (std::size_t j = 0; j < COUNT; ++j) {
          int a = static_cast<int>(j & 1);
          int b = static_cast<int>((j >> 1) & 1);
          sum += CmosInverter::truthTable(a);
          sum += CmosNand::truthTable(a, b);
          sum += CmosNor::truthTable(a, b);
        }
        sink = sum;
      },
      "truth_table_10k");

  double perGate = result.stats.median * 1000.0 / (COUNT * 3);
  std::printf("  %zu x 3 gates: %8.0f batches/s  (%.2f ns/gate)\n", COUNT, result.callsPerSecond,
              perGate);
}

PERF_MAIN()
