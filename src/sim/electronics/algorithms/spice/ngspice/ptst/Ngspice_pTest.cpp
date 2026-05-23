/**
 * @file Ngspice_pTest.cpp
 * @brief Performance tests for NgspiceWrapper init/shutdown and DC solve.
 *
 * NgspiceWrapper is a verification tool -- performance is dominated by
 * libngspice internals. These benchmarks quantify the overhead of our wrapper
 * layer and document ngspice's own performance for reference.
 *
 * Usage:
 *   ./Ngspice_PTEST --csv results.csv
 *   ./Ngspice_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/algorithms/spice/ngspice/inc/NgspiceWrapper.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;
using sim::electronics::algorithms::spice::ngspice::NgspiceStatus;
using sim::electronics::algorithms::spice::ngspice::NgspiceWrapper;

/* ----------------------------- Init/Shutdown ----------------------------- */

/**
 * @brief NgspiceWrapper construction and destruction throughput.
 *
 * Measures the overhead of initializing and shutting down the ngspice
 * shared library binding. This is a one-time cost per simulation run.
 */
PERF_TEST(NgspicePerf, InitShutdown) {
  UB_PERF_GUARD(perf);

  volatile bool sink = false;

  std::printf("\n=== NgspiceWrapper Init/Shutdown ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      NgspiceWrapper wrapper;
      sink = NgspiceWrapper::isLibngspiceAvailable();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        NgspiceWrapper wrapper;
        sink = NgspiceWrapper::isLibngspiceAvailable();
      },
      "init_shutdown");

  std::printf("  Init+shutdown: %8.0f cycles/s  (%.1f us/cycle)\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- DC Operating Point ----------------------------- */

/**
 * @brief DC operating point solve for a simple resistor divider.
 *
 * Loads a minimal netlist from string and runs DC analysis. Measures the
 * end-to-end cost of netlist loading + DC solve + result extraction.
 * This is dominated by ngspice internals.
 */
PERF_TEST(NgspicePerf, DcOpResistorDivider) {
  UB_PERF_GUARD(perf);

  volatile double sink = 0.0;

  std::printf("\n=== DC Operating Point: Resistor Divider ===\n");

  // Simple resistor divider: Vdd(5V) -- R1(1k) -- OUT -- R2(1k) -- GND
  static const char* NETLIST = "Resistor Divider\n"
                               "V1 vdd 0 5.0\n"
                               "R1 vdd out 1k\n"
                               "R2 out 0 1k\n"
                               ".end\n";

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      NgspiceWrapper wrapper;
      NgspiceStatus status = wrapper.loadNetlistFromString(NETLIST);
      if (status == NgspiceStatus::OK) {
        status = wrapper.runDcOperatingPoint();
        if (status == NgspiceStatus::OK) {
          double V = 0.0;
          (void)wrapper.getNodeVoltage("out", V);
          sink = V;
        }
      }
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        NgspiceWrapper wrapper;
        NgspiceStatus status = wrapper.loadNetlistFromString(NETLIST);
        if (status == NgspiceStatus::OK) {
          status = wrapper.runDcOperatingPoint();
          if (status == NgspiceStatus::OK) {
            double V = 0.0;
            (void)wrapper.getNodeVoltage("out", V);
            sink = V;
          }
        }
      },
      "dc_op_divider");

  std::printf("  DC solve: %8.0f solves/s  (%.1f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

PERF_MAIN()
