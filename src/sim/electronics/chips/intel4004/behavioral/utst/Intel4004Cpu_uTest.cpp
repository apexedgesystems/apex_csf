/**
 * @file Intel4004Cpu_uTest.cpp
 * @brief Unit tests for the Intel 4004 behavioral CPU model.
 */

#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Cpu.hpp"
#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Instructions.hpp"
#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Programs.hpp"

#include <gtest/gtest.h>

using sim::electronics::chips::intel4004::Intel4004Cpu;
using namespace sim::electronics::chips::intel4004;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default state is power-on: all zeros, not halted. */
TEST(Intel4004CpuTest, DefaultConstruction) {
  Intel4004Cpu cpu;
  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_FALSE(cpu.carry);
  EXPECT_EQ(cpu.pc, 0);
  EXPECT_EQ(cpu.sp, 0);
  EXPECT_FALSE(cpu.halted);
  EXPECT_EQ(cpu.cyclesExecuted, 0);
  EXPECT_EQ(cpu.instructionsExecuted, 0);

  for (std::size_t i = 0; i < Intel4004Cpu::NUM_REGISTERS; ++i) {
    EXPECT_EQ(cpu.registers[i], 0);
  }
}

/* ----------------------------- NOP ----------------------------- */

/** @test NOP advances PC by 1 without modifying state. */
TEST(Intel4004CpuTest, Nop) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_NOP.data(), PROGRAM_NOP.size());

  cpu.step();
  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_EQ(cpu.pc, 1);
  EXPECT_FALSE(cpu.carry);
  EXPECT_EQ(cpu.instructionsExecuted, 1);
}

/** @test Multiple NOPs advance PC sequentially. */
TEST(Intel4004CpuTest, NopChain) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_NOP.data(), PROGRAM_NOP.size());

  for (int i = 0; i < 8; ++i) {
    cpu.step();
  }
  EXPECT_EQ(cpu.pc, 8);
  EXPECT_EQ(cpu.instructionsExecuted, 8);
}

/* ----------------------------- LDM ----------------------------- */

/** @test LDM loads 4-bit immediate into accumulator. */
TEST(Intel4004CpuTest, Ldm) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_LDM.data(), PROGRAM_LDM.size());

  cpu.step();
  EXPECT_EQ(cpu.accumulator, 5);
  EXPECT_EQ(cpu.pc, 1);
}

/** @test LDM with all 16 values. */
TEST(Intel4004CpuTest, LdmAllValues) {
  for (std::uint8_t val = 0; val < 16; ++val) {
    Intel4004Cpu cpu;
    const std::uint8_t INSTR = encodeLDM(val);
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, val);
  }
}

/* ----------------------------- FIM ----------------------------- */

/** @test FIM loads 8-bit immediate into register pair. */
TEST(Intel4004CpuTest, Fim) {
  Intel4004Cpu cpu;
  const std::uint8_t PROG[] = {encodeFIM(0), 0xAB};
  cpu.loadProgram(PROG, 2);

  cpu.step();
  EXPECT_EQ(cpu.registers[0], 0xA); // R0 = high nibble
  EXPECT_EQ(cpu.registers[1], 0xB); // R1 = low nibble
  EXPECT_EQ(cpu.pc, 2);
}

/** @test FIM to all 8 register pairs. */
TEST(Intel4004CpuTest, FimAllPairs) {
  for (std::uint8_t p = 0; p < 8; ++p) {
    Intel4004Cpu cpu;
    const std::uint8_t DATA = static_cast<std::uint8_t>((p << 4) | (p + 1));
    const std::uint8_t PROG[] = {encodeFIM(p), DATA};
    cpu.loadProgram(PROG, 2);
    cpu.step();

    const std::uint8_t IDX = p * 2;
    EXPECT_EQ(cpu.registers[IDX], p);
    EXPECT_EQ(cpu.registers[IDX + 1], (p + 1) & 0xF);
  }
}

/* ----------------------------- LD ----------------------------- */

/** @test LD copies register to accumulator. */
TEST(Intel4004CpuTest, Ld) {
  Intel4004Cpu cpu;
  cpu.registers[5] = 0xC;
  const std::uint8_t INSTR = encodeLD(5);
  cpu.loadProgram(&INSTR, 1);

  cpu.step();
  EXPECT_EQ(cpu.accumulator, 0xC);
}

