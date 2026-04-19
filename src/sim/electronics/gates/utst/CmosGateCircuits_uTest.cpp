/**
 * @file CmosGateCircuits_uTest.cpp
 * @brief Unit tests for circuit-level CMOS gate models.
 *
 * Validates all 7 gate types (NOT, NAND, NOR, AND, OR, XOR, XNOR) produce
 * correct output voltages through MosfetLevel1 physics and MNA solving,
 * matching expected CMOS logic behavior from LogicGates.hpp truth tables.
 */

#include "src/sim/electronics/gates/inc/CmosGateCircuits.hpp"
#include "src/sim/electronics/gates/inc/LogicGates.hpp"

#include <gtest/gtest.h>

using sim::electronics::devices::nonlinear::MosfetLevel1Params;
using sim::electronics::gates::CmosAndCircuit;
using sim::electronics::gates::CmosInverterCircuit;
using sim::electronics::gates::CmosNandCircuit;
using sim::electronics::gates::CmosNorCircuit;
using sim::electronics::gates::CmosOrCircuit;
using sim::electronics::gates::CmosXnorCircuit;
using sim::electronics::gates::CmosXorCircuit;

/* ----------------------------- Constants ----------------------------- */

/// Supply voltage for all tests.
static constexpr double VDD = 5.0;
/// Channel width (meters).
static constexpr double W = 10e-6;
/// Channel length (meters).
static constexpr double L = 1e-6;
/// Voltage threshold for logic high (output should be above this).
static constexpr double V_HIGH_MIN = 3.8;
/// Voltage threshold for logic low (output should be below this).
static constexpr double V_LOW_MAX = 1.1;

/// NMOS parameters: typical enhancement-mode NMOS.
static const MosfetLevel1Params NMOS_PARAMS{.Kp = 120e-6, .Vth = 0.7, .lambda = 0.02};
/// PMOS parameters: typical enhancement-mode PMOS (note positive Vth for the
/// reversed-polarity model used in the circuit wrappers).
static const MosfetLevel1Params PMOS_PARAMS{.Kp = 60e-6, .Vth = 0.7, .lambda = 0.02};

/* ----------------------------- CmosInverterCircuit Tests ----------------------------- */

