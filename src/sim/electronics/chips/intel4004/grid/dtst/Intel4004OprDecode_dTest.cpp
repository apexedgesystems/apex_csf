/**
 * @file Intel4004OprDecode_dTest.cpp
 * @brief OPR latch bit-pattern compliance per Intel datasheet table 8-18.
 *
 * For each of the 16 instruction classes, the datasheet table 8-18
 * specifies the OPR field (D7-D4) bit pattern. After fetching the byte
 * at M1, OPR.0..3 must hold those bits. This test verifies fetch+decode
 * correctness for the entire instruction set at L1.
 *
 * Active-low PMOS convention:
 *   - bit = 1 -> low voltage  (< VDD/2 = 2.5V)
 *   - bit = 0 -> high voltage (>= VDD/2 = 2.5V)
 *
 * Reference: Intel 4004 datasheet, page 8-18 (BASIC INSTRUCTIONS).
 */

#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1.hpp"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel2.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <string>

using sim::electronics::chips::intel4004::Intel4004Cpu;
using sim::electronics::chips::intel4004::Intel4004GridLevel1;
using sim::electronics::chips::intel4004::Intel4004GridLevel2;
using sim::electronics::chips::intel4004::loadSpiceNetlist;

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = INTEL4004_DATA_DIR "/lajos-4004.spice";
#endif


struct InstructionClass {
  std::uint8_t opcode;       ///< representative opcode
  std::uint8_t expectedOpr;  ///< OPR field bits per datasheet 8-18
  const char* mnemonic;
};

// Per datasheet 8-18 BASIC INSTRUCTIONS + 8-19 I/O & RAM INSTRUCTIONS:
// OPR is the upper 4 bits (D7-D4) of the opcode.
constexpr std::array<InstructionClass, 16> INSTRUCTION_CLASSES = {{
    {0x00, 0x0, "NOP"},   // OPR = 0000
    {0x10, 0x1, "JCN"},   // OPR = 0001 (any C1-C4)
    {0x20, 0x2, "FIM"},   // OPR = 0010 (RRR0)
    {0x30, 0x3, "FIN"},   // OPR = 0011 (RRR0)
    {0x40, 0x4, "JUN"},   // OPR = 0100
    {0x50, 0x5, "JMS"},   // OPR = 0101
    {0x60, 0x6, "INC"},   // OPR = 0110
    {0x70, 0x7, "ISZ"},   // OPR = 0111
    {0x80, 0x8, "ADD"},   // OPR = 1000
    {0x90, 0x9, "SUB"},   // OPR = 1001
    {0xA0, 0xA, "LD"},    // OPR = 1010
    {0xB0, 0xB, "XCH"},   // OPR = 1011
    {0xC0, 0xC, "BBL"},   // OPR = 1100
    {0xD0, 0xD, "LDM"},   // OPR = 1101
    {0xE0, 0xE, "IO/RAM"},// OPR = 1110 (WRM, WMP, ..., RD3)
    {0xF0, 0xF, "ACC ops"}, // OPR = 1111 (CLB, CLC, IAC, ..., DCL)
}};

constexpr std::size_t WARMUP = 16;


#ifdef INTEL4004_DATA_DIR

/**
 * @test L1 OPR latch captures correct bit pattern for all 16 instruction
 *       classes per Intel datasheet table 8-18.
 *
 * For each instruction class, drive WARMUP NOPs + 1 byte of the test
 * opcode through L1 (with behavioral OPR sample stub active). Read
 * OPR.0..3 voltages and verify they match the expected bit pattern.
 *
 * This test validates the entire fetch+decode pipeline for the full
 * instruction set at L1.
 */
