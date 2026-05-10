/**
 * @file Intel4004Disassembler_uTest.cpp
 * @brief Unit tests for Intel4004Disassembler (byte stream -> mnemonic).
 *
 * Covers all 16 instruction groups (1-byte and 2-byte forms), the F-group
 * accumulator-op mnemonic table, the E-group I/O & RAM mnemonic table,
 * and the 1-byte-remaining edge cases for two-byte ops.
 */

#include "src/sim/electronics/chips/intel4004/behavioral/inc/Intel4004Disassembler.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using sim::electronics::chips::intel4004::accGroupMnemonic;
using sim::electronics::chips::intel4004::DisassembledInstruction;
using sim::electronics::chips::intel4004::disassemble;
using sim::electronics::chips::intel4004::ioRamMnemonic;

namespace {

DisassembledInstruction disasm(std::initializer_list<std::uint8_t> bytes) {
  static thread_local std::array<std::uint8_t, 16> buf{};
  std::size_t i = 0;
  for (auto b : bytes) {
    buf[i++] = b;
  }
  return disassemble(buf.data(), bytes.size());
}

}

/* ----------------------------- Edge cases ----------------------------- */

/** @test Empty input returns the documented marker and zero length */
TEST(Intel4004DisassemblerTest, EmptyInputReturnsMarker) {
  std::array<std::uint8_t, 1> buf{0x00};
  const auto R = disassemble(buf.data(), 0);
  EXPECT_EQ(R.mnemonic, "<empty>");
  EXPECT_EQ(R.length, 0u);
}

/* ----------------------------- 1-byte instructions ----------------------------- */

/** @test NOP (0x00) decodes correctly */
TEST(Intel4004DisassemblerTest, NopDecode) {
  const auto R = disasm({0x00});
  EXPECT_EQ(R.mnemonic, "NOP");
  EXPECT_EQ(R.length, 1u);
}

/** @test SRC P0..P7 odd nibbles in the 0x2X group */
TEST(Intel4004DisassemblerTest, SrcDecode) {
  for (std::uint8_t pair = 0; pair < 8; ++pair) {
    const std::uint8_t OP = static_cast<std::uint8_t>(0x20 | (pair << 1) | 1);
    const auto R = disasm({OP});
    EXPECT_EQ(R.mnemonic, "SRC P" + std::to_string(pair));
    EXPECT_EQ(R.length, 1u);
  }
}

/** @test FIN / JIN in the 0x3X group (even = FIN, odd = JIN) */
TEST(Intel4004DisassemblerTest, FinJinDecode) {
  for (std::uint8_t pair = 0; pair < 8; ++pair) {
    const std::uint8_t FIN_OP = static_cast<std::uint8_t>(0x30 | (pair << 1));
    const std::uint8_t JIN_OP = static_cast<std::uint8_t>(0x30 | (pair << 1) | 1);
    EXPECT_EQ(disasm({FIN_OP}).mnemonic, "FIN P" + std::to_string(pair));
    EXPECT_EQ(disasm({JIN_OP}).mnemonic, "JIN P" + std::to_string(pair));
  }
}

/** @test INC R0..R15 (0x60..0x6F) */
TEST(Intel4004DisassemblerTest, IncDecode) {
  for (std::uint8_t reg = 0; reg < 16; ++reg) {
    const auto R = disasm({static_cast<std::uint8_t>(0x60 | reg)});
    EXPECT_EQ(R.mnemonic, "INC R" + std::to_string(reg));
    EXPECT_EQ(R.length, 1u);
  }
}

/** @test Register ALU ops -- ADD, SUB, LD, XCH register operand */
TEST(Intel4004DisassemblerTest, RegisterAluDecodes) {
  EXPECT_EQ(disasm({0x84}).mnemonic, "ADD R4");
  EXPECT_EQ(disasm({0x9F}).mnemonic, "SUB R15");
  EXPECT_EQ(disasm({0xA0}).mnemonic, "LD R0");
  EXPECT_EQ(disasm({0xB7}).mnemonic, "XCH R7");
}

/** @test BBL constant decodes 0..15 */
TEST(Intel4004DisassemblerTest, BblDecode) {
  for (std::uint8_t imm = 0; imm < 16; ++imm) {
    EXPECT_EQ(disasm({static_cast<std::uint8_t>(0xC0 | imm)}).mnemonic,
              "BBL " + std::to_string(imm));
  }
}

/** @test LDM constant decodes 0..15 */
TEST(Intel4004DisassemblerTest, LdmDecode) {
  for (std::uint8_t imm = 0; imm < 16; ++imm) {
    EXPECT_EQ(disasm({static_cast<std::uint8_t>(0xD0 | imm)}).mnemonic,
              "LDM " + std::to_string(imm));
  }
}

/* ----------------------------- 2-byte instructions ----------------------------- */

/** @test JCN with each condition code + immediate address */
TEST(Intel4004DisassemblerTest, JcnDecode) {
  for (std::uint8_t cond = 0; cond < 16; ++cond) {
    const auto R = disasm({static_cast<std::uint8_t>(0x10 | cond), 0x42});
    char buf[16];
    std::snprintf(buf, sizeof(buf), "JCN C%X, 0x42", cond);
    EXPECT_EQ(R.mnemonic, buf);
    EXPECT_EQ(R.length, 2u);
  }
}