/* ----------------------------- ADD ----------------------------- */

/** @test ADD program: 3 + 5 = 8, no carry. */
TEST(Intel4004CpuTest, AddRegisters) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_ADD.data(), PROGRAM_ADD.size());

  cpu.step(); // FIM P0, 0x35
  cpu.step(); // LD R0 -> ACC = 3
  cpu.step(); // ADD R1 -> ACC = 3 + 5 + 0 = 8

  EXPECT_EQ(cpu.accumulator, 8);
  EXPECT_FALSE(cpu.carry);
}

/** @test ADD with carry input. */
TEST(Intel4004CpuTest, AddWithCarryIn) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 7;
  cpu.accumulator = 8;
  cpu.carry = true;

  const std::uint8_t INSTR = encodeADD(0);
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  // 8 + 7 + 1 = 16 -> ACC = 0, carry = 1
  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_TRUE(cpu.carry);
}

/** @test ADD overflow produces carry. */
TEST(Intel4004CpuTest, AddOverflow) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 0xF;
  cpu.accumulator = 0x1;

  const std::uint8_t INSTR = encodeADD(0);
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  // 1 + 15 + 0 = 16 -> ACC = 0, carry = 1
  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_TRUE(cpu.carry);
}

/* ----------------------------- SUB ----------------------------- */

/** @test SUB program: 9 - 3 = 6 with STC (no borrow). */
TEST(Intel4004CpuTest, SubRegisters) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_SUB.data(), PROGRAM_SUB.size());

  cpu.step(); // FIM P0, 0x93
  cpu.step(); // LD R0 -> ACC = 9
  cpu.step(); // STC -> carry = 1
  cpu.step(); // SUB R1 -> ACC = 9 + ~3 + 1 = 9 + 12 + 1 = 22 -> ACC = 6, carry = 1

  EXPECT_EQ(cpu.accumulator, 6);
  EXPECT_TRUE(cpu.carry); // No borrow
}

/** @test SUB with borrow (carry=0): 3 - 9 -> underflow. */
TEST(Intel4004CpuTest, SubWithBorrow) {
  Intel4004Cpu cpu;
  cpu.accumulator = 3;
  cpu.registers[0] = 9;
  cpu.carry = true; // No borrow input

  const std::uint8_t INSTR = encodeSUB(0);
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  // 3 + ~9 + 1 = 3 + 6 + 1 = 10 -> ACC = 10, carry = 0 (borrow)
  EXPECT_EQ(cpu.accumulator, 10);
  EXPECT_FALSE(cpu.carry); // Borrow occurred
}

/* ----------------------------- XCH ----------------------------- */

/** @test XCH exchanges accumulator and register. */
TEST(Intel4004CpuTest, Xch) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_XCH.data(), PROGRAM_XCH.size());

  cpu.step(); // FIM P0, 0x70 -> R0=7
  cpu.step(); // LDM 3 -> ACC=3
  cpu.step(); // XCH R0 -> ACC=7, R0=3

  EXPECT_EQ(cpu.accumulator, 7);
  EXPECT_EQ(cpu.registers[0], 3);
}

/* ----------------------------- INC ----------------------------- */

/** @test INC increments register with 4-bit wrap. */
TEST(Intel4004CpuTest, Inc) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_INC.data(), PROGRAM_INC.size());

  cpu.step(); // FIM P0, 0xE0 -> R0=14
  cpu.step(); // INC R0 -> 15
  EXPECT_EQ(cpu.registers[0], 15);

  cpu.step(); // INC R0 -> 0 (wrap)
  EXPECT_EQ(cpu.registers[0], 0);

  cpu.step(); // INC R0 -> 1
  EXPECT_EQ(cpu.registers[0], 1);
}

/* ----------------------------- JUN ----------------------------- */

/** @test JUN jumps to 12-bit address. */
TEST(Intel4004CpuTest, Jun) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_JUN.data(), PROGRAM_JUN.size());

  cpu.step(); // LDM 1 -> ACC=1, PC=1
  EXPECT_EQ(cpu.accumulator, 1);

  cpu.step(); // JUN 0x000 -> PC=0
  EXPECT_EQ(cpu.pc, 0);
}

/* ----------------------------- JCN ----------------------------- */

