/**
 * @file Intel4004Grid_dTest.cpp
 * @brief Development tests for the Intel 4004 transistor-level circuit assembly.
 *
 * Builds the full 2,242-transistor circuit and runs transient simulation;
 * each test takes 50-70 seconds. Manual execution only (not run by `make
 * test`).
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Grid.hpp"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>
#include <string>

using sim::electronics::circuit::Circuit;
using sim::electronics::chips::intel4004::Intel4004Grid;
using sim::electronics::chips::intel4004::Intel4004GridLevel1;
using sim::electronics::chips::intel4004::Intel4004Netlist;
using sim::electronics::chips::intel4004::loadSpiceNetlist;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;

/* ----------------------------- Helpers ----------------------------- */

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = std::string(INTEL4004_DATA_DIR) + "/lajos-4004.spice";
#endif

/* ----------------------------- Build Tests ----------------------------- */

#ifdef INTEL4004_DATA_DIR

/** @test buildCircuit allocates the expected number of nets. */
TEST(Intel4004GridTest, BuildCircuit) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);

  EXPECT_EQ(grid.transistorCount(), 2242);
  EXPECT_GE(grid.netCount(), 1081);
  EXPECT_GE(circuit.netCount(), 1081);
}

/** @test Key signal nets are present in the net map. */
TEST(Intel4004GridTest, NetAllocation) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  grid.buildCircuit(NETLIST);

  EXPECT_NE(grid.findNet("VDD"), 0);
  EXPECT_NE(grid.findNet("D0"), 0);
  EXPECT_NE(grid.findNet("D1"), 0);
  EXPECT_NE(grid.findNet("D2"), 0);
  EXPECT_NE(grid.findNet("D3"), 0);
  EXPECT_NE(grid.findNet("CLK1"), 0);
  EXPECT_NE(grid.findNet("CLK2"), 0);
  EXPECT_NE(grid.findNet("SYNC"), 0);
  EXPECT_NE(grid.findNet("ACC.0"), 0);
  EXPECT_NE(grid.findNet("ACC.1"), 0);
  EXPECT_NE(grid.findNet("ACC.2"), 0);
  EXPECT_NE(grid.findNet("ACC.3"), 0);

  // GND maps to ground (NetID 0)
  EXPECT_EQ(grid.findNet("GND"), 0);
}

/** @test All transistors resolve to valid NetIDs. */
TEST(Intel4004GridTest, TransistorResolution) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  grid.buildCircuit(NETLIST);

  // Every transistor should have resolved drain/gate/source.
  // Gate and source of 0 (ground) are valid for pull-up loads.
  // Drain of 0 would mean connecting drain to ground, which is valid.
  for (std::size_t i = 0; i < grid.transistorCount(); ++i) {
    const auto& T = grid.transistors_[i];
    // At minimum, not all three should be ground (that would be a degenerate transistor)
    bool allGround = (T.drain == 0 && T.gate == 0 && T.source == 0);
    EXPECT_FALSE(allGround) << "Transistor " << i << " has all-ground pins";
  }
}

/** @test Circuit has stamp functions registered. */
TEST(Intel4004GridTest, StampRegistration) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);

  // VDD source + transistor stamp + external IO = 3 stamp functions
  EXPECT_EQ(circuit.stampCount(), 3);
}

/* ----------------------------- DC Solve Tests ----------------------------- */

/** @test Sparse DC operating point converges. */
TEST(Intel4004GridTest, DcOperatingPointConverges) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  TransientState state;
  state.resize(circuit.netCount(), 0);
  auto status = circuit.computeDC(state);

  EXPECT_EQ(status, TransientStatus::SUCCESS);
}

/** @test VDD reads ~5.0V after DC solve. */
TEST(Intel4004GridTest, DcVddVoltage) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  TransientState state;
  state.resize(circuit.netCount(), 0);
  circuit.computeDC(state);

  double vdd = grid.readNetVoltage(state.nodeVoltages, "VDD");
  EXPECT_NEAR(vdd, 5.0, 0.01);
}

/** @test Ground reference is 0V after DC solve. */
TEST(Intel4004GridTest, DcGroundVoltage) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  TransientState state;
  state.resize(circuit.netCount(), 0);
  circuit.computeDC(state);

  // Ground is NetID 0, which is the reference node (always 0V)
  EXPECT_DOUBLE_EQ(state.nodeVoltages[0], 0.0);
}