/** @test FIM Pn, immediate -- pair 0..7 */
TEST(Intel4004DisassemblerTest, FimDecode) {
  for (std::uint8_t pair = 0; pair < 8; ++pair) {
    const std::uint8_t OP = static_cast<std::uint8_t>(0x20 | (pair << 1));
    const auto R = disasm({OP, 0xAB});
    EXPECT_EQ(R.mnemonic, "FIM P" + std::to_string(pair) + ", 0xAB");
    EXPECT_EQ(R.length, 2u);
  }
}

/** @test JUN -- 12-bit address spans both bytes */
TEST(Intel4004DisassemblerTest, JunDecode) {
  // 0x4_HI 0xLO -> 12-bit addr (HI<<8) | LO
  const auto R = disasm({0x42, 0x34});
  EXPECT_EQ(R.mnemonic, "JUN 0x234");
  EXPECT_EQ(R.length, 2u);
}

/** @test JMS -- 12-bit address */
TEST(Intel4004DisassemblerTest, JmsDecode) {
  const auto R = disasm({0x5A, 0xBC});
  EXPECT_EQ(R.mnemonic, "JMS 0xABC");
  EXPECT_EQ(R.length, 2u);
}

/** @test ISZ Rn, addr decodes the register + 8-bit branch target */
TEST(Intel4004DisassemblerTest, IszDecode) {
  const auto R = disasm({0x73, 0x10});
  EXPECT_EQ(R.mnemonic, "ISZ R3, 0x10");
  EXPECT_EQ(R.length, 2u);
}

/* ----------------------------- 1-byte-remaining edge cases ----------------------------- */

/** @test Two-byte ops with only 1 byte remaining return a fallback marker */
TEST(Intel4004DisassemblerTest, TwoByteWithOneByteRemainingFallsBackToMarker) {
  std::array<std::uint8_t, 1> buf;

  buf = {0x14}; // JCN
  EXPECT_EQ(disassemble(buf.data(), 1).mnemonic, "JCN C4, ??");
  EXPECT_EQ(disassemble(buf.data(), 1).length, 1u);

  buf = {0x20}; // FIM (even)
  EXPECT_EQ(disassemble(buf.data(), 1).mnemonic, "FIM P0, ??");

  buf = {0x42}; // JUN
  EXPECT_EQ(disassemble(buf.data(), 1).mnemonic, "JUN ??");

  buf = {0x53}; // JMS
  EXPECT_EQ(disassemble(buf.data(), 1).mnemonic, "JMS ??");

  buf = {0x73}; // ISZ
  EXPECT_EQ(disassemble(buf.data(), 1).mnemonic, "ISZ R3, ??");
}

/* ----------------------------- E-group (IO/RAM) ----------------------------- */

/** @test E-group decodes via ioRamMnemonic for all 16 OPAs */
TEST(Intel4004DisassemblerTest, IoRamGroupDecode) {
  const std::array<const char*, 16> EXPECTED = {
      "WRM", "WMP", "WRR", "WPM", "WR0", "WR1", "WR2", "WR3",
      "SBM", "RDM", "RDR", "ADM", "RD0", "RD1", "RD2", "RD3"};
  for (std::uint8_t opa = 0; opa < 16; ++opa) {
    const auto R = disasm({static_cast<std::uint8_t>(0xE0 | opa)});
    EXPECT_EQ(R.mnemonic, EXPECTED[opa]);
    EXPECT_EQ(R.length, 1u);
  }
}

/** @test ioRamMnemonic returns the documented stub for out-of-range opa */
TEST(Intel4004DisassemblerTest, IoRamMnemonicHandlesOutOfRange) {
  EXPECT_EQ(ioRamMnemonic(0x10), std::string_view("E?"));
  EXPECT_EQ(ioRamMnemonic(0xFF), std::string_view("E?"));
}

/* ----------------------------- F-group (ACC ops) ----------------------------- */

/** @test F-group decodes via accGroupMnemonic for all defined OPAs */
TEST(Intel4004DisassemblerTest, AccGroupDecode) {
  const std::array<const char*, 14> EXPECTED = {
      "CLB", "CLC", "IAC", "CMC", "CMA", "RAL", "RAR",
      "TCC", "DAC", "TCS", "STC", "DAA", "KBP", "DCL"};
  for (std::uint8_t opa = 0; opa < EXPECTED.size(); ++opa) {
    const auto R = disasm({static_cast<std::uint8_t>(0xF0 | opa)});
    EXPECT_EQ(R.mnemonic, EXPECTED[opa]);
    EXPECT_EQ(R.length, 1u);
  }
}

/** @test F-group OPAs 0xE..0xF fall through to the documented stub */
TEST(Intel4004DisassemblerTest, AccGroupHandlesUndefinedOpa) {
  EXPECT_EQ(accGroupMnemonic(0xE), std::string_view("F?"));
  EXPECT_EQ(accGroupMnemonic(0xF), std::string_view("F?"));
}

/* ----------------------------- Determinism ----------------------------- */

/** @test Repeated calls on the same byte sequence return identical output */
TEST(Intel4004DisassemblerDeterminismTest, RepeatedCallsAreIdentical) {
  std::array<std::uint8_t, 2> buf{0x42, 0x34};
  const auto FIRST = disassemble(buf.data(), 2);
  for (int i = 0; i < 50; ++i) {
    const auto SAMPLE = disassemble(buf.data(), 2);
    EXPECT_EQ(SAMPLE.mnemonic, FIRST.mnemonic);
    EXPECT_EQ(SAMPLE.length, FIRST.length);
  }
}