/** @test JCN jumps when accumulator is zero. */
TEST(Intel4004CpuTest, JcnAccZero) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_JCN.data(), PROGRAM_JCN.size());

  cpu.step(); // LDM 0 -> ACC=0
  EXPECT_EQ(cpu.accumulator, 0);

  cpu.step(); // JCN 4, 0x06 -> condition true (ACC==0), jump to 0x06
  EXPECT_EQ(cpu.pc, 6);

  cpu.step(); // LDM 2 at address 6
  EXPECT_EQ(cpu.accumulator, 2);
}

/** @test JCN does not jump when condition is false. */
TEST(Intel4004CpuTest, JcnNoJump) {
  Intel4004Cpu cpu;
  const std::uint8_t PROG[] = {
      encodeLDM(5), // ACC = 5 (nonzero)
      encodeJCN(COND_ACC_ZERO),
      0x05,         // Jump if ACC == 0 (false)
      encodeLDM(9), // Should execute (no jump)
      NOP,
      encodeLDM(0) // Should not reach via jump
  };
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // LDM 5
  cpu.step(); // JCN -> no jump, PC = 3
  EXPECT_EQ(cpu.pc, 3);

  cpu.step(); // LDM 9
  EXPECT_EQ(cpu.accumulator, 9);
}

/** @test JCN with inverted condition. */
TEST(Intel4004CpuTest, JcnInverted) {
  Intel4004Cpu cpu;
  const std::uint8_t PROG[] = {
      encodeLDM(5), // Addr 0: ACC = 5
      encodeJCN(COND_INVERT | COND_ACC_ZERO),
      0x06,         // Addr 1-2: Jump if ACC != 0
      encodeLDM(0), // Addr 3: Skipped
      NOP,          // Addr 4: Skipped
      NOP,          // Addr 5: Skipped
      encodeLDM(8)  // Addr 6: Jumped here
  };
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // LDM 5
  cpu.step(); // JCN inverted -> ACC != 0, jump to 0x06
  EXPECT_EQ(cpu.pc, 6);

  cpu.step(); // LDM 8
  EXPECT_EQ(cpu.accumulator, 8);
}

/* ----------------------------- JMS / BBL ----------------------------- */

/** @test JMS calls subroutine, BBL returns with data. */
TEST(Intel4004CpuTest, Subroutine) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_SUBROUTINE.data(), PROGRAM_SUBROUTINE.size());

  cpu.step(); // JMS 0x004 -> push return (2), jump to 4
  EXPECT_EQ(cpu.pc, 4);

  cpu.step(); // LDM 7 at address 4 -> ACC = 7
  EXPECT_EQ(cpu.accumulator, 7);

  cpu.step(); // BBL 3 at address 5 -> ACC = 3, return to address 2
  EXPECT_EQ(cpu.pc, 2);
  EXPECT_EQ(cpu.accumulator, 3);

  cpu.step(); // LDM 0 at address 2
  EXPECT_EQ(cpu.accumulator, 0);
}

/** @test Nested subroutine calls use stack correctly. */
TEST(Intel4004CpuTest, NestedSubroutines) {
  Intel4004Cpu cpu;
  // Addr 0: JMS 0x004     (call sub1)
  // Addr 2: LDM 0          (sentinel after outer return)
  // Addr 3: NOP
  // Addr 4: JMS 0x008     (sub1: call sub2)
  // Addr 6: BBL 1          (sub1: return with 1)
  // Addr 7: NOP
  // Addr 8: LDM 9          (sub2: ACC = 9)
  // Addr 9: BBL 2          (sub2: return with 2)
  const std::uint8_t PROG[] = {
      encodeJMS(0x004),
      0x04,         // Call sub1
      encodeLDM(0), // After return
      NOP,
      encodeJMS(0x008),
      0x08,         // sub1: call sub2
      encodeBBL(1), // sub1: return with 1
      NOP,
      encodeLDM(9), // sub2: ACC = 9
      encodeBBL(2)  // sub2: return with 2
  };
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // JMS 0x004
  EXPECT_EQ(cpu.pc, 4);

  cpu.step(); // JMS 0x008 (nested call)
  EXPECT_EQ(cpu.pc, 8);

  cpu.step(); // LDM 9
  EXPECT_EQ(cpu.accumulator, 9);

  cpu.step(); // BBL 2 -> return to sub1 (addr 6), ACC = 2
  EXPECT_EQ(cpu.pc, 6);
  EXPECT_EQ(cpu.accumulator, 2);

  cpu.step(); // BBL 1 -> return to main (addr 2), ACC = 1
  EXPECT_EQ(cpu.pc, 2);
  EXPECT_EQ(cpu.accumulator, 1);
}

