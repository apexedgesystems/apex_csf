/**
 * @file Intel4004Gate_pTest.cpp
 * @brief Performance tests for Intel 4004 gate-level model.
 *
 * Two workloads:
 *   GateExtraction:   Extract ~427 NOR gates and ~133 pass gates from netlist.
 *   GatePropagation:  Event-driven NOR evaluation throughput (full circuit).
 *
 * Gate extraction is an initialization-only cost. Propagation runs every
 * clock phase during simulation and dominates gate-level execution time.
 *
 * Usage:
 *   ./Intel4004Gate_PTEST --cycles 100 --csv results.csv
 *   ./Intel4004Gate_PTEST --quick
 *   ./Intel4004Gate_PTEST --cycles 100 --repeats 15 --csv final.csv
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/chips/intel4004/gate/inc/Intel4004GateLevel.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>

#include <cstdio>

namespace ub = vernier::bench;
using sim::electronics::chips::intel4004::Intel4004GateLevel;
using sim::electronics::chips::intel4004::Intel4004Netlist;
using sim::electronics::chips::intel4004::loadSpiceNetlist;

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = std::string(INTEL4004_DATA_DIR) + "/lajos-4004.spice";
#else
static const std::string SPICE_PATH =
    "src/sim/electronics/chips/intel4004/netlist/data/lajos-4004.spice";
#endif

static const Intel4004Netlist& cachedNetlist() {
  static const auto INSTANCE = loadSpiceNetlist(SPICE_PATH);
  return INSTANCE;
}

/* ----------------------------- GateExtraction ----------------------------- */

/**
 * @brief Extract ~427 NOR gates and ~133 pass gates from the SPICE netlist.
 *
 * Measures the full build() cost: grid construction, gate extraction,
 * pass gate identification, and fanout map construction. This runs once
 * per simulation session.
 */
PERF_TEST(GatePerf, GateExtraction) {
  UB_PERF_GUARD(perf);

  const auto& NETLIST = cachedNetlist();
  volatile std::size_t sink = 0;

  std::printf("\n=== GateExtraction (427 NOR gates from netlist) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      Intel4004GateLevel gl;
      gl.build(NETLIST);
      sink = gl.gateCount();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < perf.cycles(); ++i) {
          Intel4004GateLevel gl;
          gl.build(NETLIST);
          sink = gl.gateCount();
        }
      },
      "gate_extraction_427");

  const double PER_BUILD_MS = result.stats.median / perf.cycles();
  std::printf("  427 NOR gates: %.2f ms/build  (%zu builds/s, CV=%.1f%%)\n", PER_BUILD_MS,
              static_cast<std::size_t>(result.callsPerSecond * perf.cycles()),
              result.stats.cv * 100.0);
}

/* ----------------------------- GatePropagation ----------------------------- */

/**
 * @brief Event-driven NOR evaluation throughput on the full circuit.
 *
 * Measures propagate() which iterates all 427 NOR gates and 133 pass gates
 * until convergence. This is the inner loop cost during gate-level execution:
 * each clock phase calls propagate() once.
 */
PERF_TEST(GatePerf, GatePropagation) {
  UB_PERF_GUARD(perf);

  const auto& NETLIST = cachedNetlist();

  // Pre-build the gate model (initialization cost not measured)
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  volatile std::size_t sink = 0;

  std::printf("\n=== GatePropagation (event-driven NOR evaluation, 427 gates) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      // Drive CLK1 active to create work for the propagator
      gl.driveNet(gl.grid_.clk1Net_, false);
      sink = gl.propagate();
      // Drive CLK1 inactive for next iteration
      gl.driveNet(gl.grid_.clk1Net_, true);
      sink = gl.propagate();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < perf.cycles(); ++i) {
          gl.driveNet(gl.grid_.clk1Net_, false);
          sink = gl.propagate();
          gl.driveNet(gl.grid_.clk1Net_, true);
          sink = gl.propagate();
        }
      },
      "gate_propagation_427");

  const double PER_PROP_US = result.stats.median / (perf.cycles() * 2);
  std::printf("  Propagate (427 gates): %.1f us/call  (%zu props/s, CV=%.1f%%)\n", PER_PROP_US,
              static_cast<std::size_t>(result.callsPerSecond * perf.cycles() * 2),
              result.stats.cv * 100.0);
}

PERF_MAIN()