TEST(Intel4004L1, OprDecodeAllInstructionClasses) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);

  std::size_t passed = 0;
  for (const auto& cls : INSTRUCTION_CLASSES) {
    Intel4004GridLevel1 grid;
    auto circuit = grid.buildCircuit(NETLIST);

    std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
    rom[WARMUP] = cls.opcode;

    // Run warmup, then execute the test byte with a per-phase callback
    // that captures OPR voltages immediately after M1 (high nibble fetch).
    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 0);

    const unsigned oprNets[4] = {grid.findNet("OPR.0"), grid.findNet("OPR.1"),
                                  grid.findNet("OPR.2"), grid.findNet("OPR.3")};

    // Capture OPR right after M1 ends (ms=3 in our 8-step phase model).
    // simulateLevel1 already ran the warmup; now execute the test byte.
    double vOprAfterM1[4]{0, 0, 0, 0};
    bool captured = false;
    auto onPhase = [&](std::uint8_t ms, int /*clkPhase*/,
                       const std::vector<double>& v) {
      // ms=3 = M1 phase. Capture voltages at end of M1 (ph=1) before X1.
      if (ms == 3 && !captured) {
        for (int b = 0; b < 4; ++b) vOprAfterM1[b] = v[oprNets[b]];
        captured = true;
      }
    };
    grid.traceExecuteByte(circuit, state, cls.opcode, onPhase);

    unsigned oprVal = 0;
    for (int b = 0; b < 4; ++b) {
      // Active-low: low voltage = logic 1
      if (vOprAfterM1[b] < 2.5) oprVal |= (1u << b);
    }

    const bool match = (oprVal == cls.expectedOpr);
    std::printf("  %-8s opcode=0x%02X expected_OPR=%X got=%X "
                "V=%4.2f/%4.2f/%4.2f/%4.2f  %s\n",
                cls.mnemonic, cls.opcode, cls.expectedOpr, oprVal,
                vOprAfterM1[0], vOprAfterM1[1], vOprAfterM1[2], vOprAfterM1[3],
                match ? "PASS" : "FAIL");
    if (match) ++passed;

    EXPECT_EQ(oprVal, cls.expectedOpr)
        << cls.mnemonic << " (opcode 0x" << std::hex
        << static_cast<unsigned>(cls.opcode)
        << ") OPR mismatch at end of M1: expected "
        << static_cast<unsigned>(cls.expectedOpr) << " got " << oprVal;
  }

  std::printf("\n  Result: %zu/%zu instruction classes pass OPR decode\n",
              passed, INSTRUCTION_CLASSES.size());
}

/**
 * @test L1 OPA latch captures correct bit pattern for all 16 LDM N values.
 *
 * LDM N (opcode 0xD<N>) has OPR = 1101 (constant) and OPA = N (variable).
 * After M2 (low nibble fetch), OPA.0..3 must hold the bits of N.
 * This validates fetch+decode of the operand field across all 4-bit values.
 *
 * Reference: Intel 4004 datasheet, page 8-18 LDM row.
 */
TEST(Intel4004L1, OpaDecodeAllImmediateValues) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);

  std::size_t passed = 0;
  for (unsigned n = 0; n < 16; ++n) {
    Intel4004GridLevel1 grid;
    auto circuit = grid.buildCircuit(NETLIST);

    const std::uint8_t opcode = static_cast<std::uint8_t>(0xD0 | n);
    std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
    rom[WARMUP] = opcode;

    auto state = grid.simulateLevel1(circuit, rom.data(), rom.size(), WARMUP, 0);

    const unsigned opaNets[4] = {grid.findNet("OPA.0"), grid.findNet("OPA.1"),
                                  grid.findNet("OPA.2"), grid.findNet("OPA.3")};

    // Capture OPA right after M2 ends (ms=4 in our 8-step phase model).
    double vOpaAfterM2[4]{0, 0, 0, 0};
    bool captured = false;
    auto onPhase = [&](std::uint8_t ms, int /*clkPhase*/,
                       const std::vector<double>& v) {
      if (ms == 4 && !captured) {
        for (int b = 0; b < 4; ++b) vOpaAfterM2[b] = v[opaNets[b]];
        captured = true;
      }
    };
    grid.traceExecuteByte(circuit, state, opcode, onPhase);

    unsigned opaVal = 0;
    for (int b = 0; b < 4; ++b) {
      if (vOpaAfterM2[b] < 2.5) opaVal |= (1u << b);
    }

    const bool match = (opaVal == n);
    std::printf("  LDM %2u  opcode=0x%02X expected_OPA=%X got=%X "
                "V=%4.2f/%4.2f/%4.2f/%4.2f  %s\n",
                n, opcode, n, opaVal,
                vOpaAfterM2[0], vOpaAfterM2[1], vOpaAfterM2[2], vOpaAfterM2[3],
                match ? "PASS" : "FAIL");
    if (match) ++passed;

    EXPECT_EQ(opaVal, n)
        << "LDM " << n << " (opcode 0x" << std::hex
        << static_cast<unsigned>(opcode)
        << ") OPA mismatch at end of M2: expected " << n << " got " << opaVal;
  }

  std::printf("\n  Result: %zu/16 LDM N values pass OPA decode\n", passed);
}


