/**
 * @file GateLevel_uTest.cpp
 * @brief Unit tests for Intel 4004 gate-level simulator.
 *
 * Tests gate extraction, NOR evaluation, and event propagation
 * using the real SPICE netlist. Fast, focused tests.
 */

#include "src/sim/electronics/chips/intel4004/gate/inc/Intel4004GateLevel.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>

using sim::electronics::chips::intel4004::Gate;
using sim::electronics::chips::intel4004::Intel4004GateLevel;
using sim::electronics::chips::intel4004::loadSpiceNetlist;

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = INTEL4004_DATA_DIR "/lajos-4004.spice";
#else
static const std::string SPICE_PATH = "src/sim/electronics/chips/intel4004/netlist/data/lajos-4004.spice";
#endif

/* ----------------------------- Gate Extraction ----------------------------- */

/** @test Gate extraction finds approximately 427 gates from the netlist. */
TEST(GateLevelExtractionTest, GateCount) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  EXPECT_GT(gl.gateCount(), 400) << "Too few gates extracted";
  EXPECT_LT(gl.gateCount(), 500) << "Too many gates extracted";
}

/** @test Most gates have logic transistors; some are standalone loads. */
TEST(GateLevelExtractionTest, GateLogicCoverage) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  std::size_t withLogic = 0, standalone = 0;
  for (auto& gate : gl.gates_) {
    if (gate.inputNets.empty()) {
      ++standalone;
    } else {
      ++withLogic;
    }
  }

  EXPECT_GT(withLogic, 350) << "Too few gates with logic transistors";
  EXPECT_LT(standalone, 100) << "Too many standalone loads";
}

/** @test Gate types span INV through NOR15 (matching MC calibration results). */
TEST(GateLevelExtractionTest, GateTypeRange) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  std::size_t maxInputs = 0;
  std::size_t invCount = 0;
  for (auto& gate : gl.gates_) {
    if (gate.inputNets.size() > maxInputs) {
      maxInputs = gate.inputNets.size();
    }
    if (gate.inputNets.size() == 1) {
      ++invCount;
    }
  }

  EXPECT_GE(maxInputs, 10) << "Should have gates with 10+ inputs";
  EXPECT_GT(invCount, 0) << "Should have at least one inverter (1-input NOR)";
}

/* ----------------------------- NOR Evaluation ----------------------------- */

/** @test NOR gate truth table: all inputs HIGH -> output HIGH. */
TEST(GateLevelEvaluationTest, NorAllInactive) {
  Gate gate;
  gate.outputNet = 5;
  gate.inputNets = {1, 2, 3};

  std::vector<std::uint8_t> state(10, true);
  EXPECT_TRUE(Intel4004GateLevel::evaluateNor(gate, state));
}

/** @test NOR gate truth table: one input LOW -> output LOW. */
TEST(GateLevelEvaluationTest, NorOneActive) {
  Gate gate;
  gate.outputNet = 5;
  gate.inputNets = {1, 2, 3};

  std::vector<std::uint8_t> state(10, true);
  state[2] = false;
  EXPECT_FALSE(Intel4004GateLevel::evaluateNor(gate, state));
}

/** @test NOR gate truth table: all inputs LOW -> output LOW. */
TEST(GateLevelEvaluationTest, NorAllActive) {
  Gate gate;
  gate.outputNet = 5;
  gate.inputNets = {1, 2, 3};

  std::vector<std::uint8_t> state(10, true);
  state[1] = false;
  state[2] = false;
  state[3] = false;
  EXPECT_FALSE(Intel4004GateLevel::evaluateNor(gate, state));
}

/** @test Inverter (1-input NOR): input LOW -> output HIGH, vice versa. */
TEST(GateLevelEvaluationTest, InverterTruthTable) {
  Gate gate;
  gate.outputNet = 5;
  gate.inputNets = {1};

  std::vector<std::uint8_t> state(10, true);

  state[1] = true;
  EXPECT_TRUE(Intel4004GateLevel::evaluateNor(gate, state));

  state[1] = false;
  EXPECT_FALSE(Intel4004GateLevel::evaluateNor(gate, state));
}

