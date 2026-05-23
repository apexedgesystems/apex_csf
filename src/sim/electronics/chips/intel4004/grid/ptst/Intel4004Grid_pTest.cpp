/**
 * @file Intel4004Grid_pTest.cpp
 * @brief Performance tests for Intel 4004 transistor-level grid.
 *
 * Four workloads:
 *   GridBuild:             Circuit construction from SPICE netlist (initialization).
 *   GridSimulateByte:      Single-byte L1 execution (L0-authoritative byte path).
 *   GridStampAll:          Full 2,242-transistor stamp throughput (inner-loop cost).
 *   GridSimulateByteLevel2: Single-byte L2 execution with full primitives, BSIM3
 *                          latch core, Meyer caps, bootstrap caps, 32-NOP warmup
 *                          (the post-2026-04-25 production workload for L2 closure).
 *
 * All four tests are heavyweight (ms-to-seconds per iteration). Use --cycles 1
 * to avoid auto-tuning overhead. The measured() API handles repeats internally.
 *
 * Usage:
 *   ./Intel4004Grid_PTEST --cycles 1 --csv results.csv
 *   ./Intel4004Grid_PTEST --cycles 1 --quick
 *   ./Intel4004Grid_PTEST --cycles 1 --repeats 15 --csv final.csv
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1.hpp"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel2.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace ub = vernier::bench;
using sim::electronics::chips::intel4004::Intel4004GridLevel1;
using sim::electronics::chips::intel4004::Intel4004GridLevel2;
using sim::electronics::chips::intel4004::Intel4004Netlist;
using sim::electronics::chips::intel4004::loadSpiceNetlist;
using sim::electronics::circuit::Circuit;

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

/* ----------------------------- GridBuild ----------------------------- */

/**
 * @brief Circuit construction from SPICE netlist.
 *
 * Measures buildCircuit() which allocates ~1,081 nets, pre-resolves all
 * 2,242 transistors to NetIDs, and registers stamp functions. This runs
 * once per simulation session.
 */
PERF_TEST(GridPerf, GridBuild) {
  UB_PERF_GUARD(perf);

  const auto& NETLIST = cachedNetlist();
  volatile std::size_t sink = 0;

  std::printf("\n=== GridBuild (circuit from netlist, 2242 transistors) ===\n");

  perf.warmup([&] {
    Intel4004GridLevel1 grid;
    auto circuit = grid.buildCircuit(NETLIST);
    sink = grid.transistorCount();
  });

  auto result = perf.measured(
      [&] {
        Intel4004GridLevel1 grid;
        auto circuit = grid.buildCircuit(NETLIST);
        sink = grid.transistorCount();
      },
      "grid_build_circuit");

  std::printf("  2242 transistors: %.2f ms/build  (CV=%.1f%%)\n", result.stats.median,
              result.stats.cv * 100.0);
}

/* ----------------------------- GridSimulateByte ----------------------------- */

/**
 * @brief Single-byte Level 1 execution time.
 *
 * The production workload: warm up with binary switch (16 NOP bytes), then
 * execute one LDM 5 byte under Level 1 Shichman-Hodges physics. This is
 * the end-to-end cost users pay per instruction byte.
 */
PERF_TEST(GridPerf, GridSimulateByte) {
  UB_PERF_GUARD(perf);

  const auto& NETLIST = cachedNetlist();
  volatile std::uint8_t sink = 0;

  static constexpr std::size_t WARMUP_NOPS = 16;
  std::vector<std::uint8_t> rom(WARMUP_NOPS + 1, 0x00);
  rom.back() = 0xD5; // LDM 5

  std::printf("\n=== GridSimulateByte (L1 single byte, 2242 tx, 1081 nets) ===\n");

  perf.warmup([&] {
    Intel4004GridLevel1 grid;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.bsParams_.vth = 1.17;
    grid.gminTransient_ = 1e-9;
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP_NOPS, 1);
    sink = grid.readAccumulator(state.nodeVoltages);
  });

  auto result = perf.measured(
      [&] {
        Intel4004GridLevel1 grid;
        auto circuit = grid.buildCircuit(NETLIST);
        grid.bsParams_.vth = 1.17;
        grid.gminTransient_ = 1e-9;
        auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP_NOPS, 1);
        sink = grid.readAccumulator(state.nodeVoltages);
      },
      "grid_simulate_byte_l1");

  const double PER_BYTE_S = result.stats.median / 1e6;
  std::printf("  L1 single byte: %.3f s/byte  (CV=%.1f%%)\n", PER_BYTE_S, result.stats.cv * 100.0);
}

