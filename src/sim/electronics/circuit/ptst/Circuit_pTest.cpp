/**
 * @file Circuit_pTest.cpp
 * @brief Performance tests for the Circuit construction and simulation API.
 *
 * Measures end-to-end build + DC solve throughput for circuits of varying
 * size, and transient simulation throughput (steps/sec) for RC circuits.
 *
 * Usage:
 *   ./Circuit_PTEST --csv results.csv
 *   ./Circuit_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"
#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace ub = vernier::bench;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::algorithms::transient::TransientConfig;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;
using sim::electronics::circuit::Circuit;
using sim::electronics::circuit::CircuitNet;
using sim::electronics::devices::linear::ResistorModel;

/* ----------------------------- CircuitBuildSolve ----------------------------- */

/**
 * @brief Build + DC solve for a 3-net resistor divider (Vdd -- R1 -- out -- R2 -- GND).
 *
 * Minimal circuit that exercises the full build/solve path. Establishes
 * baseline overhead for small circuits.
 */
PERF_TEST(CircuitBuildSolve, BuildAndDC_3net) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  std::printf("\n=== Build + DC Solve: 3-net resistor divider ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      Circuit ckt;
      CircuitNet vdd = ckt.addNet();
      CircuitNet out = ckt.addNet();

      const NetID VDD = vdd.id;
      const NetID OUT = out.id;

      ckt.addStamp(
          [VDD, OUT](MnaSystem& mna, double /*time*/, const std::vector<double>& /*prev*/) {
            mna.addVoltageSource(VDD, Circuit::ground(), 5.0);
            ResistorModel::stamp(mna, VDD, OUT, 1e3);
            ResistorModel::stamp(mna, OUT, Circuit::ground(), 1e3);
          });

      ckt.build();
      TransientState state;
      ckt.computeDC(state);
      sink = state.voltage(OUT);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        Circuit ckt;
        CircuitNet vdd = ckt.addNet();
        CircuitNet out = ckt.addNet();

        const NetID VDD = vdd.id;
        const NetID OUT = out.id;

        ckt.addStamp(
            [VDD, OUT](MnaSystem& mna, double /*time*/, const std::vector<double>& /*prev*/) {
              mna.addVoltageSource(VDD, Circuit::ground(), 5.0);
              ResistorModel::stamp(mna, VDD, OUT, 1e3);
              ResistorModel::stamp(mna, OUT, Circuit::ground(), 1e3);
            });

        ckt.build();
        TransientState state;
        ckt.computeDC(state);
        sink = state.voltage(OUT);
      },
      "build_dc_3net");

  std::printf("  3-net divider: %8.0f solves/s  (%.1f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief Build + DC solve for a 50-net resistor chain.
 *
 * Chain of 50 resistor sections (Vdd -- R1 -- n1 -- R2 -- n2 -- ... -- GND)
 * measures how build + solve scales with circuit size.
 */
PERF_TEST(CircuitBuildSolve, BuildAndDC_50net) {
  UB_PERF_GUARD(perf);

  const std::size_t SECTIONS = 50;
  volatile double sink = 0.0;

  std::printf("\n=== Build + DC Solve: %zu-net resistor chain ===\n", SECTIONS);

  auto buildAndSolve = [&]() {
    Circuit ckt;
    CircuitNet vdd = ckt.addNet();

    std::vector<NetID> nets;
    nets.reserve(SECTIONS);
    for (std::size_t j = 0; j < SECTIONS - 1; ++j) {
      nets.push_back(ckt.addNet().id);
    }

    const NetID VDD = vdd.id;

    ckt.addStamp([VDD, nets](MnaSystem& mna, double /*time*/, const std::vector<double>& /*prev*/) {
      mna.addVoltageSource(VDD, Circuit::ground(), 5.0);
      ResistorModel::stamp(mna, VDD, nets[0], 1e3);
      for (std::size_t j = 0; j + 1 < nets.size(); ++j) {
        ResistorModel::stamp(mna, nets[j], nets[j + 1], 1e3);
      }
      ResistorModel::stamp(mna, nets.back(), Circuit::ground(), 1e3);
    });

    ckt.build();
    TransientState state;
    ckt.computeDC(state);
    sink = state.voltage(nets.back());
  };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      buildAndSolve();
    }
  });

  auto result = perf.throughputLoop(buildAndSolve, "build_dc_50net");

  std::printf("  %zu-net chain: %8.0f solves/s  (%.1f us/solve)\n", SECTIONS, result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- CircuitTransient ----------------------------- */

/**
 * @brief Transient simulation throughput for RC circuit, 10 time steps.
 *
 * Simple RC lowpass (Vdd -- R -- out -- C -- GND). Measures transient
 * stepping overhead at minimal step count.
 */
PERF_TEST(CircuitTransient, RcTransient_10step) {
  UB_PERF_GUARD(perf);

  const std::size_t STEPS = 10;
  volatile double sink = 0.0;

  std::printf("\n=== RC Transient: %zu steps ===\n", STEPS);

  auto runSim = [&]() {
    Circuit ckt;
    CircuitNet vdd = ckt.addNet();
    CircuitNet out = ckt.addNet();

    const NetID VDD = vdd.id;
    const NetID OUT = out.id;

    ckt.addStamp([VDD, OUT](MnaSystem& mna, double /*time*/, const std::vector<double>& /*prev*/) {
      mna.addVoltageSource(VDD, Circuit::ground(), 5.0);
      ResistorModel::stamp(mna, VDD, OUT, 1e3);
    });

    ckt.addCapacitor(OUT, Circuit::ground(), 1e-6);

    TransientConfig cfg;
    cfg.tStart = 0.0;
    cfg.tStep = 1e-6;
    cfg.tEnd = cfg.tStep * static_cast<double>(STEPS);
    cfg.dcOpPoint = true;

    auto result = ckt.simulate(cfg);
    sink = result.finalState.voltage(OUT);
  };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      runSim();
    }
  });

  auto result = perf.throughputLoop(runSim, "rc_transient_10step");

  double stepsPerSec = result.callsPerSecond * static_cast<double>(STEPS);
  std::printf("  %zu steps: %8.0f sims/s  (%8.0f steps/s)  (%.1f us/sim)\n", STEPS,
              result.callsPerSecond, stepsPerSec, result.stats.median);
}