/* ----------------------------- Propagation ----------------------------- */

/** @test Classify gates as dynamic (output connects to pass gate) vs static. */
TEST(GateLevelExtractionTest, DynamicVsStatic) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  std::set<sim::electronics::algorithms::mna::NetID> passGateNets;
  for (auto& pg : gl.passGates_) {
    passGateNets.insert(pg.sourceNet);
    passGateNets.insert(pg.drainNet);
  }

  std::size_t dynamicCount = 0, staticCount = 0;
  for (auto& gate : gl.gates_) {
    if (passGateNets.count(gate.outputNet)) {
      ++dynamicCount;
    } else {
      ++staticCount;
    }
  }
  EXPECT_GT(dynamicCount, 0) << "Should have some dynamic gates";
  EXPECT_GT(staticCount, 0) << "Should have some static gates";
}

/** @test Full circuit propagation converges within reasonable rounds. */
TEST(GateLevelPropagationTest, ConvergesFromReset) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  std::size_t rounds = gl.propagate(200);
  EXPECT_LT(rounds, 200) << "Propagation did not converge in 200 rounds";
}

/** @test Driving a net and propagating changes output gates. */
TEST(GateLevelPropagationTest, DriveAndPropagate) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  auto clk1 = gl.grid_.clk1Net_;
  gl.driveNet(clk1, false);
  std::size_t rounds = gl.propagate();
  EXPECT_GT(rounds, 0) << "Should need at least one propagation round";
}

/* ----------------------------- Accessors ----------------------------- */

/** @test netCount() returns the resolved net count from the grid. */
TEST(GateLevelAccessorsTest, NetCount) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  EXPECT_GT(gl.netCount(), 500) << "4004 has ~1,081 nets";
}

/** @test netValue() returns per-net logic state after build. */
TEST(GateLevelAccessorsTest, NetValue) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  // GND (net 0) is always LOW
  EXPECT_FALSE(gl.netValue(0));
  // Arbitrary non-GND net starts HIGH (reset state)
  EXPECT_TRUE(gl.netValue(1));
}

/** @test readAccumulator() returns 0 in reset state (all nets HIGH = inactive). */
TEST(GateLevelAccessorsTest, AccumulatorAfterReset) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  EXPECT_EQ(gl.readAccumulator(), 0);
}

/** @test readCarry() returns false in reset state. */
TEST(GateLevelAccessorsTest, CarryAfterReset) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  EXPECT_FALSE(gl.readCarry());
}

/** @test Gate::typeString() returns INV for 1-input, NORn for n-input. */
TEST(GateLevelAccessorsTest, TypeString) {
  sim::electronics::chips::intel4004::Gate gate;

  gate.logicIndices = {0};
  EXPECT_EQ(gate.typeString(), "INV");

  gate.logicIndices = {0, 1};
  EXPECT_EQ(gate.typeString(), "NOR2");

  gate.logicIndices = {0, 1, 2, 3};
  EXPECT_EQ(gate.typeString(), "NOR4");
}

/* ----------------------------- Execution ----------------------------- */

/** @test executeMachineState() drives timing and propagates without divergence. */
TEST(GateLevelExecutionTest, SingleMachineState) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  std::size_t rounds = gl.executeMachineState(0, 0x00);
  EXPECT_GT(rounds, 0) << "Should need propagation rounds";
}

/** @test driveDataBus() and releaseDataBus() toggle data bus nets. */
TEST(GateLevelExecutionTest, DataBusDrive) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  // Drive 0xF (all bits set -> all LOW in PMOS)
  gl.driveDataBus(0x0F);
  for (int bit = 0; bit < 4; ++bit) {
    EXPECT_FALSE(gl.netValue(gl.grid_.dataBusNets_[bit]));
  }

  // Release (all HIGH)
  gl.releaseDataBus();
  for (int bit = 0; bit < 4; ++bit) {
    EXPECT_TRUE(gl.netValue(gl.grid_.dataBusNets_[bit]));
  }
}
