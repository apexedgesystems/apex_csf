/**
 * @file GateLevelExecution_dTest.cpp
 * @brief Component-level tests for Intel 4004 gate-level instruction execution.
 *
 * Verifies that the gate-level simulator (Level 2) produces the same
 * functional results as the behavioral CPU (Level 0) and binary switch
 * circuit (Level 1) for the same machine code.
 */

#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/intel4004/behavioral/inc/Intel4004Programs.hpp"
#include "src/sim/electronics/intel4004/gate/inc/Intel4004GateLevel.hpp"
#include "src/sim/electronics/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>

using sim::electronics::intel4004::Intel4004Cpu;
using sim::electronics::intel4004::Intel4004GateLevel;
using sim::electronics::intel4004::loadSpiceNetlist;

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = INTEL4004_DATA_DIR "/lajos-4004.spice";
#else
static const std::string SPICE_PATH = "src/sim/electronics/intel4004/netlist/data/lajos-4004.spice";
#endif

static constexpr std::size_t WARMUP_NOPS = 16;

/* ----------------------------- LDM Tests ----------------------------- */

/** @test Gate-level LDM 5: ACC should be 5, matching behavioral Level 0. */
TEST(GateLevelExecution, LdmAccumulator) {
  using namespace sim::electronics::intel4004;

  Intel4004Cpu behavioral;
  behavioral.loadProgram(PROGRAM_LDM.data(), PROGRAM_LDM.size());
  behavioral.run(1);

  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GateLevel gl;
  gl.build(NETLIST);

  std::vector<std::uint8_t> rom(WARMUP_NOPS + PROGRAM_LDM.size());
  std::fill(rom.begin(), rom.begin() + WARMUP_NOPS, 0x00);
  std::copy(PROGRAM_LDM.begin(), PROGRAM_LDM.end(), rom.begin() + WARMUP_NOPS);

  std::size_t totalBytes = WARMUP_NOPS + PROGRAM_LDM.size();
  for (std::size_t byteIdx = 0; byteIdx < totalBytes; ++byteIdx) {
    std::uint8_t romByte = (byteIdx < rom.size()) ? rom[byteIdx] : 0x00;
    for (std::uint8_t state = 0; state < 8; ++state) {
      gl.executeMachineState(state, romByte);

      if (byteIdx == 0 || byteIdx == WARMUP_NOPS) {
        const char* stateNames[] = {"A1", "A2", "A3", "M1", "M2", "X1", "X2", "X3"};
        auto addIb = gl.grid_.findNet("ADD-IB");
        auto addAcc = gl.grid_.findNet("ADD-ACC");
        auto opaIb = gl.grid_.findNet("OPA-IB");
        auto writeAcc = gl.grid_.findNet("WRITE_ACC(1)");
        auto ldmBbl = gl.grid_.findNet("LDM/BBL");
        std::cout << "  " << stateNames[state]
                  << ": D0=" << (gl.netValue(gl.grid_.dataBusNets_[0]) ? "H" : "L")
                  << " ADD-IB=" << (addIb > 0 ? (gl.netValue(addIb) ? "H" : "L") : "?")
                  << " ADD-ACC=" << (addAcc > 0 ? (gl.netValue(addAcc) ? "H" : "L") : "?")
                  << " OPA-IB=" << (opaIb > 0 ? (gl.netValue(opaIb) ? "H" : "L") : "?")
                  << " WR_ACC=" << (writeAcc > 0 ? (gl.netValue(writeAcc) ? "H" : "L") : "?")
                  << " LDM=" << (ldmBbl > 0 ? (gl.netValue(ldmBbl) ? "H" : "L") : "?")
                  << " ACC=" << +gl.readAccumulator() << "\n";
      }
    }
  }
  std::uint8_t gateAcc = gl.readAccumulator();

  std::cout << "  Behavioral ACC = " << +behavioral.accumulator << "\n";
  std::cout << "  Gate-level ACC = " << +gateAcc << "\n";
  std::cout << "  Pass gates: " << gl.passGates_.size() << "\n";

  const char* probeNets[] = {
      "D0",    "D1",    "D2",    "D3",     "OPA.0",   "OPA.1", "OPA.2", "OPA.3", "ACC.0",
      "ACC.1", "ACC.2", "ACC.3", "OPA-IB", "ADD-ACC", "M12",   "M22",   "SYNC",  "~(X21&~CLK2)",
      "N0351", "N0415", "N0448", "CLK1",   "CLK2",    "A12",   "X22",   "X32",   "S00716"};
  for (auto name : probeNets) {
    auto net = gl.grid_.findNet(name);
    if (net > 0) {
      std::cout << "  " << name << " = " << (gl.netValue(net) ? "HIGH" : "LOW") << "\n";
    }
  }

  EXPECT_EQ(behavioral.accumulator, gateAcc)
      << "Gate-level ACC mismatch: behavioral=" << +behavioral.accumulator << " gate=" << +gateAcc;
}
