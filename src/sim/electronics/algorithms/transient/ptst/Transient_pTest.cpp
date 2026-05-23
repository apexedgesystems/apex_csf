/**
 * @file Transient_pTest.cpp
 * @brief Performance tests for the transient simulation engine.
 *
 * Measures throughput for individual time-step methods and end-to-end RC
 * circuit simulation. All benchmarks use simple RC circuits to isolate
 * solver overhead from device-model cost.
 *
 * Usage:
 *   ./Transient_PTEST --csv results.csv
 *   ./Transient_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <vector>

namespace ub = vernier::bench;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::transient::IntegrationMethod;
using sim::electronics::algorithms::transient::TransientConfig;
using sim::electronics::algorithms::transient::TransientResult;
using sim::electronics::algorithms::transient::TransientSolver;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;

/* ----------------------------- Constants ----------------------------- */

static constexpr double VDD = 5.0;
static constexpr double DT = 1e-9; // 1 ns step

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief Build an RC-chain solver: Vdd -- R -- node1 -- C -- Gnd, repeated.
 *
 * Creates a chain of N identical RC sections. Net 0 is ground, net 1 is Vdd,
 * nets 2..N+1 are the capacitor top nodes.
 *
 * @param netCount Total net count (N+2 for N RC sections).
 * @param R Resistance per section (Ohms).
 * @param C Capacitance per section (Farads).
 * @return Configured solver ready for run() or step().
 */
static TransientSolver buildRcChain(std::size_t sections, double R, double C) {
  std::size_t netCount = sections + 2; // 0=Gnd, 1=Vdd, 2..N+1=cap nodes
  TransientSolver solver(netCount);

  for (std::size_t i = 0; i < sections; ++i) {
    solver.companions().addCapacitor(static_cast<uint32_t>(i + 2), 0, C);
  }

  double G = 1.0 / R;
  solver.setStampCallback([sections, G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    // Vdd to node 2
    mna.addConductance(1, 2, G);
    // Chain: node i+2 to node i+3
    for (std::size_t i = 0; i + 1 < sections; ++i) {
      mna.addConductance(static_cast<uint32_t>(i + 2), static_cast<uint32_t>(i + 3), G);
    }
  });

  return solver;
}

/* ----------------------------- BackwardEulerStep ----------------------------- */

/**
 * @brief Single backward Euler step throughput at 4 nets (1 RC section).
 *
 * Measures the per-step cost of building the MNA system, stamping companions,
 * and solving via dense LU. This is the minimum-cost transient step.
 */
PERF_TEST(TransientPerf, BackwardEulerStep_4net) {
  UB_PERF_GUARD(perf);

  const std::size_t SECTIONS = 1;
  volatile double sink = 0.0;

  std::printf("\n=== Backward Euler Step (4 nets, 1 RC) ===\n");

  auto solver = buildRcChain(SECTIONS, 1e3, 1e-6);
  solver.setIntegrationMethod(IntegrationMethod::BACKWARD_EULER);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientState state;
      state.resize(solver.netCount(), 1);
      solver.step(DT, state);
      sink = state.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientState state;
        state.resize(solver.netCount(), 1);
        solver.step(DT, state);
        sink = state.voltage(2);
      },
      "be_step_4net");

  std::printf("  4 nets: %8.0f steps/s  (%.1f us/step)\n", result.callsPerSecond,
              result.stats.median * 1e3);
}

/**
 * @brief Single backward Euler step throughput at 12 nets (10 RC sections).
 *
 * A moderate-sized chain to show how step cost scales with net count.
 */
PERF_TEST(TransientPerf, BackwardEulerStep_12net) {
  UB_PERF_GUARD(perf);

  const std::size_t SECTIONS = 10;
  volatile double sink = 0.0;

  std::printf("\n=== Backward Euler Step (12 nets, 10 RC) ===\n");

  auto solver = buildRcChain(SECTIONS, 1e3, 1e-6);
  solver.setIntegrationMethod(IntegrationMethod::BACKWARD_EULER);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientState state;
      state.resize(solver.netCount(), 1);
      solver.step(DT, state);
      sink = state.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientState state;
        state.resize(solver.netCount(), 1);
        solver.step(DT, state);
        sink = state.voltage(2);
      },
      "be_step_12net");

  std::printf("  12 nets: %8.0f steps/s  (%.1f us/step)\n", result.callsPerSecond,
              result.stats.median * 1e3);
}

/**
 * @brief Single backward Euler step throughput at 52 nets (50 RC sections).
 *
 * Larger chain to show dense LU scaling at higher net counts.
 */
PERF_TEST(TransientPerf, BackwardEulerStep_52net) {
  UB_PERF_GUARD(perf);

  const std::size_t SECTIONS = 50;
  volatile double sink = 0.0;

  std::printf("\n=== Backward Euler Step (52 nets, 50 RC) ===\n");

  auto solver = buildRcChain(SECTIONS, 1e3, 1e-6);
  solver.setIntegrationMethod(IntegrationMethod::BACKWARD_EULER);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientState state;
      state.resize(solver.netCount(), 1);
      solver.step(DT, state);
      sink = state.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientState state;
        state.resize(solver.netCount(), 1);
        solver.step(DT, state);
        sink = state.voltage(2);
      },
      "be_step_52net");

  std::printf("  52 nets: %8.0f steps/s  (%.1f us/step)\n", result.callsPerSecond,
              result.stats.median * 1e3);
}