/* -------------------------------------------------------------------------
 * Incremental stub-removal probes for L2.
 *
 * L1 has 5 behavioral stubs. L2 disables all 5 at once; the tests below
 * progressively re-enable subsets to isolate which stubs are load-bearing
 * for OPR decode, ACC writeback, and OPR/OPA capture.
 * -----------------------------------------------------------------------*/

/**
 * @test L2 with all behavioral stubs ON must match L1's 16/16 OPR decode.
 *
 * If this passes, the BSIM3 latch core is compatible with the L1 behavioral
 * overlay. If it fails, BSIM3 conflicts with the overlay and must be
 * investigated before progressing to physics-driven configurations.
 */
TEST(Intel4004L2_StubRemoval, AllStubsOn_OprDecode) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  const std::string CAPS_PATH =
      std::string(INTEL4004_DATA_DIR) + "/lajos-4004-bootstrap-caps.txt";

  std::size_t passed = 0;
  for (const auto& cls : INSTRUCTION_CLASSES) {
    Intel4004GridLevel2 grid;
    // Re-enable all behavioral stubs that the L2 ctor disables.
    grid.applyBehavioralLatchOverlay_ = true;  // OPR/OPA sample stubs ON
    grid.applyBehavioralX3_ = true;            // ACC write stub ON

    grid.enableMeyerCaps_ = true;
    grid.gminTransient_ = grid.gminTransientWithCaps_;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.loadBootstrapCaps(CAPS_PATH);

    std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
    rom[WARMUP] = cls.opcode;

    auto state = grid.simulateLevel1FromScratch(
        circuit, rom.data(), rom.size(), WARMUP, 0,
        /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);

    const unsigned oprNets[4] = {grid.findNet("OPR.0"), grid.findNet("OPR.1"),
                                  grid.findNet("OPR.2"), grid.findNet("OPR.3")};

    double vOprAfterM1[4]{0, 0, 0, 0};
    bool captured = false;
    auto onPhase = [&](std::uint8_t ms, int /*clkPhase*/,
                       const std::vector<double>& v) {
      if (ms == 3 && !captured) {
        for (int b = 0; b < 4; ++b) vOprAfterM1[b] = v[oprNets[b]];
        captured = true;
      }
    };
    grid.traceExecuteByte(circuit, state, cls.opcode, onPhase);

    unsigned oprVal = 0;
    for (int b = 0; b < 4; ++b) {
      if (vOprAfterM1[b] < 2.5) oprVal |= (1u << b);
    }

    const bool match = (oprVal == cls.expectedOpr);
    std::printf("  %-8s opcode=0x%02X expected_OPR=%X got=%X "
                "V=%4.2f/%4.2f/%4.2f/%4.2f  %s\n",
                cls.mnemonic, cls.opcode, cls.expectedOpr, oprVal,
                vOprAfterM1[0], vOprAfterM1[1], vOprAfterM1[2], vOprAfterM1[3],
                match ? "PASS" : "FAIL");
    if (match) ++passed;
    EXPECT_EQ(oprVal, cls.expectedOpr) << cls.mnemonic;
  }

  std::printf("\n  result: %zu/%zu OPR-decode classes pass\n",
              passed, INSTRUCTION_CLASSES.size());
}


