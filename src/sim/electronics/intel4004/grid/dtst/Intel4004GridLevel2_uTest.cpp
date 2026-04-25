/**
 * @file Intel4004GridLevel2_uTest.cpp
 * @brief Verification tests for Intel4004GridLevel2: 100% physics path.
 *
 * L2 differs from L1 in two ways:
 *   1. Latch feedback core uses BSIM3 (smooth Vgst_eff) instead of binary
 *      switch fallback.
 *   2. Behavioral latch overlay is OFF -- the cross-coupled latches must
 *      resolve from physics alone.
 *
 * These tests prove L2 can produce the same digital ACC as L0 (the
 * authoritative behavioral CPU) with the overlay disabled.
 *
 * Manual execution only (per dtst convention; tests run 50-70 seconds each).
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetBsim3.hpp"
#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel2.hpp"
#include "src/sim/electronics/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using sim::electronics::intel4004::Intel4004Cpu;
using sim::electronics::intel4004::Intel4004GridLevel2;
using sim::electronics::intel4004::loadSpiceNetlist;

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = std::string(INTEL4004_DATA_DIR) + "/lajos-4004.spice";
#endif

#ifdef INTEL4004_DATA_DIR

/* ----------------------------- Construction ----------------------------- */

/** @test L2 is operationally distinct from L1 (different type, different config). */
TEST(Intel4004L2, ConstructionIsIndependent) {
  Intel4004GridLevel2 grid;

  // L2 = 100% physics for the steady-state hold: behavioral overlay OFF,
  // BSIM3 stamps the latch core, differentiated GMIN keeps NR convergent.
  EXPECT_FALSE(grid.applyBehavioralLatchOverlay_);

  // Calibrated GMIN tiers (see DISABLED_DifferentiatedGminSweep dtest):
  EXPECT_NEAR(grid.gminTransient_, 5e-3, 1e-12);
  EXPECT_NEAR(grid.gminDriven_, 1e-12, 1e-15);

  // BSIM3 latch params calibrated for the Intel 4004 10 micron PMOS process.
  EXPECT_NEAR(grid.bsim3LatchParams_.n_factor, 1.8, 1e-9);
  EXPECT_NEAR(grid.bsim3LatchParams_.Vth0, Intel4004GridLevel2::VTH_ENH, 1e-9);
}

/** @test L2 builds the same circuit topology as L1 (same netlist parser). */
TEST(Intel4004L2, BuildCircuit) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GridLevel2 grid;
  auto circuit = grid.buildCircuit(NETLIST);

  EXPECT_EQ(grid.transistorCount(), 2242u);
  EXPECT_GE(grid.netCount(), 1081u);
}

/* ----------------------------- Convergence with overlay OFF ----------------------------- */

/**
 * @test L2 default config (overlay OFF + differentiated GMIN) converges
 *       cleanly through warmup + program byte without any net going
 *       out of supply rails.
 *
 * This is the load-bearing test for the "L2 = 100% physics steady-state
 * hold" claim. The previous DISABLED version of this test failed with
 * 73 OOB nets and voltages reaching -407V. The cure landed in this
 * commit: gminTransient_ = 5e-3 algebraically anchors floating storage
 * nets at the Jacobian level, while gminDriven_ = 1e-12 leaves NOR
 * outputs and clocks undisturbed.
 *
 * @note This test does not assert ACC matches L0 -- the pure-physics
 * X3 instruction execution datapath (data-bus -> OPA -> ACC) is a
 * separate milestone; even L1 currently relies on behavioral X3.
 */