/** @test DC ADD-ACC control signal with Level1 grid (componentMode=true). */
TEST(Intel4004GridTest, DcAddAccControl) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GridLevel1 grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseModeLevel1(circuit);
  circuit.solver().invalidateCache();

  TransientState state;
  state.resize(circuit.netCount(), 0);
  circuit.computeDC(state);

  double addAcc = grid.readNetVoltage(state.nodeVoltages, "ADD-ACC");
  double writeAcc = grid.readNetVoltage(state.nodeVoltages, "WRITE_ACC(1)");
  double n0477 = grid.readNetVoltage(state.nodeVoltages, "N0477");
  std::printf("  L1 DC: ADD-ACC=%.4f  WRITE_ACC(1)=%.4f  N0477=%.4f\n",
              addAcc, writeAcc, n0477);
  std::printf("  VDD net=%zu  gLoad=%.3e\n", grid.vdd_, grid.bsParams_.gLoad);

  // ADD-ACC should be HIGH (inactive, pulled up by depletion load)
  // when both pull-downs are inactive
  if (writeAcc > 4.0 && n0477 > 4.0) {
    EXPECT_GT(addAcc, 4.0) << "ADD-ACC should be HIGH when pull-downs inactive";
  }
}

/** @test DC operating point voltage distribution with pure Level 1 stamps. */
TEST(Intel4004GridTest, DcVoltageDistribution) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  TransientState state;
  state.resize(circuit.netCount(), 0);
  circuit.computeDC(state);

  // Print ACC net voltages
  std::printf("  DC Operating Point (pure Level 1, %zu transistors):\n", grid.transistorCount());
  for (int i = 0; i < 4; ++i) {
    double v = grid.readNetVoltage(state.nodeVoltages, "ACC." + std::to_string(i));
    std::printf("    ACC.%d = %.4fV\n", i, v);
  }

  // Voltage distribution: count nodes near VDD, near GND, or mid-rail
  int nearVdd = 0, nearGnd = 0, midRail = 0;
  for (std::size_t i = 1; i < state.nodeVoltages.size(); ++i) {
    double v = state.nodeVoltages[i];
    if (v > 4.0) ++nearVdd;
    else if (v < 1.0) ++nearGnd;
    else ++midRail;
  }
  std::printf("    Nodes: %d near VDD (>4V), %d near GND (<1V), %d mid-rail (1-4V)\n",
              nearVdd, nearGnd, midRail);

  // A healthy CMOS/PMOS circuit should have most nodes near VDD or GND
  // Mid-rail equilibrium would show most nodes between 1-4V
  EXPECT_GT(nearVdd + nearGnd, midRail)
      << "More mid-rail nodes than rail nodes suggests mid-rail equilibrium bug";
}

/* ----------------------------- Simulation Tests ----------------------------- */

/** @test NOP chain runs without solver failure. */
TEST(Intel4004GridTest, SimulateNopChain) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  // 8 NOP bytes (0x00)
  const std::array<std::uint8_t, 8> ROM = {0, 0, 0, 0, 0, 0, 0, 0};
  auto state = grid.simulate(circuit, ROM.data(), ROM.size(), 8);

  // Verify simulation completed (8 bytes fetched)
  EXPECT_EQ(grid.bytesFetched(), 8);

  // VDD should still be ~5V after simulation
  double vdd = grid.readNetVoltage(state.nodeVoltages, "VDD");
  EXPECT_NEAR(vdd, 5.0, 0.1);
}

/** @test External IO nets are cached correctly during buildCircuit. */
TEST(Intel4004GridTest, ExternalNetsCached) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  grid.buildCircuit(NETLIST);

  EXPECT_NE(grid.clk1Net_, 0);
  EXPECT_NE(grid.clk2Net_, 0);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_NE(grid.dataBusNets_[i], 0) << "D" << i << " not cached";
    EXPECT_NE(grid.accNets_[i], 0) << "ACC." << i << " not cached";
  }
}

/** @test Binary-switch LDM 5 produces ACC=5.
 *
 * Uses the Intel4004Grid base class which stamps transistors via the
 * three-region binary switch model (ON/OFF/subthreshold conductances).
 * Despite its former name, this test does NOT exercise Level 1 Shichman-
 * Hodges physics -- that requires Intel4004GridLevel1.
 */
TEST(Intel4004GridTest, BinarySwitchLdmAcc5) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  // Warmup NOPs + LDM 5 (opcode 0xD5)
  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = 0xD5; // LDM 5

  auto state = grid.simulate(circuit, rom.data(), rom.size(), rom.size());

  std::printf("  Binary-switch LDM 5 (%zu transistors, %zu bytes):\n",
              grid.transistorCount(), rom.size());
  for (int i = 0; i < 4; ++i) {
    double v = grid.readNetVoltage(state.nodeVoltages, "ACC." + std::to_string(i));
    std::printf("    ACC.%d = %.4fV\n", i, v);
  }

  std::uint8_t acc = grid.readAccumulator(state.nodeVoltages);
  std::printf("    ACC readback = %d (expected 5)\n", acc);

  EXPECT_EQ(acc, 5) << "Binary switch should produce ACC=5 for LDM 5";
}