/**
 * @test L2 closure: all behavioral stubs OFF, all 3 custom L2
 *       primitives ON (LdmAccWriteback + OprCaptureCell + OpaCaptureCell).
 *
 * Pure physics + engineering-justified custom primitives. No
 * L0-authoritative stubs. All decode signals physics-driven; all bus
 * voltages physics-driven; primitives abstract only the non-converging
 * cascades.
 *
 * Pass criterion: ACC == N for all 16 LDM N values.
 *
 * 16/16 PASS confirms the 3 primitives successfully replace the L1
 * 5-stub set for LDM-class instructions.
 */
TEST(Intel4004L2_StubRemoval, FullL2_AllPrimitives_Ldm) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  const std::string CAPS_PATH =
      std::string(INTEL4004_DATA_DIR) + "/lajos-4004-bootstrap-caps.txt";

  std::size_t passed = 0;
  for (unsigned n = 0; n < 16; ++n) {
    Intel4004GridLevel2 grid;
    // Default L2 config (all stubs OFF, all 3 primitives ON).
    ASSERT_FALSE(grid.applyBehavioralLatchOverlay_);
    ASSERT_FALSE(grid.applyBehavioralX3_);
    ASSERT_TRUE(grid.applyL2LdmAccWriteback_);
    ASSERT_TRUE(grid.applyL2OprCaptureCell_);
    ASSERT_TRUE(grid.applyL2OpaCaptureCell_);

    grid.enableMeyerCaps_ = true;
    grid.gminTransient_ = grid.gminTransientWithCaps_;
    auto circuit = grid.buildCircuit(NETLIST);
    grid.loadBootstrapCaps(CAPS_PATH);

    const std::uint8_t opcode = static_cast<std::uint8_t>(0xD0 | n);
    std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
    rom[WARMUP] = opcode;

    auto state = grid.simulateLevel1FromScratch(
        circuit, rom.data(), rom.size(), WARMUP, 0,
        /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);

    grid.traceExecuteByte(circuit, state, opcode, nullptr);
    const std::uint8_t acc = grid.readAccumulator(state.nodeVoltages);

    const bool match = (acc == n);
    std::printf("  LDM %2u  opcode=0x%02X expected ACC=%u got=%u  %s\n",
                n, opcode, n, static_cast<unsigned>(acc),
                match ? "PASS" : "FAIL");
    if (match) ++passed;
    EXPECT_EQ(acc, n) << "Full-L2 LDM " << n << ": failed";
  }

  std::printf("\n  result: %zu/16 LDM N values pass at full L2 "
              "(all primitives, no stubs)\n", passed);
}

/* ----------------------------- Bootstrap-cap-dynamics probe ----------------------------- */


/* ----------------------------- ALU writeback (IAC/CMA/ADD/SUB) ----------------------------- */


template <typename SetupFn, typename CheckFn>
void runAluWritebackByte(std::uint8_t opcode, SetupFn setup, CheckFn check) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  const std::string CAPS_PATH =
      std::string(INTEL4004_DATA_DIR) + "/lajos-4004-bootstrap-caps.txt";

  Intel4004GridLevel2 grid;
  ASSERT_TRUE(grid.applyL2AluWriteback_)
      << "AluWriteback primitive should be on by default at L2";

  grid.enableMeyerCaps_ = true;
  grid.gminTransient_ = grid.gminTransientWithCaps_;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.loadBootstrapCaps(CAPS_PATH);

  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = opcode;

  auto state = grid.simulateLevel1FromScratch(
      circuit, rom.data(), rom.size(), WARMUP, 0,
      /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);

  setup(grid, state);
  grid.traceExecuteByte(circuit, state, opcode, nullptr);
  check(grid, state);
}


/* ----------------------------- Stage A coverage: ACC group + LD + XCH vs L0 ----------------------------- */