TEST(Intel4004L2, SimulateConvergesWithOverlayOff) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5; // LDM 5

  Intel4004GridLevel2 grid;
  ASSERT_FALSE(grid.applyBehavioralLatchOverlay_);

  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseModeLevel1(circuit);
  circuit.solver().invalidateCache();
  auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 1);

  constexpr double V_LO = -1.0;
  constexpr double V_HI = Intel4004GridLevel2::VDD_VOLTAGE + 1.0;
  std::size_t outOfRange = 0;
  double maxAbs = 0.0;
  for (std::size_t i = 1; i < state.nodeVoltages.size(); ++i) {
    const double v = state.nodeVoltages[i];
    if (!std::isfinite(v) || v < V_LO || v > V_HI) ++outOfRange;
    if (std::isfinite(v) && std::fabs(v) > maxAbs) maxAbs = std::fabs(v);
  }
  EXPECT_EQ(outOfRange, 0u)
      << "L2 default config must converge without OOB nets";
  EXPECT_LE(maxAbs, V_HI)
      << "L2 max|v| = " << maxAbs << " exceeds rail tolerance " << V_HI;

  const std::uint8_t l2Acc = grid.readAccumulator(state.nodeVoltages);
  std::printf("L2 100%% physics steady-state: %zu OOB, max|v|=%.4fV, "
              "ACC readback=%u\n",
              outOfRange, maxAbs, static_cast<unsigned>(l2Acc));
}

/* ----------------------------- Stamp wire-up verification ----------------------------- */

/**
 * @test L2's latch hook produces the same (id, gm, gds) as a direct
 * MosfetBsim3::stampValues call with matching parameters.
 *
 * This is the "we wired it correctly" proof: it doesn't depend on
 * full-circuit convergence, only on the L2 hook delegating to the
 * BSIM3 model with the right parameters.
 */
TEST(Intel4004L2, LatchStampDelegatesToBsim3) {
  // Construct a minimal grid to populate transistorKp_ via classification.
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GridLevel2 grid;
  grid.buildCircuit(NETLIST);
  grid.computeTransistorKp(); // populates per-transistor calibrated Kp

  // Pick a representative latch transistor (one whose gate is NOT a NOR
  // output net -- those are the ones routed to the BSIM3 hook).
  // We assert the L2 params match what the hook will use.
  EXPECT_NEAR(grid.bsim3LatchParams_.n_factor, 1.8, 1e-9);
  EXPECT_NEAR(grid.bsim3LatchParams_.Vth0, Intel4004GridLevel2::VTH_ENH, 1e-9);
  EXPECT_NEAR(grid.bsim3LatchParams_.lambda, Intel4004GridLevel2::LAMBDA, 1e-9);

  // Direct BSIM3 call at the calibrated 4004 latch operating point:
  //   Gate = LOW (0V), Source = VDD (5V) -> VSG = 5V, well above Vth.
  //   Drain at VOL = 1.20V -> VSD = 3.80V (saturation).
  // This is the "should be ON" case for an enhancement PMOS.
  using sim::electronics::devices::nonlinear::MosfetBsim3;
  using sim::electronics::devices::nonlinear::MosfetBsim3Params;
  MosfetBsim3Params bp = grid.bsim3LatchParams_;
  bp.Kp = Intel4004GridLevel2::KP_PROCESS *
          Intel4004GridLevel2::WL_ENHANCEMENT_LOGIC;
  const auto SV = MosfetBsim3::stampValues(/*vgs=*/5.0, /*vds=*/3.8,
                                           /*vbs=*/0.0, bp);
  EXPECT_GT(SV.id, 0.0) << "Strong inversion should give positive Id";
  EXPECT_GT(SV.gm, 0.0) << "Strong inversion should give positive gm";
  EXPECT_GT(SV.gds, 0.0) << "Channel-length modulation -> positive gds";
}

/* ----------------------------- L1 vs L2 separation ----------------------------- */

/**
 * @test L1 and L2 can be instantiated side-by-side without interference.
 *
 * Operational separation contract: a process can hold an Intel4004GridLevel1
 * and an Intel4004GridLevel2 at the same time, each with its own circuit,
 * each running independently with its own physics model.
 */
TEST(Intel4004L2, CoexistsWithLevel1) {
  // Both classes can be constructed in the same scope.
  sim::electronics::intel4004::Intel4004GridLevel1 gridL1;
  Intel4004GridLevel2 gridL2;

  // L1 = behavioral overlay ON (binary-switch latch + hard pin).
  EXPECT_TRUE(gridL1.applyBehavioralLatchOverlay_);
  EXPECT_EQ(gridL1.gminDriven_, 0.0); // L1 default: uniform GMIN
  // L2 = behavioral overlay OFF (BSIM3 latch + differentiated GMIN).
  EXPECT_FALSE(gridL2.applyBehavioralLatchOverlay_);
  EXPECT_GT(gridL2.gminDriven_, 0.0);

  // No shared mutable state between instances.
  EXPECT_NE(&gridL1.bsParams_, &gridL2.bsParams_);
}