/** @test 100% Level 1 physics multi-instruction: LDM 5 + NOP + LDM 3.
 *
 * Disables componentMode_, forcing Shichman-Hodges on ALL 2242 transistors
 * including the ~338-transistor latch feedback core. This is the L2 target
 * per SESSION_CONTINUITY; L1 proper keeps behavioral latch-hold for those
 * 338 transistors (componentMode_ = true).
 *
 * DISABLED: depletion-load NOR VOL = 1.20 V sits ~30 mV ABOVE VTH_enh
 * (1.17 V). Cross-coupled latches cannot resolve from mid-rail without
 * subthreshold current. Unblocks after BSIM3/4 device model lands (see
 * src/sim/electronics/devices/nonlinear/FUTURE_MODELS.md and SESSION_CONTINUITY.md).
 */
TEST(Intel4004L2Test, DISABLED_MultiInstructionLdmNopLdm) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);

  constexpr std::size_t WARMUP = 16;
  std::vector<std::uint8_t> rom(WARMUP + 3, 0x00);
  rom[WARMUP] = 0xD5;     // LDM 5
  rom[WARMUP + 1] = 0x00; // NOP
  rom[WARMUP + 2] = 0xD3; // LDM 3

  auto printAccState = [](Intel4004GridLevel1& grid, const auto& state, const char* label) {
    std::printf("  %s: ACC=%d", label, grid.readAccumulator(state.nodeVoltages));
    for (int i = 0; i < 4; ++i) {
      double v = grid.readNetVoltage(state.nodeVoltages, "ACC." + std::to_string(i));
      std::printf("  ACC.%d=%.3f", i, v);
    }
    const char* sigs[] = {"ADD-ACC", "ADD-IB", "OPA-IB", "OPA.0", "N0846", "D0"};
    std::printf("\n    Path:");
    for (const auto* sig : sigs) {
      double v = grid.readNetVoltage(state.nodeVoltages, sig);
      std::printf("  %s=%.2f", sig, v);
    }
    std::printf("\n");
  };

  // After LDM 5
  {
    Intel4004GridLevel1 grid;
    grid.componentMode_ = false; // Pure Level 1 on ALL transistors
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 1);
    printAccState(grid, state, "After LDM 5");
    EXPECT_EQ(grid.readAccumulator(state.nodeVoltages), 5);
  }

  // After NOP
  {
    Intel4004GridLevel1 grid;
    grid.componentMode_ = false;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 2);
    printAccState(grid, state, "After NOP  ");
    EXPECT_EQ(grid.readAccumulator(state.nodeVoltages), 5) << "ACC should hold 5 through NOP";
  }

  // Full sequence
  {
    Intel4004GridLevel1 grid;
    grid.componentMode_ = false;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseModeLevel1(circuit);
    circuit.solver().invalidateCache();
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 3);
    printAccState(grid, state, "After LDM 3");
    EXPECT_EQ(grid.readAccumulator(state.nodeVoltages), 3);
  }
}

/** @test Single byte fetch drives correct number of clock cycles. */
TEST(Intel4004GridTest, SingleByteFetch) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.enableSparseMode(circuit);

  const std::array<std::uint8_t, 1> ROM = {0x00};
  auto state = grid.simulate(circuit, ROM.data(), ROM.size(), 1);

  EXPECT_EQ(grid.bytesFetched(), 1);

  // After simulation, clocks should be in dead zone (both HIGH = inactive in PMOS)
  EXPECT_TRUE(grid.clk1High_);
  EXPECT_TRUE(grid.clk2High_);
}

/** @test Timing generator M12/SYNC at different NOP counts. */
TEST(Intel4004GridTest, TimingGeneratorStartup) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);

  // Test with default convergence (0.01V threshold, 20 steps)
  std::cout << "=== Default convergence (threshold=0.01, steps=20) ===" << std::endl;
  for (std::size_t nops : {1, 4, 8, 16}) {
    std::vector<std::uint8_t> rom(nops, 0x00);
    Intel4004Grid grid;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseMode(circuit);
    auto state = grid.simulate(circuit, rom.data(), rom.size(), nops);

    double m12 = grid.readNetVoltage(state.nodeVoltages, "M12");
    double m22 = grid.readNetVoltage(state.nodeVoltages, "M22");
    double sync = grid.readNetVoltage(state.nodeVoltages, "SYNC");

    std::cout << "NOPs=" << nops << " M12=" << m12 << " M22=" << m22 << " SYNC=" << sync
              << std::endl;
  }

  // Test with NO convergence (threshold=0, 50 steps)
  std::cout << "=== No convergence (threshold=0, steps=50) ===" << std::endl;
  for (std::size_t nops : {4, 16}) {
    std::vector<std::uint8_t> rom(nops, 0x00);
    Intel4004Grid grid;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.enableSparseMode(circuit);
    auto state = grid.simulate(circuit, rom.data(), rom.size(), nops, 1e-6, 50, 0.0);

    double m12 = grid.readNetVoltage(state.nodeVoltages, "M12");
    double m22 = grid.readNetVoltage(state.nodeVoltages, "M22");
    double sync = grid.readNetVoltage(state.nodeVoltages, "SYNC");

    std::cout << "NOPs=" << nops << " M12=" << m12 << " M22=" << m22 << " SYNC=" << sync
              << std::endl;
  }
}

#endif // INTEL4004_DATA_DIR