/// Run one byte at L2 with seeded ACC/CY/R0, return final ACC and CY
/// from physics. ALL primitives ON (default L2 config).
struct L2RunResult { std::uint8_t acc; bool cy; std::uint8_t r0; };

L2RunResult runOneByteL2(std::uint8_t opcode, std::uint8_t seedAcc,
                         bool seedCy, std::uint8_t seedR0) {
  static const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  const std::string CAPS_PATH =
      std::string(INTEL4004_DATA_DIR) + "/lajos-4004-bootstrap-caps.txt";
  Intel4004GridLevel2 grid;
  grid.enableMeyerCaps_ = true;
  grid.gminTransient_ = grid.gminTransientWithCaps_;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.loadBootstrapCaps(CAPS_PATH);

  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = opcode;
  auto state = grid.simulateLevel1FromScratch(
      circuit, rom.data(), rom.size(), WARMUP, 0,
      /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);
  grid.forceAccLogic(state.nodeVoltages, seedAcc);
  grid.forceCarry(state.nodeVoltages, seedCy);
  grid.forceRegisterValue(state.nodeVoltages, /*reg=*/0, seedR0);
  grid.traceExecuteByte(circuit, state, opcode, nullptr);

  L2RunResult r{};
  r.acc = grid.readAccumulator(state.nodeVoltages);
  r.cy = grid.readCarry(state.nodeVoltages);
  // Read R0 voltages directly
  std::uint8_t reg = 0;
  for (int b = 0; b < 4; ++b) {
    char nm[8]; std::snprintf(nm, sizeof(nm), "R0.%d", b);
    auto id = grid.findNet(nm);
    if (id > 0 && id < state.nodeVoltages.size() &&
        state.nodeVoltages[id] < Intel4004GridLevel2::VDD_VOLTAGE * 0.5) {
      reg |= (1u << b);
    }
  }
  r.r0 = reg;
  return r;
}

/// L0 reference: run one byte starting from the seeded state.
struct L0Ref { std::uint8_t acc; bool cy; std::uint8_t r0; };

L0Ref runOneByteL0(std::uint8_t opcode, std::uint8_t seedAcc,
                   bool seedCy, std::uint8_t seedR0) {
  Intel4004Cpu cpu;
  std::uint8_t prog[1] = {opcode};
  cpu.loadProgram(prog, sizeof(prog));
  cpu.accumulator = seedAcc;
  cpu.carry = seedCy;
  cpu.registers[0] = seedR0;
  cpu.step();
  return {static_cast<std::uint8_t>(cpu.accumulator), cpu.carry,
          static_cast<std::uint8_t>(cpu.registers[0])};
}

struct AccOpCase {
  std::uint8_t opcode;
  const char* mnemonic;
  std::uint8_t acc, r0;
  bool cy;
};


/**
 * @test L2 vs L0 parity sweep across the ACC group + LD + XCH on a
 *       small set of seed values. Each op runs at full L2 (all 4
 *       primitives on, no behavioral stubs); the L2 result must
 *       match L0 byte-for-byte on ACC, CY, and R0.
 */