/** @test Inverter with input low produces output near VDD. */
TEST(CmosInverterCircuit, InputLowOutputHigh) {
  CmosInverterCircuit inv(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  inv.build();
  inv.setInput(0.0);
  double vout = inv.computeDC();
  EXPECT_GT(vout, V_HIGH_MIN) << "Inverter: 0V input should produce output near VDD, got " << vout;
}

/** @test Inverter with input high produces output near ground. */
TEST(CmosInverterCircuit, InputHighOutputLow) {
  CmosInverterCircuit inv(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  inv.build();
  inv.setInput(VDD);
  double vout = inv.computeDC();
  EXPECT_LT(vout, V_LOW_MAX) << "Inverter: VDD input should produce output near 0V, got " << vout;
}

/** @test Inverter output is monotonically decreasing with input. */
TEST(CmosInverterCircuit, TransferCurveMonotonic) {
  CmosInverterCircuit inv(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  inv.build();

  double prevVout = VDD + 1.0; // Start above VDD
  constexpr int STEPS = 5;
  for (int i = 0; i <= STEPS; ++i) {
    double vin = VDD * static_cast<double>(i) / STEPS;
    inv.setInput(vin);
    double vout = inv.computeDC();
    EXPECT_LE(vout, prevVout) << "Transfer curve should be monotonically decreasing at Vin=" << vin;
    prevVout = vout;
  }
}

/* ----------------------------- CmosNandCircuit Tests ----------------------------- */

/** @test NAND(0,0) produces output near VDD. */
TEST(CmosNandCircuit, BothLowOutputHigh) {
  CmosNandCircuit nand(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nand.build();
  nand.setInputs(0.0, 0.0);
  double vout = nand.computeDC();
  EXPECT_GT(vout, V_HIGH_MIN) << "NAND(0,0) should produce output near VDD, got " << vout;
}

/** @test NAND(0,VDD) produces output near VDD. */
TEST(CmosNandCircuit, ALowBHighOutputHigh) {
  CmosNandCircuit nand(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nand.build();
  nand.setInputs(0.0, VDD);
  double vout = nand.computeDC();
  EXPECT_GT(vout, V_HIGH_MIN) << "NAND(0,VDD) should produce output near VDD, got " << vout;
}

/** @test NAND(VDD,0) produces output near VDD. */
TEST(CmosNandCircuit, AHighBLowOutputHigh) {
  CmosNandCircuit nand(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nand.build();
  nand.setInputs(VDD, 0.0);
  double vout = nand.computeDC();
  EXPECT_GT(vout, V_HIGH_MIN) << "NAND(VDD,0) should produce output near VDD, got " << vout;
}

/** @test NAND(VDD,VDD) produces output near ground. */
TEST(CmosNandCircuit, BothHighOutputLow) {
  CmosNandCircuit nand(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nand.build();
  nand.setInputs(VDD, VDD);
  double vout = nand.computeDC();
  EXPECT_LT(vout, V_LOW_MAX) << "NAND(VDD,VDD) should produce output near 0V, got " << vout;
}

/* ----------------------------- CmosNorCircuit Tests ----------------------------- */

/** @test NOR(0,0) produces output near VDD. */
TEST(CmosNorCircuit, BothLowOutputHigh) {
  CmosNorCircuit nor(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nor.build();
  nor.setInputs(0.0, 0.0);
  double vout = nor.computeDC();
  EXPECT_GT(vout, V_HIGH_MIN) << "NOR(0,0) should produce output near VDD, got " << vout;
}

/** @test NOR(0,VDD) produces output near ground. */
TEST(CmosNorCircuit, ALowBHighOutputLow) {
  CmosNorCircuit nor(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nor.build();
  nor.setInputs(0.0, VDD);
  double vout = nor.computeDC();
  EXPECT_LT(vout, V_LOW_MAX) << "NOR(0,VDD) should produce output near 0V, got " << vout;
}

/** @test NOR(VDD,0) produces output near ground. */
TEST(CmosNorCircuit, AHighBLowOutputLow) {
  CmosNorCircuit nor(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nor.build();
  nor.setInputs(VDD, 0.0);
  double vout = nor.computeDC();
  EXPECT_LT(vout, V_LOW_MAX) << "NOR(VDD,0) should produce output near 0V, got " << vout;
}

/** @test NOR(VDD,VDD) produces output near ground. */
TEST(CmosNorCircuit, BothHighOutputLow) {
  CmosNorCircuit nor(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  nor.build();
  nor.setInputs(VDD, VDD);
  double vout = nor.computeDC();
  EXPECT_LT(vout, V_LOW_MAX) << "NOR(VDD,VDD) should produce output near 0V, got " << vout;
}

/* ----------------------------- CmosAndCircuit Tests ----------------------------- */

/** @test AND truth table: all 4 input combinations. */
TEST(CmosAndCircuit, TruthTable) {
  CmosAndCircuit gate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  gate.build();

  double inputs[][2] = {{0.0, 0.0}, {0.0, VDD}, {VDD, 0.0}, {VDD, VDD}};
  int expected[] = {0, 0, 0, 1}; // AND truth table

  for (int i = 0; i < 4; ++i) {
    gate.setInputs(inputs[i][0], inputs[i][1]);
    double vout = gate.computeDC();
    if (expected[i] == 1) {
      EXPECT_GT(vout, V_HIGH_MIN) << "AND(" << inputs[i][0] << "," << inputs[i][1]
                                  << ") should be HIGH, got " << vout;
    } else {
      EXPECT_LT(vout, V_LOW_MAX) << "AND(" << inputs[i][0] << "," << inputs[i][1]
                                 << ") should be LOW, got " << vout;
    }
  }
}

/* ----------------------------- CmosOrCircuit Tests ----------------------------- */

/** @test OR truth table: all 4 input combinations. */
TEST(CmosOrCircuit, TruthTable) {
  CmosOrCircuit gate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  gate.build();

  double inputs[][2] = {{0.0, 0.0}, {0.0, VDD}, {VDD, 0.0}, {VDD, VDD}};
  int expected[] = {0, 1, 1, 1}; // OR truth table

  for (int i = 0; i < 4; ++i) {
    gate.setInputs(inputs[i][0], inputs[i][1]);
    double vout = gate.computeDC();
    if (expected[i] == 1) {
      EXPECT_GT(vout, V_HIGH_MIN) << "OR(" << inputs[i][0] << "," << inputs[i][1]
                                  << ") should be HIGH, got " << vout;
    } else {
      EXPECT_LT(vout, V_LOW_MAX) << "OR(" << inputs[i][0] << "," << inputs[i][1]
                                 << ") should be LOW, got " << vout;
    }
  }
}

/* ----------------------------- CmosXorCircuit Tests ----------------------------- */

/** @test XOR truth table: all 4 input combinations. */
TEST(CmosXorCircuit, TruthTable) {
  CmosXorCircuit gate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  gate.build();

  double inputs[][2] = {{0.0, 0.0}, {0.0, VDD}, {VDD, 0.0}, {VDD, VDD}};
  int expected[] = {0, 1, 1, 0}; // XOR truth table

  for (int i = 0; i < 4; ++i) {
    gate.setInputs(inputs[i][0], inputs[i][1]);
    double vout = gate.computeDC();
    if (expected[i] == 1) {
      EXPECT_GT(vout, V_HIGH_MIN) << "XOR(" << inputs[i][0] << "," << inputs[i][1]
                                  << ") should be HIGH, got " << vout;
    } else {
      EXPECT_LT(vout, V_LOW_MAX) << "XOR(" << inputs[i][0] << "," << inputs[i][1]
                                 << ") should be LOW, got " << vout;
    }
  }
}

/* ----------------------------- CmosXnorCircuit Tests ----------------------------- */

/** @test XNOR truth table: all 4 input combinations. */
TEST(CmosXnorCircuit, TruthTable) {
  CmosXnorCircuit gate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  gate.build();

  double inputs[][2] = {{0.0, 0.0}, {0.0, VDD}, {VDD, 0.0}, {VDD, VDD}};
  int expected[] = {1, 0, 0, 1}; // XNOR truth table

  for (int i = 0; i < 4; ++i) {
    gate.setInputs(inputs[i][0], inputs[i][1]);
    double vout = gate.computeDC();
    if (expected[i] == 1) {
      EXPECT_GT(vout, V_HIGH_MIN) << "XNOR(" << inputs[i][0] << "," << inputs[i][1]
                                  << ") should be HIGH, got " << vout;
    } else {
      EXPECT_LT(vout, V_LOW_MAX) << "XNOR(" << inputs[i][0] << "," << inputs[i][1]
                                 << ") should be LOW, got " << vout;
    }
  }
}

/* ----------------------------- Cross-Validation Tests ----------------------------- */

/** @test All gate circuit outputs agree with LogicGates.hpp truth tables. */
TEST(CmosGateCircuits, MatchLogicGatesTruthTables) {
  using namespace sim::electronics::gates;

  CmosInverterCircuit notGate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  CmosNandCircuit nandGate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  CmosNorCircuit norGate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  CmosAndCircuit andGate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  CmosOrCircuit orGate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  CmosXorCircuit xorGate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);
  CmosXnorCircuit xnorGate(VDD, W, L, NMOS_PARAMS, PMOS_PARAMS);

  notGate.build();
  nandGate.build();
  norGate.build();
  andGate.build();
  orGate.build();
  xorGate.build();
  xnorGate.build();

  // NOT gate
  for (int a = 0; a <= 1; ++a) {
    double vin = a * VDD;
    notGate.setInput(vin);
    double vout = notGate.computeDC();
    int expected = gateNot(a);
    if (expected == 1) {
      EXPECT_GT(vout, V_HIGH_MIN) << "NOT(" << a << ") mismatch";
    } else {
      EXPECT_LT(vout, V_LOW_MAX) << "NOT(" << a << ") mismatch";
    }
  }

  // Two-input gates
  for (int a = 0; a <= 1; ++a) {
    for (int b = 0; b <= 1; ++b) {
      double va = a * VDD;
      double vb = b * VDD;

      auto check = [&](const char* name, double vout, int expected) {
        if (expected == 1) {
          EXPECT_GT(vout, V_HIGH_MIN) << name << "(" << a << "," << b << ") mismatch, got " << vout;
        } else {
          EXPECT_LT(vout, V_LOW_MAX) << name << "(" << a << "," << b << ") mismatch, got " << vout;
        }
      };

      nandGate.setInputs(va, vb);
      check("NAND", nandGate.computeDC(), gateNand(a, b));

      norGate.setInputs(va, vb);
      check("NOR", norGate.computeDC(), gateNor(a, b));

      andGate.setInputs(va, vb);
      check("AND", andGate.computeDC(), gateAnd(a, b));

      orGate.setInputs(va, vb);
      check("OR", orGate.computeDC(), gateOr(a, b));

      xorGate.setInputs(va, vb);
      check("XOR", xorGate.computeDC(), gateXor(a, b));

      xnorGate.setInputs(va, vb);
      check("XNOR", xnorGate.computeDC(), gateXnor(a, b));
    }
  }
}