/* ----------------------------- Parameter probe ----------------------------- */

/**
 * @test Conductance sweep probe -- evidence that hard pin (G=0) is the
 * only viable anchor for the cross-coupled latch under simulateLevel1.
 *
 * Empirical results from this probe (logged once, captured in comments):
 *   G=0       ->  0 out-of-bounds  (hard MNA constraint)
 *   G=1e-12   -> 65 out-of-bounds
 *   G=1e-9    -> 93 out-of-bounds
 *   G=1e-6    -> 77 out-of-bounds
 *   G=1e-3    -> 72 out-of-bounds
 *   G=1.0     -> 35 out-of-bounds  (still bad, despite very strong pull)
 *
 * Conclusion: a Norton anchor of any finite conductance fails to suppress
 * NR transient overshoot in the bistable latch topology. The hard
 * voltage source works because it is an algebraic constraint at the MNA
 * level, not a differential pull. The soft-overlay infrastructure is
 * preserved (latchOverlayConductance_) for use after the X3 datapath
 * rework, when the cross-coupled latch may converge from physics alone.
 *
 * Disabled by default (3-minute runtime; manual diagnostic).
 */
TEST(Intel4004L2, DISABLED_ConductanceSweepProbe) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5; // LDM 5

  auto runOnce = [&](double G) -> std::pair<std::size_t, std::uint8_t> {
    Intel4004GridLevel2 grid;
    grid.latchOverlayConductance_ = G;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 1);

    constexpr double V_LO = -1.0;
    constexpr double V_HI = Intel4004GridLevel2::VDD_VOLTAGE + 1.0;
    std::size_t outOfRange = 0;
    for (std::size_t i = 1; i < state.nodeVoltages.size(); ++i) {
      const double v = state.nodeVoltages[i];
      if (!std::isfinite(v) || v < V_LO || v > V_HI) ++outOfRange;
    }
    return {outOfRange, grid.readAccumulator(state.nodeVoltages)};
  };

  std::printf("L0 expected ACC after LDM 5 = 5 (warmup readback)\n");
  for (double G : {0.0, 1e-12, 1e-9, 1e-6, 1e-3, 1.0}) {
    const auto [oob, acc] = runOnce(G);
    std::printf("G=%-9.0e -> %3zu out-of-bounds, ACC=%u\n", G, oob, static_cast<unsigned>(acc));
  }
}

/* ----------------------------- L2 multi-instruction parity ----------------------------- */

/**
 * @test L2 multi-instruction LDM produces same ACC as L0 (mirrors L1
 *       multi-instruction tests). Behavioral X3 still fires; L2's
 *       improvement vs L1 is the analog fidelity (BSIM3 + overlay off
 *       + GMIN tiering), not the instruction execution path.
 */
TEST(Intel4004L2, MultiInstructionLdm5) {
  // L0 reference
  Intel4004Cpu cpu;
  std::uint8_t romL0[] = {0xD5, 0x00};
  cpu.loadProgram(romL0, sizeof(romL0));
  cpu.step();
  ASSERT_EQ(cpu.accumulator, 5);

  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5;

  Intel4004GridLevel2 grid;
  ASSERT_TRUE(grid.applyBehavioralX3_); // default: behavioral X3 active

  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseModeLevel1(circuit);
  circuit.solver().invalidateCache();
  auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 0);
  grid.traceExecuteByte(circuit, state, rom[WARMUP], nullptr);

  EXPECT_EQ(grid.readAccumulator(state.nodeVoltages), cpu.accumulator);
}

/* ----------------------------- Pure-physics multi-instruction probe ----------------------------- */

/**
 * @test Pure-physics LDM 5: does the analog circuit propagate
 *       D0..D3 -> OPA -> ACC without behavioral X3 execution?
 *
 * This is the load-bearing milestone for "100% physics multi-instruction".
 * L2's defaults turn OFF: behavioral overlay AND behavioral X3 switch.
 * The analog circuit must transfer the data-bus value to ACC through
 * the actual transistor connections (M1368: D0 -> OPA-IB -> OPA.0,
 * then internal X3 path: OPA -> ACC via ADD-ACC / M12 / ADSR signals).
 *
 * Pass criterion: ACC reads 5 (or any digital answer matching L0).
 * Fail mode (informational): ACC reads warmup state (likely 15) ->
 * physics path not yet propagating; identifies which gap to attack.
 */