/* ----------------------------- GridStampAll ----------------------------- */

/**
 * @brief Full 2,242-transistor stamp throughput via DC solve.
 *
 * Measures the cost of a single DC solve which stamps all transistors into
 * a sparse MNA matrix and runs KLU factorize + solve. This captures the
 * inner-loop cost that dominates each sub-step of every clock phase.
 */
PERF_TEST(GridPerf, GridStampAll) {
  UB_PERF_GUARD(perf);

  const auto& NETLIST = cachedNetlist();
  Intel4004GridLevel1 grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  volatile double sink = 0.0;

  std::printf("\n=== GridStampAll (DC solve: stamp 2242 tx + KLU factor/solve) ===\n");

  perf.warmup([&] {
    sim::electronics::algorithms::transient::TransientState state;
    state.resize(circuit.netCount(), 0);
    circuit.computeDC(state);
    sink = state.nodeVoltages[1];
  });

  auto result = perf.measured(
      [&] {
        sim::electronics::algorithms::transient::TransientState state;
        state.resize(circuit.netCount(), 0);
        circuit.computeDC(state);
        sink = state.nodeVoltages[1];
      },
      "grid_dc_solve");

  std::printf("  DC solve (stamp+factor+solve): %.1f us/solve  (CV=%.1f%%)\n", result.stats.median,
              result.stats.cv * 100.0);
}

/* ----------------------------- GridSimulateByteLevel2 ----------------------------- */

/**
 * @brief Single-byte L2 execution time.
 *
 * The L2 production workload (post 2026-04-25 closure): full primitives ON,
 * BSIM3 latch core, Meyer caps, bootstrap caps, 32-NOP warmup via
 * `simulateLevel1FromScratch`, then one LDM 5 byte at L2 fidelity. This is
 * the end-to-end cost users pay per instruction byte for the
 * physics-authoritative 100% transistor-stamped path.
 */
PERF_TEST(GridPerf, GridSimulateByteLevel2) {
  UB_PERF_GUARD(perf);

  const auto& NETLIST = cachedNetlist();
  volatile std::uint8_t sink = 0;

  static constexpr std::size_t WARMUP_NOPS = 32;
  std::vector<std::uint8_t> rom(WARMUP_NOPS + 1, 0x00);
  rom.back() = 0xD5; // LDM 5

#ifdef INTEL4004_DATA_DIR
  static const std::string CAPS_PATH =
      std::string(INTEL4004_DATA_DIR) + "/lajos-4004-bootstrap-caps.txt";
#else
  static const std::string CAPS_PATH =
      "src/sim/electronics/chips/intel4004/netlist/data/lajos-4004-bootstrap-caps.txt";
#endif

  std::printf("\n=== GridSimulateByteLevel2 (L2 single byte, BSIM3 latch + caps) ===\n");

  perf.warmup([&] {
    Intel4004GridLevel2 grid;
    grid.enableMeyerCaps_ = true;
    grid.gminTransient_ = grid.gminTransientWithCaps_;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.loadBootstrapCaps(CAPS_PATH);
    auto state = grid.simulateLevel1FromScratch(circuit, rom.data(), rom.size(), WARMUP_NOPS, 0,
                                                /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);
    grid.traceExecuteByte(circuit, state, 0xD5, nullptr);
    sink = grid.readAccumulator(state.nodeVoltages);
  });

  auto result = perf.measured(
      [&] {
        Intel4004GridLevel2 grid;
        grid.enableMeyerCaps_ = true;
        grid.gminTransient_ = grid.gminTransientWithCaps_;
        auto circuit = grid.buildCircuit(NETLIST);
        grid.loadBootstrapCaps(CAPS_PATH);
        auto state = grid.simulateLevel1FromScratch(circuit, rom.data(), rom.size(), WARMUP_NOPS, 0,
                                                    /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);
        grid.traceExecuteByte(circuit, state, 0xD5, nullptr);
        sink = grid.readAccumulator(state.nodeVoltages);
      },
      "grid_simulate_byte_l2");

  const double PER_BYTE_S = result.stats.median / 1e6;
  std::printf("  L2 single byte: %.3f s/byte  (CV=%.1f%%)\n", PER_BYTE_S, result.stats.cv * 100.0);
}

PERF_MAIN()
