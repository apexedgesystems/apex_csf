/**
 * @file LogicGates_uTest.cpp
 * @brief Unit tests for digital logic gate truth tables and adders.
 *
 * Validates all combinations of inputs against expected truth tables.
 */

#include "src/sim/electronics/gates/inc/LogicGates.hpp"

#include <gtest/gtest.h>

using sim::electronics::gates::fullAdder;
using sim::electronics::gates::gateAnd;
using sim::electronics::gates::gateNand;
using sim::electronics::gates::gateNor;
using sim::electronics::gates::gateNot;
using sim::electronics::gates::gateOr;
using sim::electronics::gates::gateXnor;
using sim::electronics::gates::gateXor;
using sim::electronics::gates::halfAdder;

/* ----------------------------- Single-Input ----------------------------- */

/** @test NOT gate truth table. */
TEST(LogicGates, NotTruthTable) {
  EXPECT_EQ(gateNot(0), 1);
  EXPECT_EQ(gateNot(1), 0);
}

/* ----------------------------- Two-Input Gates ----------------------------- */

/** @test AND gate truth table. */
TEST(LogicGates, AndTruthTable) {
  EXPECT_EQ(gateAnd(0, 0), 0);
  EXPECT_EQ(gateAnd(0, 1), 0);
  EXPECT_EQ(gateAnd(1, 0), 0);
  EXPECT_EQ(gateAnd(1, 1), 1);
}

/** @test OR gate truth table. */
TEST(LogicGates, OrTruthTable) {
  EXPECT_EQ(gateOr(0, 0), 0);
  EXPECT_EQ(gateOr(0, 1), 1);
  EXPECT_EQ(gateOr(1, 0), 1);
  EXPECT_EQ(gateOr(1, 1), 1);
}

/** @test NAND gate truth table. */
TEST(LogicGates, NandTruthTable) {
  EXPECT_EQ(gateNand(0, 0), 1);
  EXPECT_EQ(gateNand(0, 1), 1);
  EXPECT_EQ(gateNand(1, 0), 1);
  EXPECT_EQ(gateNand(1, 1), 0);
}

/** @test NOR gate truth table. */
TEST(LogicGates, NorTruthTable) {
  EXPECT_EQ(gateNor(0, 0), 1);
  EXPECT_EQ(gateNor(0, 1), 0);
  EXPECT_EQ(gateNor(1, 0), 0);
  EXPECT_EQ(gateNor(1, 1), 0);
}

/** @test XOR gate truth table. */
TEST(LogicGates, XorTruthTable) {
  EXPECT_EQ(gateXor(0, 0), 0);
  EXPECT_EQ(gateXor(0, 1), 1);
  EXPECT_EQ(gateXor(1, 0), 1);
  EXPECT_EQ(gateXor(1, 1), 0);
}

/** @test XNOR gate truth table. */
TEST(LogicGates, XnorTruthTable) {
  EXPECT_EQ(gateXnor(0, 0), 1);
  EXPECT_EQ(gateXnor(0, 1), 0);
  EXPECT_EQ(gateXnor(1, 0), 0);
  EXPECT_EQ(gateXnor(1, 1), 1);
}

/* ----------------------------- Adders ----------------------------- */

/** @test Half adder truth table (4 input combinations). */
TEST(LogicGates, HalfAdder) {
  // 0+0 = 0, carry 0
  auto r1 = halfAdder(0, 0);
  EXPECT_EQ(r1.sum, 0);
  EXPECT_EQ(r1.carry, 0);

  // 0+1 = 1, carry 0
  auto r2 = halfAdder(0, 1);
  EXPECT_EQ(r2.sum, 1);
  EXPECT_EQ(r2.carry, 0);

  // 1+0 = 1, carry 0
  auto r3 = halfAdder(1, 0);
  EXPECT_EQ(r3.sum, 1);
  EXPECT_EQ(r3.carry, 0);

  // 1+1 = 0, carry 1 (binary 10 = 2)
  auto r4 = halfAdder(1, 1);
  EXPECT_EQ(r4.sum, 0);
  EXPECT_EQ(r4.carry, 1);
}

/** @test Full adder truth table (8 input combinations). */
TEST(LogicGates, FullAdder) {
  // Test all 8 combinations of (a, b, cin)
  // Expected: a + b + cin = 2*cout + sum
  for (int a = 0; a < 2; ++a) {
    for (int b = 0; b < 2; ++b) {
      for (int cin = 0; cin < 2; ++cin) {
        auto r = fullAdder(a, b, cin);
        int expected = a + b + cin;
        int actual = 2 * r.cout + r.sum;
        EXPECT_EQ(actual, expected) << "FA(" << a << "," << b << "," << cin << ") = "
                                    << "sum=" << r.sum << " cout=" << r.cout
                                    << " (expected total=" << expected << ", got=" << actual << ")";
      }
    }
  }
}