/* ----------------------------- ISZ ----------------------------- */

/** @test ISZ counting loop: R0 counts 0 through 15, wraps to 0. */
TEST(Intel4004CpuTest, CountingLoop) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_COUNTING_LOOP.data(), PROGRAM_COUNTING_LOOP.size());

  for (int i = 0; i < 17; ++i) {
    cpu.step();
  }

  EXPECT_EQ(cpu.registers[0], 0);          // R0 wrapped back to 0
  EXPECT_EQ(cpu.pc, 4);                    // Fell through ISZ
  EXPECT_EQ(cpu.instructionsExecuted, 17); // 1 FIM + 16 ISZ
}

/** @test ISZ jumps when register is not zero after increment. */
TEST(Intel4004CpuTest, IszJumps) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 5;
  const std::uint8_t PROG[] = {encodeISZ(0), 0x00};
  cpu.loadProgram(PROG, 2);

  cpu.step();
  EXPECT_EQ(cpu.registers[0], 6);
  EXPECT_EQ(cpu.pc, 0); // Jumped back to 0x00
}

/** @test ISZ falls through when register wraps to zero. */
TEST(Intel4004CpuTest, IszFallsThrough) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 15;
  const std::uint8_t PROG[] = {encodeISZ(0), 0x00, NOP};
  cpu.loadProgram(PROG, 3);

  cpu.step();
  EXPECT_EQ(cpu.registers[0], 0);
  EXPECT_EQ(cpu.pc, 2); // Fell through
}

/* ----------------------------- Accumulator Operations ----------------------------- */

/** @test Accumulator operation chain: LDM->IAC->CMA->RAL->RAR->CLB->STC->TCC. */
TEST(Intel4004CpuTest, AccumulatorOpsChain) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_ACC_OPS.data(), PROGRAM_ACC_OPS.size());

  cpu.step(); // LDM 5
  EXPECT_EQ(cpu.accumulator, 5);
  EXPECT_FALSE(cpu.carry);

  cpu.step(); // IAC -> 6
  EXPECT_EQ(cpu.accumulator, 6);
  EXPECT_FALSE(cpu.carry);

  cpu.step(); // CMA -> ~6 & 0xF = 9
  EXPECT_EQ(cpu.accumulator, 9);

  cpu.step(); // RAL -> (1001 << 1 | 0) & 0xF = 0010, carry = 1
  EXPECT_EQ(cpu.accumulator, 2);
  EXPECT_TRUE(cpu.carry);

  cpu.step(); // RAR -> (0010 >> 1 | 1<<3) = 1001, carry = 0
  EXPECT_EQ(cpu.accumulator, 9);
  EXPECT_FALSE(cpu.carry);

  cpu.step(); // CLB -> ACC = 0, carry = 0
  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_FALSE(cpu.carry);

  cpu.step(); // STC -> carry = 1
  EXPECT_TRUE(cpu.carry);

  cpu.step(); // TCC -> ACC = 1, carry = 0
  EXPECT_EQ(cpu.accumulator, 1);
  EXPECT_FALSE(cpu.carry);
}

/** @test CLB clears both accumulator and carry. */
TEST(Intel4004CpuTest, Clb) {
  Intel4004Cpu cpu;
  cpu.accumulator = 0xF;
  cpu.carry = true;
  const std::uint8_t INSTR = CLB;
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_FALSE(cpu.carry);
}

/** @test CLC clears carry only. */
TEST(Intel4004CpuTest, Clc) {
  Intel4004Cpu cpu;
  cpu.accumulator = 5;
  cpu.carry = true;
  const std::uint8_t INSTR = CLC;
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  EXPECT_EQ(cpu.accumulator, 5);
  EXPECT_FALSE(cpu.carry);
}

/** @test IAC overflow wraps and sets carry. */
TEST(Intel4004CpuTest, IacOverflow) {
  Intel4004Cpu cpu;
  cpu.accumulator = 0xF;
  const std::uint8_t INSTR = IAC;
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_TRUE(cpu.carry);
}

