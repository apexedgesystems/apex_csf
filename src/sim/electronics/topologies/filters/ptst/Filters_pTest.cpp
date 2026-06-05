/**
 * @file Filters_pTest.cpp
 * @brief Performance benchmarks for analog filter circuit models.
 *
 * Coverage:
 *  - DC operating point solve (build + computeDC)
 *  - Step response simulation throughput (build + 100-step transient)
 *  - Analytical function throughput (pure math, no circuit)
 *
 * Usage:
 *   ./Filters_PTEST --quick --analyze
 *   ./Filters_PTEST --gtest_filter="*Step*" --cycles 100 --repeats 30 --csv step.csv
 *   ./Filters_PTEST --profile perf --gtest_filter="*Step*"
 */

#include "src/bench/inc/Perf.hpp"

#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"
#include "src/sim/electronics/topologies/filters/inc/RcLowPass.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ub = vernier::bench;

/* ----------------------------- DC Operating Point ----------------------------- */

/**
 * @brief DC operating point solve: construct filter, build, computeDC.
 *
 * Measures end-to-end latency of constructing the circuit, building the
 * solver, and solving the DC operating point.
 */
PERF_TEST(RcLowPassPerf, Dc) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sim::electronics::topologies::filters::RcLowPass filter(1e3, 1e-6);
      filter.build();
      filter.setInputVoltage(5.0);

      sim::electronics::algorithms::transient::TransientState state;
      state.resize(filter.circuit().netCount(), 0);
      filter.circuit().computeDC(state);
      sink = state.nodeVoltages[filter.outNet()];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        sim::electronics::topologies::filters::RcLowPass filter(1e3, 1e-6);
        filter.build();
        filter.setInputVoltage(5.0);

        sim::electronics::algorithms::transient::TransientState state;
        state.resize(filter.circuit().netCount(), 0);
        filter.circuit().computeDC(state);
        sink = state.nodeVoltages[filter.outNet()];
      },
      "rc-lowpass-dc");

  std::printf("  Throughput: %.0f solves/s, Median: %.3f us/solve\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- Step Response Simulation ----------------------------- */

/**
 * @brief Step response simulation: build + 100 transient steps.
 *
 * Measures throughput of a complete transient simulation: construction,
 * build, DC solve, then 100 time steps of Backward Euler integration.
 */
PERF_TEST(RcLowPassPerf, Step) {
  UB_PERF_GUARD(perf);

  constexpr int STEPS = 100;
  constexpr double DT = 10e-6;
  volatile double sink = 0.0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sim::electronics::topologies::filters::RcLowPass filter(1e3, 1e-6);
      filter.build();
      filter.setInputVoltage(5.0);

      sim::electronics::algorithms::transient::TransientState state;
      state.resize(filter.circuit().netCount(), 0);
      filter.circuit().computeDC(state);
      filter.circuit().solver().setIntegrationMethod(
          sim::electronics::algorithms::transient::IntegrationMethod::BACKWARD_EULER);

      for (int s = 0; s < STEPS; ++s) {
        filter.circuit().solver().step(DT, state);
      }
      sink = state.nodeVoltages[filter.outNet()];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        sim::electronics::topologies::filters::RcLowPass filter(1e3, 1e-6);
        filter.build();
        filter.setInputVoltage(5.0);

        sim::electronics::algorithms::transient::TransientState state;
        state.resize(filter.circuit().netCount(), 0);
        filter.circuit().computeDC(state);
        filter.circuit().solver().setIntegrationMethod(
            sim::electronics::algorithms::transient::IntegrationMethod::BACKWARD_EULER);

        for (int s = 0; s < STEPS; ++s) {
          filter.circuit().solver().step(DT, state);
        }
        sink = state.nodeVoltages[filter.outNet()];
      },
      "rc-lowpass-step-100");

  std::printf("  Throughput: %.0f sims/s, Median: %.3f us/sim (%d steps)\n", result.callsPerSecond,
              result.stats.median, STEPS);
  std::printf("  Per-step: %.3f us\n", result.stats.median / STEPS);
}

/* ----------------------------- Analytical Functions ----------------------------- */

/**
 * @brief Analytical function throughput: pure math, no circuit construction.
 *
 * Measures combined throughput of analyticalStepResponse and
 * analyticalMagnitudeResponse over a sweep of input values.
 */
PERF_TEST(RcLowPassPerf, Analytical) {
  UB_PERF_GUARD(perf);

  sim::electronics::topologies::filters::RcLowPass filter(1e3, 1e-6);
  constexpr int POINTS = 1000;
  constexpr double VIN = 5.0;
  constexpr double TAU = 1e-3;
  volatile double sink = 0.0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (int p = 0; p < POINTS; ++p) {
        double t = static_cast<double>(p) * (5.0 * TAU / POINTS);
        sum += filter.analyticalStepResponse(VIN, t);
        double f = static_cast<double>(p + 1) * 10.0;
        sum += filter.analyticalMagnitudeResponse(f);
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (int p = 0; p < POINTS; ++p) {
          double t = static_cast<double>(p) * (5.0 * TAU / POINTS);
          sum += filter.analyticalStepResponse(VIN, t);
          double f = static_cast<double>(p + 1) * 10.0;
          sum += filter.analyticalMagnitudeResponse(f);
        }
        sink = sum;
      },
      "rc-lowpass-analytical-1k");

  std::printf("  Throughput: %.0f sweeps/s, Median: %.3f us/sweep (%d points)\n",
              result.callsPerSecond, result.stats.median, POINTS);
  std::printf("  Per-point: %.3f ns (2 functions per point)\n",
              result.stats.median * 1000.0 / POINTS);
}

PERF_MAIN()