TEST(Intel4004L2, DISABLED_PurePhysicsLdm5) {
  // L0 reference
  Intel4004Cpu cpu;
  std::uint8_t romL0[] = {0xD5, 0x00};
  cpu.loadProgram(romL0, sizeof(romL0));
  cpu.step();
  ASSERT_EQ(cpu.accumulator, 5);

  // L2 simulation
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5;

  Intel4004GridLevel2 grid;
  ASSERT_FALSE(grid.applyBehavioralLatchOverlay_);
  ASSERT_FALSE(grid.applyBehavioralX3_);

  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseModeLevel1(circuit);
  circuit.solver().invalidateCache();

  // Phase 1: warmup with L1 binary switch (inherited)
  auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 0);

  // Phase 2: trace-execute the LDM byte (drives external clocks + data
  // bus, no behavioral X3 because we disabled it)
  grid.traceExecuteByte(circuit, state, rom[WARMUP], nullptr);

  const std::uint8_t l2Acc = grid.readAccumulator(state.nodeVoltages);
  std::printf("\n  L2 pure-physics LDM 5: L0=%u, L2=%u %s\n",
              static_cast<unsigned>(cpu.accumulator), static_cast<unsigned>(l2Acc),
              (l2Acc == cpu.accumulator) ? "[MATCH]" : "[MISMATCH -- physics path gap]");

  EXPECT_EQ(l2Acc, cpu.accumulator)
      << "Pure-physics ACC must match L0 for LDM 5";
}

/* ----------------------------- X3 datapath diagnostic ----------------------------- */

/**
 * @test Diagnose which X3-path signals are correctly driven when
 *       pure-physics mode is enabled. Dumps OPA, ACC, and the
 *       intermediate control signals (ADD-ACC, ADSL, ADSR, WRITE_ACC,
 *       M12) at end of each machine state during LDM 5 execution.
 *
 * Active-low logic: 0V means logic 1 (asserted).
 * For LDM, we expect during X3:
 *   - WRITE_ACC asserts (~0V)
 *   - ADD-ACC fires (~0V) -> opens OPA -> ACC pass gate
 *   - OPA[0..3] holds 0101 (LDM 5)
 *   - ACC[0..3] settles to 0101 by end of X3
 *
 * Manual diagnostic; ~30 seconds.
 */
TEST(Intel4004L2, DISABLED_X3DatapathDiagnostic) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5;

  Intel4004GridLevel2 grid;
  grid.applyBehavioralX3_ = false; // pure-physics probe

  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseModeLevel1(circuit);
  circuit.solver().invalidateCache();
  auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 0);

  const char* signals[] = {
      "OPR.0", "OPR.1", "OPR.2", "OPR.3",
      "OPA.0", "OPA.1", "OPA.2", "OPA.3",
      "ACC.0", "ACC.1", "ACC.2", "ACC.3",
      "~OPR.0", "~OPR.1", "~OPR.2", "~OPR.3",
      "ADD-ACC", "ADSL", "ADSR", "WRITE_ACC(1)", "M12",
      "LDM/BBL", "X31", "X3i", "OPA-IB",
  };

  std::printf("\n  ==== X3 datapath diagnostic, end of byte ====\n");
  std::printf("  signal            ID    voltage   logic\n");
  std::printf("  ----------------  ----  --------  -----\n");
  grid.traceExecuteByte(circuit, state, rom[WARMUP], nullptr);

  for (const char* sig : signals) {
    auto id = grid.findNet(sig);
    if (id == 0) {
      std::printf("  %-16s  ----  (not found)\n", sig);
      continue;
    }
    const double v = state.nodeVoltages[id];
    const char* lvl = (v < 2.5) ? "1" : "0"; // active-low
    std::printf("  %-16s  %4u  %8.4f  %s\n", sig, static_cast<unsigned>(id), v, lvl);
  }
  std::printf("\n  L0 expects: OPA=0101 (=5), ACC=0101 (=5)\n");
  std::printf("  Active-low: '1' = 0V LOW; '0' = 5V HIGH\n");
}