/** @test DAC decrement with and without borrow. */
TEST(Intel4004CpuTest, Dac) {
  // No borrow: 5 -> 4
  {
    Intel4004Cpu cpu;
    cpu.accumulator = 5;
    const std::uint8_t INSTR = DAC;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, 4);
    EXPECT_TRUE(cpu.carry); // No borrow
  }

  // Borrow: 0 -> 15
  {
    Intel4004Cpu cpu;
    cpu.accumulator = 0;
    const std::uint8_t INSTR = DAC;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, 15);
    EXPECT_FALSE(cpu.carry); // Borrow
  }
}

/** @test CMC complements carry. */
TEST(Intel4004CpuTest, Cmc) {
  Intel4004Cpu cpu;
  cpu.carry = false;
  const std::uint8_t INSTR = CMC;
  cpu.loadProgram(&INSTR, 1);
  cpu.step();
  EXPECT_TRUE(cpu.carry);

  cpu.reset();
  cpu.carry = true;
  cpu.loadProgram(&INSTR, 1);
  cpu.step();
  EXPECT_FALSE(cpu.carry);
}

/** @test TCS transfers 10 if carry, 9 if no carry. */
TEST(Intel4004CpuTest, Tcs) {
  {
    Intel4004Cpu cpu;
    cpu.carry = true;
    const std::uint8_t INSTR = TCS;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, 10);
    EXPECT_FALSE(cpu.carry);
  }
  {
    Intel4004Cpu cpu;
    cpu.carry = false;
    const std::uint8_t INSTR = TCS;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, 9);
    EXPECT_FALSE(cpu.carry);
  }
}

/** @test KBP one-hot to binary conversion. */
TEST(Intel4004CpuTest, Kbp) {
  const std::uint8_t EXPECTED[] = {0, 1,   2,   0xF, 3,   0xF, 0xF, 0xF,
                                   4, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF, 0xF};
  for (std::uint8_t val = 0; val < 16; ++val) {
    Intel4004Cpu cpu;
    cpu.accumulator = val;
    const std::uint8_t INSTR = KBP;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, EXPECTED[val]) << "KBP(" << static_cast<int>(val) << ")";
  }
}

/** @test DAA decimal adjust after BCD addition. */
TEST(Intel4004CpuTest, Daa) {
  // 7 + 5 = 12 -> BCD: ACC = 2, carry = 0 from addition, then DAA adds 6 -> 8
  // Wait -- let's do a simpler test: ACC=12 (0xC), carry=0 -> DAA adds 6 -> 0x2, carry=1
  {
    Intel4004Cpu cpu;
    cpu.accumulator = 0xC; // 12 > 9
    cpu.carry = false;
    const std::uint8_t INSTR = DAA;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    // 12 + 6 = 18 -> ACC = 2, carry = 1
    EXPECT_EQ(cpu.accumulator, 2);
    EXPECT_TRUE(cpu.carry);
  }

  // ACC=5, carry=0 -> no adjustment
  {
    Intel4004Cpu cpu;
    cpu.accumulator = 5;
    cpu.carry = false;
    const std::uint8_t INSTR = DAA;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, 5);
    EXPECT_FALSE(cpu.carry);
  }

  // ACC=3, carry=1 -> DAA adds 6 -> 9, carry stays 1
  {
    Intel4004Cpu cpu;
    cpu.accumulator = 3;
    cpu.carry = true;
    const std::uint8_t INSTR = DAA;
    cpu.loadProgram(&INSTR, 1);
    cpu.step();
    EXPECT_EQ(cpu.accumulator, 9);
    EXPECT_TRUE(cpu.carry);
  }
}

/* ----------------------------- SRC / DCL / RAM ----------------------------- */

/** @test SRC sets the address register from a register pair. */
TEST(Intel4004CpuTest, Src) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 0xA;
  cpu.registers[1] = 0x5;

  const std::uint8_t INSTR = encodeSRC(0);
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  EXPECT_EQ(cpu.srcAddress, 0xA5);
}

/** @test DCL sets RAM bank from accumulator. */
TEST(Intel4004CpuTest, Dcl) {
  Intel4004Cpu cpu;
  cpu.accumulator = 3;
  const std::uint8_t INSTR = DCL;
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  EXPECT_EQ(cpu.ramBank, 3);
}