TEST(Intel4004L2_AluWriteback, StageA_AccGroupVsL0) {
  // Seed values cover edge cases: 0, low, mid, high, 9 (DAA boundary),
  // 0xF (overflow), 0x1/0x2/0x4/0x8 (KBP one-hot), and various.
  const AccOpCase CASES[] = {
      // CLB
      {0xF0, "CLB", 0xA, 0x3, true},
      {0xF0, "CLB", 0x0, 0x0, false},
      // CLC
      {0xF1, "CLC", 0x5, 0x0, true},
      {0xF1, "CLC", 0xF, 0x0, false},
      // CMC
      {0xF3, "CMC", 0x7, 0x0, false},
      {0xF3, "CMC", 0x7, 0x0, true},
      // RAL
      {0xF5, "RAL", 0x5, 0x0, false}, // 0101 -> 1010, cy=0
      {0xF5, "RAL", 0xC, 0x0, true},  // 1100<<1|1=1001, cy=1
      {0xF5, "RAL", 0x0, 0x0, true},  // 0000<<1|1=0001, cy=0
      // RAR
      {0xF6, "RAR", 0xA, 0x0, false}, // 1010 -> 0101, cy=0
      {0xF6, "RAR", 0x3, 0x0, true},  // 0011>>1|1000=1001, cy=1
      // TCC
      {0xF7, "TCC", 0x8, 0x0, false},
      {0xF7, "TCC", 0x8, 0x0, true},
      // DAC
      {0xF8, "DAC", 0x5, 0x0, false}, // 5-1=4, cy=1 (no borrow)
      {0xF8, "DAC", 0x0, 0x0, false}, // 0-1=F, cy=0 (borrow)
      // TCS
      {0xF9, "TCS", 0x8, 0x0, false}, // -> 9
      {0xF9, "TCS", 0x8, 0x0, true},  // -> 10
      // STC
      {0xFA, "STC", 0x3, 0x0, false},
      // DAA
      {0xFB, "DAA", 0x5, 0x0, false}, // <=9, no carry: no change
      {0xFB, "DAA", 0xA, 0x0, false}, // > 9 -> +6 = 0x10 -> 0, cy=1
      {0xFB, "DAA", 0x5, 0x0, true},  // carry set -> +6 = 0xB
      // KBP
      {0xFC, "KBP", 0x0, 0x0, false},
      {0xFC, "KBP", 0x1, 0x0, false},
      {0xFC, "KBP", 0x2, 0x0, false},
      {0xFC, "KBP", 0x4, 0x0, false},
      {0xFC, "KBP", 0x8, 0x0, false},
      {0xFC, "KBP", 0x3, 0x0, false}, // ambiguous -> 0xF
      {0xFC, "KBP", 0xF, 0x0, false},
      // DCL (no ACC change, just sets ramBank_)
      {0xFD, "DCL", 0x5, 0x0, false},
      // LD r0
      {0xA0, "LD",  0x3, 0x9, false},
      {0xA0, "LD",  0xF, 0x0, true},
      // XCH r0
      {0xB0, "XCH", 0x3, 0x9, true}, // ACC<-9, R0<-3, CY unchanged
  };

  std::size_t passed = 0, total = 0;
  for (const auto& c : CASES) {
    ++total;
    const auto l0 = runOneByteL0(c.opcode, c.acc, c.cy, c.r0);
    const auto l2 = runOneByteL2(c.opcode, c.acc, c.cy, c.r0);
    const bool match = (l0.acc == l2.acc) && (l0.cy == l2.cy) &&
                       (l0.r0 == l2.r0);
    std::printf("  %4s  in:acc=%X r0=%X cy=%d  L0:acc=%X cy=%d r0=%X  L2:acc=%X cy=%d r0=%X  %s\n",
                c.mnemonic, c.acc, c.r0, c.cy ? 1 : 0,
                l0.acc, l0.cy ? 1 : 0, l0.r0,
                l2.acc, l2.cy ? 1 : 0, l2.r0,
                match ? "PASS" : "FAIL");
    if (match) ++passed;
    EXPECT_EQ(l2.acc, l0.acc) << c.mnemonic;
    EXPECT_EQ(l2.cy, l0.cy) << c.mnemonic;
    EXPECT_EQ(l2.r0, l0.r0) << c.mnemonic;
  }
  std::printf("\n  Stage A vs L0: %zu/%zu PASS\n", passed, total);
}

/* ----------------------------- Stage B: INC / SRC / BBL / JIN vs L0 ----------------------------- */


struct RegPcRunResult {
  std::uint8_t acc, r0, r1, r2;
  std::uint16_t pc;
  std::uint8_t srcAddress;
};