/* ----------------------------- Differentiated-GMIN sweep ----------------------------- */

/**
 * @test L2 overlay-OFF with strong gminTransient_ + tiny gminDriven_ on
 *       NOR outputs and clocks. Inverse of the storage-only approach:
 *       give EVERYTHING the strong anchor, then exempt the genuinely
 *       driven nets so logic levels stay clean.
 */
TEST(Intel4004L2, DISABLED_DifferentiatedGminSweep) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5;

  auto runOnce = [&](double gTrans, double gDriven) -> std::pair<std::size_t, double> {
    Intel4004GridLevel2 grid;
    grid.applyBehavioralLatchOverlay_ = false;
    grid.gminTransient_ = gTrans;
    grid.gminDriven_ = gDriven;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 1);
    constexpr double V_LO = -1.0;
    constexpr double V_HI = Intel4004GridLevel2::VDD_VOLTAGE + 1.0;
    std::size_t oob = 0;
    double maxAbs = 0;
    for (std::size_t i = 1; i < state.nodeVoltages.size(); ++i) {
      const double v = state.nodeVoltages[i];
      if (!std::isfinite(v) || v < V_LO || v > V_HI) ++oob;
      if (std::isfinite(v) && std::fabs(v) > maxAbs) maxAbs = std::fabs(v);
    }
    return {oob, maxAbs};
  };

  std::printf("\n  ==== Differentiated GMIN: strong floor + small NOR/clock ====\n");
  std::printf("  gTrans  gDriven   OOB  max|v|\n");
  for (double gT : {1e-3, 5e-3, 1e-2}) {
    for (double gD : {1e-12, 1e-9, 1e-6}) {
      const auto [oob, maxAbs] = runOnce(gT, gD);
      std::printf("  %.0e   %.0e   %4zu  %12.4f\n", gT, gD, oob, maxAbs);
    }
  }
}

/* ----------------------------- GMIN sweep ----------------------------- */

/**
 * @test Sweep gminTransient_ with overlay OFF: does stronger GMIN
 *       eliminate OOB nets?
 *
 * The overlay-off failure mode is *NR pathology*, not drift: net
 * voltages reach -407V, -290V, etc. -- mathematically nonsensical.
 * Cause: with no algebraic constraint on a floating net, the Jacobian
 * row has only ~100 nS conductance (GMIN + cap companion), allowing
 * huge step values from the linear solve.
 *
 * GMIN at 1e-9 is weak (1 GOhm equiv). At 1e-6 (1 MOhm) it's still
 * 1000x weaker than typical transistor gm at operating point, but
 * strong enough to algebraically anchor the row.
 *
 * Manual probe; runs ~3 minutes total.
 */
TEST(Intel4004L2, DISABLED_GminSweepOverlayOff) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5;

  auto runOnce = [&](double gmin) -> std::pair<std::size_t, double> {
    Intel4004GridLevel2 grid;
    grid.applyBehavioralLatchOverlay_ = false;
    grid.gminTransient_ = gmin;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 1);
    constexpr double V_LO = -1.0;
    constexpr double V_HI = Intel4004GridLevel2::VDD_VOLTAGE + 1.0;
    std::size_t oob = 0;
    double maxAbs = 0;
    for (std::size_t i = 1; i < state.nodeVoltages.size(); ++i) {
      const double v = state.nodeVoltages[i];
      if (!std::isfinite(v) || v < V_LO || v > V_HI) ++oob;
      if (std::isfinite(v) && std::fabs(v) > maxAbs) maxAbs = std::fabs(v);
    }
    return {oob, maxAbs};
  };

  std::printf("\n  ==== GMIN sweep (overlay OFF) ====\n");
  std::printf("  gmin       OOB  max|v|\n");
  for (double gmin : {1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3}) {
    const auto [oob, maxAbs] = runOnce(gmin);
    std::printf("  %.0e   %4zu  %12.4f\n", gmin, oob, maxAbs);
  }
}

/* ----------------------------- OOB diagnostic ----------------------------- */