/** @test RAM write/read round-trip. */
TEST(Intel4004CpuTest, RamWriteRead) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_RAM.data(), PROGRAM_RAM.size());

  cpu.step(); // FIM P0, 0x00
  cpu.step(); // SRC P0
  cpu.step(); // LDM 5
  cpu.step(); // WRM -> write 5 to RAM[0]
  cpu.step(); // LDM 0 -> clear ACC
  EXPECT_EQ(cpu.accumulator, 0);

  cpu.step(); // RDM -> read RAM[0] = 5
  EXPECT_EQ(cpu.accumulator, 5);
}

/* ----------------------------- FIN / JIN ----------------------------- */

/** @test FIN reads ROM data into register pair using R0R1 address. */
TEST(Intel4004CpuTest, Fin) {
  Intel4004Cpu cpu;
  // Put data at ROM address 0x010
  cpu.rom[0x10] = 0x5A;

  // Set R0R1 to point to 0x10
  cpu.registers[0] = 0x1;
  cpu.registers[1] = 0x0;

  // FIN P1 (load into R2R3 from ROM[PC_page:R0R1])
  const std::uint8_t INSTR = encodeFIN(1);
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  EXPECT_EQ(cpu.registers[2], 0x5); // R2 = high nibble
  EXPECT_EQ(cpu.registers[3], 0xA); // R3 = low nibble
}

/** @test JIN jumps to address formed from PC page and register pair. */
TEST(Intel4004CpuTest, Jin) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 0x0;
  cpu.registers[1] = 0x8;

  const std::uint8_t INSTR = encodeJIN(0);
  cpu.loadProgram(&INSTR, 1);
  cpu.step();

  EXPECT_EQ(cpu.pc, 0x08); // PC page 0x000 | R0R1 = 0x08
}

/* ----------------------------- Instruction Encoding ----------------------------- */