/// Run one byte at L2 with seeded ACC/CY/registers/PC/stack. Returns
/// post-byte values from physics.
RegPcRunResult runRegPcL2(std::uint8_t opcode, std::uint8_t seedAcc,
                          std::uint8_t seedR0, std::uint8_t seedR1,
                          std::uint8_t seedR2, std::uint16_t seedPc,
                          std::uint16_t seedStack1) {
  static const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  const std::string CAPS_PATH =
      std::string(INTEL4004_DATA_DIR) + "/lajos-4004-bootstrap-caps.txt";
  Intel4004GridLevel2 grid;
  grid.enableMeyerCaps_ = true;
  grid.gminTransient_ = grid.gminTransientWithCaps_;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.loadBootstrapCaps(CAPS_PATH);

  std::vector<std::uint8_t> rom(WARMUP + 1, 0x00);
  rom[WARMUP] = opcode;
  auto state = grid.simulateLevel1FromScratch(
      circuit, rom.data(), rom.size(), WARMUP, 0,
      /*clockPeriod=*/1e-6, /*stepsPerPhase=*/5);

  grid.forceAccLogic(state.nodeVoltages, seedAcc);
  grid.forceRegisterValue(state.nodeVoltages, 0, seedR0);
  grid.forceRegisterValue(state.nodeVoltages, 1, seedR1);
  grid.forceRegisterValue(state.nodeVoltages, 2, seedR2);
  // Seed R3..R15 to 0 so any primitive that reads them (SRC pair 1+,
  // JIN, etc.) sees a deterministic state.
  for (unsigned r = 3; r < 16; ++r) {
    grid.forceRegisterValue(state.nodeVoltages, r, 0);
  }
  grid.forcePcLevel(state.nodeVoltages, 0, seedPc);
  grid.forcePcLevel(state.nodeVoltages, 1, seedStack1);
  grid.forcePcLevel(state.nodeVoltages, 2, 0);
  grid.forcePcLevel(state.nodeVoltages, 3, 0);
  grid.traceExecuteByte(circuit, state, opcode, nullptr);

  RegPcRunResult r{};
  r.acc = grid.readAccumulator(state.nodeVoltages);
  r.r0 = grid.readRegister(state.nodeVoltages, 0);
  r.r1 = grid.readRegister(state.nodeVoltages, 1);
  r.r2 = grid.readRegister(state.nodeVoltages, 2);
  r.pc = grid.readPc(state.nodeVoltages);
  r.srcAddress = grid.srcAddress_;
  return r;
}

struct L0RegPcRef {
  std::uint8_t acc, r0, r1, r2;
  std::uint16_t pc;
  std::uint8_t srcAddress;
};

L0RegPcRef runRegPcL0(std::uint8_t opcode, std::uint8_t seedAcc,
                      std::uint8_t seedR0, std::uint8_t seedR1,
                      std::uint8_t seedR2, std::uint16_t seedPc,
                      std::uint16_t seedStack1) {
  // Build a full-size ROM and place the opcode at seedPc so L0's
  // step() reads the right byte. PC stays as 12-bit address.
  Intel4004Cpu cpu;
  std::vector<std::uint8_t> rom(0x1000, 0x00);
  rom[seedPc & 0xFFF] = opcode;
  cpu.loadProgram(rom.data(), rom.size());
  cpu.accumulator = seedAcc;
  cpu.registers[0] = seedR0;
  cpu.registers[1] = seedR1;
  cpu.registers[2] = seedR2;
  cpu.pc = seedPc;
  cpu.stack[0] = seedStack1;
  cpu.sp = 1; // one entry on stack
  cpu.step();
  return {static_cast<std::uint8_t>(cpu.accumulator),
          static_cast<std::uint8_t>(cpu.registers[0]),
          static_cast<std::uint8_t>(cpu.registers[1]),
          static_cast<std::uint8_t>(cpu.registers[2]),
          cpu.pc,
          static_cast<std::uint8_t>(cpu.srcAddress)};
}