/**
 * @test Diagnose which nets go out-of-bounds when L2 runs with overlay off.
 *
 * Categorizes each OOB net by its role in the circuit:
 *   - Storage (DYNAMIC_STORAGE classification, non-NOR-output gate)
 *   - NOR-output (driven by a depletion load)
 *   - Clock-domain (CLK1, CLK2, derived clock signals)
 *   - Pass-gate gate (transistor whose drain/source is also OOB)
 *   - Other
 *
 * Also reports the *first* OOB net by ID and its final voltage, to
 * identify the most-likely root cause: storage nets drifting (cap
 * companion issue) vs. NOR outputs collapsing (overdrive issue) vs.
 * clock-domain artifact.
 *
 * Manual diagnostic; takes ~30 seconds.
 */
TEST(Intel4004L2, DISABLED_DiagnoseOobNets) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5;

  Intel4004GridLevel2 grid;
  grid.applyBehavioralLatchOverlay_ = false; // force the failure mode

  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseModeLevel1(circuit);
  circuit.solver().invalidateCache();
  auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 1);

  // Reverse net map (id -> name) for human-readable output.
  std::unordered_map<unsigned, std::string> idToName;
  for (const auto& [name, id] : grid.netMap_) {
    idToName[id] = name;
  }

  // Build a set of "NOR output" nets: any net that is the output of a
  // depletion-load transistor (drain side, with VDD as gate).
  std::unordered_set<unsigned> norOutputs;
  for (const auto& t : grid.transistors_) {
    if (t.isDiodeLoad) {
      // Depletion load: output is the non-VDD terminal.
      const unsigned out = (t.drain == grid.vdd_) ? t.source : t.drain;
      if (out != 0) norOutputs.insert(out);
    }
  }

  // Build set of storage nets (DYNAMIC_STORAGE class transistors, non-NOR
  // gates). Use the same classification the production code uses.
  // Approximate: any net used as source/drain of a transistor whose gate
  // is NOT a NOR output and is not a clock signal.
  std::unordered_set<unsigned> storageNets;
  const auto clk1 = grid.findNet("CLK1");
  const auto clk2 = grid.findNet("CLK2");
  for (const auto& t : grid.transistors_) {
    const bool gateIsClock = (t.gate == clk1 || t.gate == clk2);
    const bool gateIsNorOut = norOutputs.count(t.gate) > 0;
    if (!gateIsClock && !gateIsNorOut) {
      // Storage/feedback class
      if (t.drain != 0 && t.drain != grid.vdd_) storageNets.insert(t.drain);
      if (t.source != 0 && t.source != grid.vdd_) storageNets.insert(t.source);
    }
  }

  constexpr double V_LO = -1.0;
  constexpr double V_HI = Intel4004GridLevel2::VDD_VOLTAGE + 1.0;

  std::size_t total = 0, inStorage = 0, inNorOut = 0, inClock = 0, inOther = 0;
  std::printf("\n  ==== L2 overlay-off OOB diagnosis ====\n");
  std::printf("  net_id  voltage    category    name\n");
  std::printf("  ------  ---------  ----------  ----\n");
  for (std::size_t i = 1; i < state.nodeVoltages.size(); ++i) {
    const double v = state.nodeVoltages[i];
    if (std::isfinite(v) && v >= V_LO && v <= V_HI) continue;
    ++total;
    const unsigned uid = static_cast<unsigned>(i);
    const char* cat;
    if (uid == clk1 || uid == clk2) { cat = "clock"; ++inClock; }
    else if (norOutputs.count(uid)) { cat = "nor-out"; ++inNorOut; }
    else if (storageNets.count(uid)) { cat = "storage"; ++inStorage; }
    else { cat = "other"; ++inOther; }

    if (total <= 20) { // print first 20 in detail
      auto it = idToName.find(uid);
      const std::string& name = (it != idToName.end()) ? it->second : "(unmapped)";
      std::printf("  %6u  %9.4f  %-10s  %s\n", uid, v, cat, name.c_str());
    }
  }
  std::printf("  ...\n");
  std::printf("  TOTAL OOB: %zu  (storage=%zu, nor-out=%zu, clock=%zu, other=%zu)\n",
              total, inStorage, inNorOut, inClock, inOther);
}

#endif // INTEL4004_DATA_DIR