/* ----------------------------- TrapezoidalStep ----------------------------- */

/**
 * @brief Trapezoidal integration step throughput at 4 nets.
 *
 * Second-order method has marginally higher companion-model cost (extra
 * multiply for trapezoidal geq/ieq). Benchmarks whether the difference
 * is measurable versus the LU solve.
 */
PERF_TEST(TransientPerf, TrapezoidalStep_4net) {
  UB_PERF_GUARD(perf);

  const std::size_t SECTIONS = 1;
  volatile double sink = 0.0;

  std::printf("\n=== Trapezoidal Step (4 nets, 1 RC) ===\n");

  auto solver = buildRcChain(SECTIONS, 1e3, 1e-6);
  solver.setIntegrationMethod(IntegrationMethod::TRAPEZOIDAL);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientState state;
      state.resize(solver.netCount(), 1);
      solver.step(DT, state);
      sink = state.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientState state;
        state.resize(solver.netCount(), 1);
        solver.step(DT, state);
        sink = state.voltage(2);
      },
      "trap_step_4net");

  std::printf("  4 nets: %8.0f steps/s  (%.1f us/step)\n", result.callsPerSecond,
              result.stats.median * 1e3);
}

/**
 * @brief Trapezoidal integration step throughput at 12 nets.
 */
PERF_TEST(TransientPerf, TrapezoidalStep_12net) {
  UB_PERF_GUARD(perf);

  const std::size_t SECTIONS = 10;
  volatile double sink = 0.0;

  std::printf("\n=== Trapezoidal Step (12 nets, 10 RC) ===\n");

  auto solver = buildRcChain(SECTIONS, 1e3, 1e-6);
  solver.setIntegrationMethod(IntegrationMethod::TRAPEZOIDAL);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientState state;
      state.resize(solver.netCount(), 1);
      solver.step(DT, state);
      sink = state.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientState state;
        state.resize(solver.netCount(), 1);
        solver.step(DT, state);
        sink = state.voltage(2);
      },
      "trap_step_12net");

  std::printf("  12 nets: %8.0f steps/s  (%.1f us/step)\n", result.callsPerSecond,
              result.stats.median * 1e3);
}

/* ----------------------------- RcTransient ----------------------------- */

/**
 * @brief End-to-end RC transient simulation throughput (1 RC, 100 steps).
 *
 * Measures full run() cost including DC operating point (disabled here),
 * time loop, companion stamping, and solve per step.
 */
PERF_TEST(TransientPerf, RcTransient_1section_100step) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  std::printf("\n=== RC Transient (1 section, 100 steps) ===\n");

  auto solver = buildRcChain(1, 1e3, 1e-6);

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 100.0 * DT;
  config.tStep = DT;
  config.dcOpPoint = false;
  config.method = IntegrationMethod::BACKWARD_EULER;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientResult res = solver.run(config, false);
      sink = res.finalState.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientResult res = solver.run(config, false);
        sink = res.finalState.voltage(2);
      },
      "rc_1sec_100step");

  double perStep = result.stats.median * 1e6 / 100.0; // ns per step
  std::printf("  100 steps: %8.0f sims/s  (%.0f ns/step)\n", result.callsPerSecond, perStep);
}

/**
 * @brief End-to-end RC transient simulation throughput (10 RC, 100 steps).
 *
 * Moderate chain to show how end-to-end cost scales with circuit size.
 */
PERF_TEST(TransientPerf, RcTransient_10section_100step) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  std::printf("\n=== RC Transient (10 sections, 100 steps) ===\n");

  auto solver = buildRcChain(10, 1e3, 1e-6);

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 100.0 * DT;
  config.tStep = DT;
  config.dcOpPoint = false;
  config.method = IntegrationMethod::BACKWARD_EULER;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientResult res = solver.run(config, false);
      sink = res.finalState.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientResult res = solver.run(config, false);
        sink = res.finalState.voltage(2);
      },
      "rc_10sec_100step");

  double perStep = result.stats.median * 1e6 / 100.0;
  std::printf("  100 steps: %8.0f sims/s  (%.0f ns/step)\n", result.callsPerSecond, perStep);
}

/**
 * @brief End-to-end RC transient simulation (10 RC, 1000 steps).
 *
 * Longer simulation to amortize setup cost and measure sustained throughput.
 */
PERF_TEST(TransientPerf, RcTransient_10section_1000step) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  std::printf("\n=== RC Transient (10 sections, 1000 steps) ===\n");

  auto solver = buildRcChain(10, 1e3, 1e-6);

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1000.0 * DT;
  config.tStep = DT;
  config.dcOpPoint = false;
  config.method = IntegrationMethod::BACKWARD_EULER;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      TransientResult res = solver.run(config, false);
      sink = res.finalState.voltage(2);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        solver.reset();
        TransientResult res = solver.run(config, false);
        sink = res.finalState.voltage(2);
      },
      "rc_10sec_1000step");

  double perStep = result.stats.median * 1e6 / 1000.0;
  std::printf("  1000 steps: %8.0f sims/s  (%.0f ns/step)\n", result.callsPerSecond, perStep);
}

PERF_MAIN()