/**
 * @brief Transient simulation throughput for RC circuit, 100 time steps.
 *
 * Same RC circuit as above but with 100 steps. Shows how throughput
 * scales with step count (should be dominated by MNA solve cost).
 */
PERF_TEST(CircuitTransient, RcTransient_100step) {
  UB_PERF_GUARD(perf);

  const std::size_t STEPS = 100;
  volatile double sink = 0.0;

  std::printf("\n=== RC Transient: %zu steps ===\n", STEPS);

  auto runSim = [&]() {
    Circuit ckt;
    CircuitNet vdd = ckt.addNet();
    CircuitNet out = ckt.addNet();

    const NetID VDD = vdd.id;
    const NetID OUT = out.id;

    ckt.addStamp([VDD, OUT](MnaSystem& mna, double /*time*/, const std::vector<double>& /*prev*/) {
      mna.addVoltageSource(VDD, Circuit::ground(), 5.0);
      ResistorModel::stamp(mna, VDD, OUT, 1e3);
    });

    ckt.addCapacitor(OUT, Circuit::ground(), 1e-6);

    TransientConfig cfg;
    cfg.tStart = 0.0;
    cfg.tStep = 1e-6;
    cfg.tEnd = cfg.tStep * static_cast<double>(STEPS);
    cfg.dcOpPoint = true;

    auto result = ckt.simulate(cfg);
    sink = result.finalState.voltage(OUT);
  };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      runSim();
    }
  });

  auto result = perf.throughputLoop(runSim, "rc_transient_100step");

  double stepsPerSec = result.callsPerSecond * static_cast<double>(STEPS);
  std::printf("  %zu steps: %8.0f sims/s  (%8.0f steps/s)  (%.1f us/sim)\n", STEPS,
              result.callsPerSecond, stepsPerSec, result.stats.median);
}

PERF_MAIN()
