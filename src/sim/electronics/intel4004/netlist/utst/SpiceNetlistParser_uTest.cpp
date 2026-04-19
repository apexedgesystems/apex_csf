/**
 * @file SpiceNetlistParser_uTest.cpp
 * @brief Unit tests for the Intel 4004 SPICE netlist parser.
 */

#include "src/sim/electronics/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>
#include <string>

using sim::electronics::intel4004::Intel4004Netlist;
using sim::electronics::intel4004::loadSpiceNetlist;
using sim::electronics::intel4004::parseSpiceNetlist;

/* ----------------------------- API Tests ----------------------------- */

/** @test Parse a minimal 2-transistor netlist. */
TEST(SpiceNetlistParser, ParseMinimal) {
  const std::string INPUT = "* Comment\n\nM0 A B C GND efet\nM1 D E F GND efet\n";
  const auto RESULT = parseSpiceNetlist(INPUT);

  EXPECT_EQ(RESULT.transistorCount(), 2);
  EXPECT_EQ(RESULT.transistors[0].drain, "A");
  EXPECT_EQ(RESULT.transistors[0].gate, "B");
  EXPECT_EQ(RESULT.transistors[0].source, "C");
  EXPECT_EQ(RESULT.transistors[1].drain, "D");
  EXPECT_EQ(RESULT.transistors[1].gate, "E");
  EXPECT_EQ(RESULT.transistors[1].source, "F");
}

/** @test Unique nets are correctly collected and sorted. */
TEST(SpiceNetlistParser, UniqueNets) {
  const std::string INPUT = "M0 A B C GND efet\nM1 A D C GND efet\n";
  const auto RESULT = parseSpiceNetlist(INPUT);

  EXPECT_EQ(RESULT.netCount(), 4); // A, B, C, D
  EXPECT_TRUE(RESULT.hasNet("A"));
  EXPECT_TRUE(RESULT.hasNet("B"));
  EXPECT_TRUE(RESULT.hasNet("C"));
  EXPECT_TRUE(RESULT.hasNet("D"));
  EXPECT_FALSE(RESULT.hasNet("E"));
}

/** @test Comment lines (starting with *) are skipped. */
TEST(SpiceNetlistParser, SkipsComments) {
  const std::string INPUT = "* Header comment\n* Another comment\nM0 X Y Z GND efet\n";
  const auto RESULT = parseSpiceNetlist(INPUT);
  EXPECT_EQ(RESULT.transistorCount(), 1);
}

/** @test Empty lines are skipped. */
TEST(SpiceNetlistParser, SkipsEmptyLines) {
  const std::string INPUT = "\n\n\nM0 X Y Z GND efet\n\n";
  const auto RESULT = parseSpiceNetlist(INPUT);
  EXPECT_EQ(RESULT.transistorCount(), 1);
}

/** @test Empty input produces empty netlist. */
TEST(SpiceNetlistParser, EmptyInput) {
  const auto RESULT = parseSpiceNetlist("");
  EXPECT_EQ(RESULT.transistorCount(), 0);
  EXPECT_EQ(RESULT.netCount(), 0);
}

/** @test Signal names with special characters are preserved. */
TEST(SpiceNetlistParser, SpecialCharacterNets) {
  const std::string INPUT = "M0 VDD SC(A22+M22)CLK2 N0866 GND efet\n"
                            "M1 VDD (~INH)(X11+X31)CLK1 N0770 GND efet\n";
  const auto RESULT = parseSpiceNetlist(INPUT);

  EXPECT_EQ(RESULT.transistorCount(), 2);
  EXPECT_TRUE(RESULT.hasNet("SC(A22+M22)CLK2"));
  EXPECT_TRUE(RESULT.hasNet("(~INH)(X11+X31)CLK1"));
  EXPECT_TRUE(RESULT.hasNet("VDD"));
}

/** @test GND appears as drain/source net in real netlist format. */
TEST(SpiceNetlistParser, GndAsSignalNet) {
  const std::string INPUT = "M0 N0385 N0770 GND GND efet\n"
                            "M1 GND PC0.11 N0785 GND efet\n";
  const auto RESULT = parseSpiceNetlist(INPUT);

  EXPECT_EQ(RESULT.transistorCount(), 2);
  EXPECT_TRUE(RESULT.hasNet("GND"));
  EXPECT_EQ(RESULT.transistors[0].source, "GND");
  EXPECT_EQ(RESULT.transistors[1].drain, "GND");
}