/** @test isTwoByteInstruction correctly identifies 2-byte instructions. */
TEST(Intel4004InstructionsTest, TwoByteDetection) {
  EXPECT_FALSE(isTwoByteInstruction(NOP));
  EXPECT_TRUE(isTwoByteInstruction(encodeJCN(0)));
  EXPECT_TRUE(isTwoByteInstruction(encodeFIM(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeSRC(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeFIN(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeJIN(0)));
  EXPECT_TRUE(isTwoByteInstruction(encodeJUN(0)));
  EXPECT_TRUE(isTwoByteInstruction(encodeJMS(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeINC(0)));
  EXPECT_TRUE(isTwoByteInstruction(encodeISZ(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeADD(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeSUB(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeLD(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeXCH(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeBBL(0)));
  EXPECT_FALSE(isTwoByteInstruction(encodeLDM(0)));
  EXPECT_FALSE(isTwoByteInstruction(CLB));
  EXPECT_FALSE(isTwoByteInstruction(WRM));
}

/* ----------------------------- Reset ----------------------------- */

/** @test Reset clears all state, preserves ROM. */
TEST(Intel4004CpuTest, Reset) {
  Intel4004Cpu cpu;
  cpu.loadProgram(PROGRAM_ADD.data(), PROGRAM_ADD.size());
  cpu.run(3);

  cpu.reset();
  EXPECT_EQ(cpu.accumulator, 0);
  EXPECT_FALSE(cpu.carry);
  EXPECT_EQ(cpu.pc, 0);
  EXPECT_EQ(cpu.sp, 0);
  EXPECT_EQ(cpu.cyclesExecuted, 0);
  // ROM preserved
  EXPECT_EQ(cpu.rom[0], PROGRAM_ADD[0]);
}

/* ----------------------------- Register Pair Helpers ----------------------------- */

/** @test getRegisterPairValue and setRegisterPair are consistent. */
TEST(Intel4004CpuTest, RegisterPairRoundTrip) {
  Intel4004Cpu cpu;

  for (std::uint8_t p = 0; p < 8; ++p) {
    const std::uint8_t VAL = static_cast<std::uint8_t>(0x10 + p);
    cpu.setRegisterPair(p, VAL);
    EXPECT_EQ(cpu.getRegisterPairValue(p), VAL);
  }
}

/* ----------------------------- Halted Step ----------------------------- */

/** @test step() returns false when CPU is halted. */
TEST(Intel4004CpuTest, StepWhenHalted) {
  Intel4004Cpu cpu;
  cpu.halted = true;
  EXPECT_FALSE(cpu.step());
  EXPECT_EQ(cpu.cyclesExecuted, 0);
}

/* ----------------------------- JCN Carry and Test Pin ----------------------------- */

/** @test JCN jumps on carry condition. */
TEST(Intel4004CpuTest, JcnCarry) {
  Intel4004Cpu cpu;
  cpu.carry = true;
  const std::uint8_t PROG[] = {
      encodeJCN(COND_CARRY), 0x04, // Jump to 0x04 if carry set
      NOP,                         // Skipped
      NOP,                         // Skipped
      encodeLDM(7)                 // Jumped here
  };
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // JCN -> carry is set, jump to 0x04
  EXPECT_EQ(cpu.pc, 4);
}

/** @test JCN jumps on test pin low (active low). */
TEST(Intel4004CpuTest, JcnTestPin) {
  Intel4004Cpu cpu;
  cpu.testPin = false;                                     // TEST pin low -> condition true
  const std::uint8_t PROG[] = {encodeJCN(COND_TEST), 0x04, // Jump if TEST pin == 0
                               NOP, NOP, encodeLDM(3)};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step();
  EXPECT_EQ(cpu.pc, 4);
}

/** @test JCN does not jump when test pin is high. */
TEST(Intel4004CpuTest, JcnTestPinHigh) {
  Intel4004Cpu cpu;
  cpu.testPin = true; // TEST pin high -> condition false
  const std::uint8_t PROG[] = {encodeJCN(COND_TEST), 0x04, encodeLDM(9), NOP, encodeLDM(0)};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step();
  EXPECT_EQ(cpu.pc, 2); // No jump
}

/* ----------------------------- I/O: Status Registers ----------------------------- */

/** @test WR0-WR3 write accumulator to status characters; RD0-RD3 read them back. */
TEST(Intel4004CpuTest, StatusRegisterWriteRead) {
  Intel4004Cpu cpu;
  // Set up SRC address: P0 = 0x00
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  const std::uint8_t PROG[] = {
      encodeSRC(0),   // SRC P0
      encodeLDM(0xA), // ACC = 10
      WR0,            // Status[0] = 10
      encodeLDM(0xB), // ACC = 11
      WR1,            // Status[1] = 11
      encodeLDM(0xC), // ACC = 12
      WR2,            // Status[2] = 12
      encodeLDM(0xD), // ACC = 13
      WR3,            // Status[3] = 13
      encodeLDM(0),   // Clear ACC
      RD0,            // ACC = Status[0] = 10
  };
  cpu.loadProgram(PROG, sizeof(PROG));

  for (int i = 0; i < 10; ++i) {
    cpu.step();
  }
  EXPECT_EQ(cpu.accumulator, 0); // After LDM 0

  cpu.step(); // RD0
  EXPECT_EQ(cpu.accumulator, 0xA);
}

/** @test RD1-RD3 read back status characters. */
TEST(Intel4004CpuTest, StatusRegisterReadAll) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  // Pre-write status via direct member access for simplicity
  cpu.ramStatus[0] = 1;
  cpu.ramStatus[1] = 2;
  cpu.ramStatus[2] = 3;
  cpu.ramStatus[3] = 4;

  const std::uint8_t PROG[] = {
      encodeSRC(0), // SRC P0 -> srcAddress = 0x00
      RD0,
  };
  cpu.loadProgram(PROG, sizeof(PROG));
  cpu.step(); // SRC
  cpu.step(); // RD0
  EXPECT_EQ(cpu.accumulator, 1);

  cpu.reset();
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  cpu.ramStatus[1] = 5;
  const std::uint8_t PROG2[] = {encodeSRC(0), RD1};
  cpu.loadProgram(PROG2, sizeof(PROG2));
  cpu.step();
  cpu.step();
  EXPECT_EQ(cpu.accumulator, 5);

  cpu.reset();
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  cpu.ramStatus[2] = 6;
  const std::uint8_t PROG3[] = {encodeSRC(0), RD2};
  cpu.loadProgram(PROG3, sizeof(PROG3));
  cpu.step();
  cpu.step();
  EXPECT_EQ(cpu.accumulator, 6);

  cpu.reset();
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  cpu.ramStatus[3] = 7;
  const std::uint8_t PROG4[] = {encodeSRC(0), RD3};
  cpu.loadProgram(PROG4, sizeof(PROG4));
  cpu.step();
  cpu.step();
  EXPECT_EQ(cpu.accumulator, 7);
}

/* ----------------------------- I/O: RAM Output Port ----------------------------- */

/** @test WMP writes accumulator to RAM output port. */
TEST(Intel4004CpuTest, Wmp) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  const std::uint8_t PROG[] = {encodeSRC(0), encodeLDM(9), WMP};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // SRC
  cpu.step(); // LDM 9
  cpu.step(); // WMP
  // srcAddress=0x00, ramBank=0 -> output port 0
  EXPECT_EQ(cpu.ramOutput[0], 9);
}

/* ----------------------------- I/O: SBM / ADM ----------------------------- */

/** @test SBM subtracts RAM data from accumulator. */
TEST(Intel4004CpuTest, Sbm) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  cpu.ramData[0] = 3;
  cpu.accumulator = 9;
  cpu.carry = true; // No borrow

  const std::uint8_t PROG[] = {encodeSRC(0), SBM};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // SRC
  cpu.step(); // SBM: 9 + ~3 + 1 = 9 + 12 + 1 = 22 -> ACC=6, carry=1
  EXPECT_EQ(cpu.accumulator, 6);
  EXPECT_TRUE(cpu.carry);
}

/** @test ADM adds RAM data to accumulator. */
TEST(Intel4004CpuTest, Adm) {
  Intel4004Cpu cpu;
  cpu.registers[0] = 0;
  cpu.registers[1] = 0;
  cpu.ramData[0] = 5;
  cpu.accumulator = 3;
  cpu.carry = false;

  const std::uint8_t PROG[] = {encodeSRC(0), ADM};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // SRC
  cpu.step(); // ADM: 3 + 5 + 0 = 8
  EXPECT_EQ(cpu.accumulator, 8);
  EXPECT_FALSE(cpu.carry);
}

/* ----------------------------- I/O: ROM Port ----------------------------- */

/** @test WRR is a no-op; RDR returns 0. */
TEST(Intel4004CpuTest, RomPortNoOp) {
  Intel4004Cpu cpu;
  cpu.accumulator = 7;
  const std::uint8_t PROG[] = {WRR, RDR};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step(); // WRR (no-op)
  EXPECT_EQ(cpu.accumulator, 7);

  cpu.step(); // RDR -> ACC = 0
  EXPECT_EQ(cpu.accumulator, 0);
}

/** @test WPM is a no-op. */
TEST(Intel4004CpuTest, WpmNoOp) {
  Intel4004Cpu cpu;
  cpu.accumulator = 5;
  const std::uint8_t PROG[] = {WPM};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.step();
  EXPECT_EQ(cpu.accumulator, 5); // Unchanged
}

/* ----------------------------- Run ----------------------------- */

/** @test run() stops at maxCycles. */
TEST(Intel4004CpuTest, RunMaxCycles) {
  Intel4004Cpu cpu;
  // Load infinite NOP loop: JUN 0x000 at address 0
  const std::uint8_t PROG[] = {encodeJUN(0x000), 0x00};
  cpu.loadProgram(PROG, sizeof(PROG));

  cpu.run(50);
  EXPECT_EQ(cpu.cyclesExecuted, 50);
  EXPECT_FALSE(cpu.halted);
}

/* ----------------------------- Decode Helpers ----------------------------- */

/** @test decodeRegister extracts lower nibble. */
TEST(Intel4004InstructionsTest, DecodeRegister) {
  EXPECT_EQ(decodeRegister(0xA5), 5);
  EXPECT_EQ(decodeRegister(0xFF), 0xF);
  EXPECT_EQ(decodeRegister(0x80), 0);
}

/** @test decodeRegisterPair extracts bits 3:1. */
TEST(Intel4004InstructionsTest, DecodeRegisterPair) {
  EXPECT_EQ(decodeRegisterPair(0x20), 0); // P0
  EXPECT_EQ(decodeRegisterPair(0x2E), 7); // P7
}

/** @test decodeData4 extracts lower nibble. */
TEST(Intel4004InstructionsTest, DecodeData4) {
  EXPECT_EQ(decodeData4(0xD5), 5);
  EXPECT_EQ(decodeData4(0xDF), 0xF);
  EXPECT_EQ(decodeData4(0xD0), 0);
}