TEST(Intel4004L2_RegPcWriteback, StageB_RegPcVsL0) {
  // Each row: opcode, mnemonic, seedAcc, R0, R1, R2, PC, STK1, what to compare
  struct Case {
    std::uint8_t opcode;
    const char* mnemonic;
    std::uint8_t acc, r0, r1, r2;
    std::uint16_t pc, stk1;
    bool checkAcc, checkR0, checkR1, checkR2, checkPc, checkSrc;
  };
  const Case CASES[] = {
      // INC R0
      {0x60, "INC0", 0x5, 0x3, 0x0, 0x0, 0x100, 0, false, true, false, false, false, false},
      {0x60, "INC0", 0x5, 0xF, 0x0, 0x0, 0x100, 0, false, true, false, false, false, false},
      // INC R2
      {0x62, "INC2", 0x0, 0x0, 0x0, 0xA, 0x100, 0, false, false, false, true, false, false},
      // SRC pair0 (opcode 0x21): srcAddress = (R0<<4)|R1
      {0x21, "SRC0", 0x0, 0x3, 0xC, 0x0, 0x100, 0, false, false, false, false, false, true},
      // SRC pair1 (opcode 0x23): srcAddress = (R2<<4)|R3 (R3 not seeded -> 0)
      {0x23, "SRC1", 0x0, 0x0, 0x0, 0x9, 0x100, 0, false, false, false, false, false, true},
      // JIN pair0 (opcode 0x31): PC[7:0] = (R0<<4)|R1, PC[11:8] keeps
      {0x31, "JIN0", 0x0, 0x4, 0x2, 0x0, 0x500, 0, false, false, false, false, true, false},
      // BBL N: ACC = N, PC = stack[1]
      {0xC7, "BBL7", 0x0, 0x0, 0x0, 0x0, 0x100, 0x250, true, false, false, false, true, false},
      {0xC0, "BBL0", 0xF, 0x0, 0x0, 0x0, 0x300, 0x123, true, false, false, false, true, false},
  };

  std::size_t passed = 0, total = 0;
  for (const auto& c : CASES) {
    ++total;
    const auto l0 = runRegPcL0(c.opcode, c.acc, c.r0, c.r1, c.r2, c.pc, c.stk1);
    const auto l2 = runRegPcL2(c.opcode, c.acc, c.r0, c.r1, c.r2, c.pc, c.stk1);
    bool match = true;
    if (c.checkAcc && l0.acc != l2.acc) match = false;
    if (c.checkR0 && l0.r0 != l2.r0) match = false;
    if (c.checkR1 && l0.r1 != l2.r1) match = false;
    if (c.checkR2 && l0.r2 != l2.r2) match = false;
    if (c.checkPc && l0.pc != l2.pc) match = false;
    if (c.checkSrc && l0.srcAddress != l2.srcAddress) match = false;
    std::printf("  %s  L0[acc=%X r0=%X r1=%X r2=%X pc=%03X src=%02X]  "
                "L2[acc=%X r0=%X r1=%X r2=%X pc=%03X src=%02X]  %s\n",
                c.mnemonic,
                l0.acc, l0.r0, l0.r1, l0.r2, l0.pc, l0.srcAddress,
                l2.acc, l2.r0, l2.r1, l2.r2, l2.pc, l2.srcAddress,
                match ? "PASS" : "FAIL");
    if (match) ++passed;
    if (c.checkAcc) EXPECT_EQ(l2.acc, l0.acc) << c.mnemonic << " ACC";
    if (c.checkR0) EXPECT_EQ(l2.r0, l0.r0) << c.mnemonic << " R0";
    if (c.checkR1) EXPECT_EQ(l2.r1, l0.r1) << c.mnemonic << " R1";
    if (c.checkR2) EXPECT_EQ(l2.r2, l0.r2) << c.mnemonic << " R2";
    if (c.checkPc) EXPECT_EQ(l2.pc, l0.pc) << c.mnemonic << " PC";
    if (c.checkSrc) EXPECT_EQ(l2.srcAddress, l0.srcAddress) << c.mnemonic << " SRC";
  }
  std::printf("\n  Stage B vs L0: %zu/%zu PASS\n", passed, total);
}

#endif  // INTEL4004_DATA_DIR
