/**
 * @file Companions_pTest.cpp
 * @brief Performance tests for reactive element companion models.
 *
 * Companion models discretize capacitors and inductors for transient simulation.
 * Each timestep evaluates geq (equivalent conductance) and ieq (equivalent current)
 * then stamps into the MNA system. This is the per-timestep overhead for reactive
 * elements.
 *
 * Usage:
 *   ./Companions_PTEST --csv results.csv
 *   ./Companions_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <vector>

namespace ub = vernier::bench;
using sim::electronics::algorithms::companions::CapacitorCompanion;
using sim::electronics::algorithms::companions::CompanionSet;
using sim::electronics::algorithms::companions::InductorCompanion;
using sim::electronics::algorithms::transient::IntegrationMethod;

/* ----------------------------- Companion Evaluation ----------------------------- */

/**
 * @brief Capacitor companion geq+ieq evaluation throughput.
 *
 * Simulates 100 capacitors being evaluated at each timestep (typical for a
 * medium-complexity circuit with decoupling caps).
 */
PERF_TEST(CompanionPerf, CapacitorGeqIeq_100) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 100;
  const double DT = 1e-6;
  std::vector<CapacitorCompanion> caps(COUNT);
  for (std::size_t i = 0; i < COUNT; ++i) {
    caps[i].posNet = static_cast<uint32_t>(i + 1);
    caps[i].negNet = 0;
    caps[i].capacitance = 1e-6 * (1.0 + static_cast<double>(i) * 0.01);
    caps[i].prevVoltage = 2.5;
  }

  volatile double sink = 0.0;

  std::printf("\n=== Capacitor Companion geq+ieq (%zu caps, BE) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (auto& c : caps) {
        sum += c.geq(DT, IntegrationMethod::BACKWARD_EULER);
        sum += c.ieq(DT, IntegrationMethod::BACKWARD_EULER);
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (auto& c : caps) {
          sum += c.geq(DT, IntegrationMethod::BACKWARD_EULER);
          sum += c.ieq(DT, IntegrationMethod::BACKWARD_EULER);
        }
        sink = sum;
      },
      "cap_geq_ieq_100");

  double perCap = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu caps: %8.0f batches/s  (%.1f ns/cap)\n", COUNT, result.callsPerSecond, perCap);
}

/**
 * @brief CompanionSet stamp throughput (caps + inductors into MNA).
 */
PERF_TEST(CompanionPerf, CompanionSetStamp_50) {
  UB_PERF_GUARD(perf);

  const std::size_t NUM_CAPS = 40;
  const std::size_t NUM_INDS = 10;
  const double DT = 1e-6;

  CompanionSet set;
  for (std::size_t i = 0; i < NUM_CAPS; ++i) {
    set.addCapacitor(static_cast<uint32_t>(i + 1), 0, 1e-6);
    set.capacitor(i).prevVoltage = 2.5;
  }
  for (std::size_t i = 0; i < NUM_INDS; ++i) {
    set.addInductor(static_cast<uint32_t>(NUM_CAPS + i + 1), 0, 1e-3);
    set.inductor(i).prevCurrent = 0.1;
  }

  volatile double sink = 0.0;

  std::printf("\n=== CompanionSet stamp (%zu caps + %zu inductors, BE) ===\n", NUM_CAPS, NUM_INDS);

  sim::electronics::algorithms::mna::MnaSystem mna(NUM_CAPS + NUM_INDS + 1);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      mna.clear();
      set.stampAll(mna, DT, IntegrationMethod::BACKWARD_EULER);
      sink = 1.0;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        mna.clear();
        set.stampAll(mna, DT, IntegrationMethod::BACKWARD_EULER);
        sink = 1.0;
      },
      "set_stamp_50");

  double perElement = result.stats.median * 1000.0 / (NUM_CAPS + NUM_INDS);
  std::printf("  %zu elements: %8.0f batches/s  (%.1f ns/element)\n", NUM_CAPS + NUM_INDS,
              result.callsPerSecond, perElement);
}

PERF_MAIN()