/** @test Dotted signal names (register bits) are handled. */
TEST(SpiceNetlistParser, DottedNetNames) {
  const std::string INPUT = "M0 ACC.0 CLK1 R0.0 GND efet\n"
                            "M1 PC0.11 SYNC D3_PAD GND efet\n";
  const auto RESULT = parseSpiceNetlist(INPUT);

  EXPECT_TRUE(RESULT.hasNet("ACC.0"));
  EXPECT_TRUE(RESULT.hasNet("R0.0"));
  EXPECT_TRUE(RESULT.hasNet("PC0.11"));
  EXPECT_TRUE(RESULT.hasNet("D3_PAD"));
}

/** @test Malformed M-lines with missing fields are skipped. */
TEST(SpiceNetlistParser, MalformedLinesSkipped) {
  const std::string INPUT = "M0 A\n"
                            "M1 A B\n"
                            "M2 A B C GND efet\n";
  const auto RESULT = parseSpiceNetlist(INPUT);
  EXPECT_EQ(RESULT.transistorCount(), 1);
  EXPECT_EQ(RESULT.transistors[0].drain, "A");
}

/** @test Non-M lines (e.g. subcircuit, .model) are skipped. */
TEST(SpiceNetlistParser, NonMlinesSkipped) {
  const std::string INPUT = ".model efet NMOS\n"
                            ".subckt inverter\n"
                            "M0 A B C GND efet\n"
                            ".ends\n";
  const auto RESULT = parseSpiceNetlist(INPUT);
  EXPECT_EQ(RESULT.transistorCount(), 1);
}

/* ----------------------------- File Load Tests ----------------------------- */

#ifdef INTEL4004_DATA_DIR

/** @test Load the full lajos-4004.spice netlist from file. */
TEST(SpiceNetlistParser, LoadFullNetlist) {
  const std::string PATH = std::string(INTEL4004_DATA_DIR) + "/lajos-4004.spice";
  const auto RESULT = loadSpiceNetlist(PATH);

  // Expected counts from netlist analysis
  EXPECT_EQ(RESULT.transistorCount(), 2242);
  EXPECT_GT(RESULT.netCount(), 1000);
  EXPECT_LT(RESULT.netCount(), 1200);

  // Key signal nets
  EXPECT_TRUE(RESULT.hasNet("VDD"));
  EXPECT_TRUE(RESULT.hasNet("GND"));
  EXPECT_TRUE(RESULT.hasNet("CLK1"));
  EXPECT_TRUE(RESULT.hasNet("CLK2"));
  EXPECT_TRUE(RESULT.hasNet("SYNC"));

  // Accumulator bits
  EXPECT_TRUE(RESULT.hasNet("ACC.0"));
  EXPECT_TRUE(RESULT.hasNet("ACC.1"));
  EXPECT_TRUE(RESULT.hasNet("ACC.2"));
  EXPECT_TRUE(RESULT.hasNet("ACC.3"));

  // Data bus
  EXPECT_TRUE(RESULT.hasNet("D0"));
  EXPECT_TRUE(RESULT.hasNet("D1"));
  EXPECT_TRUE(RESULT.hasNet("D2"));
  EXPECT_TRUE(RESULT.hasNet("D3"));

  // Register bits
  EXPECT_TRUE(RESULT.hasNet("R0.0"));
  EXPECT_TRUE(RESULT.hasNet("R15.3"));

  EXPECT_EQ(RESULT.transistors[0].drain, "N0385");
  EXPECT_EQ(RESULT.transistors[0].gate, "N0770");
  EXPECT_EQ(RESULT.transistors[0].source, "GND");
}

/** @test Invalid file path throws. */
TEST(SpiceNetlistParser, InvalidFileThrows) {
  EXPECT_THROW(loadSpiceNetlist("/nonexistent/path.spice"), std::runtime_error);
}

#endif // INTEL4004_DATA_DIR
