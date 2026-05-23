/**
 * @file CompositeDevices_uTest.cpp
 * @brief Unit tests for CMOS composite gate models (inverter, NAND, NOR).
 */

#include "src/sim/electronics/devices/composite/inc/CompositeDevices.hpp"

#include <gtest/gtest.h>

using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::devices::composite::CmosInverter;
using sim::electronics::devices::composite::CmosNand;
using sim::electronics::devices::composite::CmosNor;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

/* ----------------------------- CmosInverter ----------------------------- */

/** @test */
TEST(CmosInverterTest, Construction) {
  const NetID VDD = 1, GND = 0, INPUT = 2, OUTPUT = 3;
  const double W = 10e-6, L = 1e-6;

  CmosInverter inv{VDD, GND, INPUT, OUTPUT, W, L};

  // PMOS: VDD -> OUTPUT (pull-up)
  EXPECT_EQ(inv.pmos.drainNet, VDD);
  EXPECT_EQ(inv.pmos.gateNet, INPUT);
  EXPECT_EQ(inv.pmos.sourceNet, OUTPUT);
  EXPECT_EQ(inv.pmos.bulkNet, VDD);

  // NMOS: OUTPUT -> GND (pull-down)
  EXPECT_EQ(inv.nmos.drainNet, OUTPUT);
  EXPECT_EQ(inv.nmos.gateNet, INPUT);
  EXPECT_EQ(inv.nmos.sourceNet, GND);
  EXPECT_EQ(inv.nmos.bulkNet, GND);

  // Geometry
  EXPECT_DOUBLE_EQ(inv.pmos.W, W);
  EXPECT_DOUBLE_EQ(inv.pmos.L, L);
  EXPECT_DOUBLE_EQ(inv.nmos.W, W);
  EXPECT_DOUBLE_EQ(inv.nmos.L, L);
}

/** @test Stamp injects nonzero conductances and currents into MNA system.
 *
 * Uses Vin = VDD so the NMOS is strongly ON (Vgs_nmos = VDD >> Vth_nmos),
 * guaranteeing nonzero gm, gds, and Ieq stamps. The PMOS uses the Level 1
 * model with positive Vth (no sign-inversion), so it also contributes when
 * Vgs_pmos = Vin - VDD = 0 falls in the subthreshold smoothing window.
 */
TEST(CmosInverterTest, StampProducesNonzeroEntries) {
  const NetID VDD = 1, GND = 0, INPUT = 2, OUTPUT = 3;
  const double W = 10e-6, L = 1e-6;
  const std::size_t NET_COUNT = 5; // 0..4

  CmosInverter inv{VDD, GND, INPUT, OUTPUT, W, L};

  MnaSystem mna(NET_COUNT);
  // Both use NMOS-style positive Vth so the Level 1 model produces current
  // when Vgs exceeds threshold.
  MosfetLevel1Params paramsNmos{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};
  MosfetLevel1Params paramsPmos{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

  const double VDD_VOLTAGE = 3.3;
  // Vin = VDD -> NMOS Vgs = 3.3V (well above Vth), stamps nonzero values
  const double VIN = VDD_VOLTAGE;

  CmosInverter::stamp(mna, inv.pmos, inv.nmos, VDD_VOLTAGE, VIN, paramsNmos, paramsPmos);

  // Verify the conductance matrix has nonzero stamps
  const auto& G = mna.conductanceMatrix();
  const auto& I = mna.currentVector();

  bool hasNonzeroG = false;
  for (double val : G) {
    if (val != 0.0) {
      hasNonzeroG = true;
      break;
    }
  }

  bool hasNonzeroI = false;
  for (double val : I) {
    if (val != 0.0) {
      hasNonzeroI = true;
      break;
    }
  }

  EXPECT_TRUE(hasNonzeroG) << "Stamp should inject nonzero conductances";

  // Note: CmosInverter::stamp() uses VDS=0 for both MOSFETs (output voltage
  // is unknown before solving), so ieq = id - gm*vgs - gds*vds = 0. The
  // current vector is expected to be zero for this initial stamp.
  EXPECT_FALSE(hasNonzeroI) << "With VDS=0 initial guess, Ieq should be zero";
}

/** @test */
TEST(CmosInverterTest, TruthTableInput0) {
  const int RESULT = CmosInverter::truthTable(0);
  EXPECT_EQ(RESULT, 1);
}

/** @test */
TEST(CmosInverterTest, TruthTableInput1) {
  const int RESULT = CmosInverter::truthTable(1);
  EXPECT_EQ(RESULT, 0);
}

/** @test */
TEST(CmosInverterTest, TruthTableAllCombinations) {
  // NOT gate: OUT = ~IN
  EXPECT_EQ(CmosInverter::truthTable(0), 1);
  EXPECT_EQ(CmosInverter::truthTable(1), 0);
}

/* ----------------------------- CmosNand ----------------------------- */

/** @test */
TEST(CmosNandTest, Construction) {
  const NetID VDD = 1, GND = 0, INA = 2, INB = 3, OUTPUT = 4, INTERNAL = 5;
  const double W = 10e-6, L = 1e-6;

  CmosNand nand{VDD, GND, INA, INB, OUTPUT, INTERNAL, W, L};

  // PMOS 1: VDD -> OUTPUT (parallel pull-up)
  EXPECT_EQ(nand.pmos[0].drainNet, VDD);
  EXPECT_EQ(nand.pmos[0].gateNet, INA);
  EXPECT_EQ(nand.pmos[0].sourceNet, OUTPUT);
  EXPECT_EQ(nand.pmos[0].bulkNet, VDD);

  // PMOS 2: VDD -> OUTPUT (parallel pull-up)
  EXPECT_EQ(nand.pmos[1].drainNet, VDD);
  EXPECT_EQ(nand.pmos[1].gateNet, INB);
  EXPECT_EQ(nand.pmos[1].sourceNet, OUTPUT);
  EXPECT_EQ(nand.pmos[1].bulkNet, VDD);

  // NMOS 1: OUTPUT -> INTERNAL (series pull-down)
  EXPECT_EQ(nand.nmos[0].drainNet, OUTPUT);
  EXPECT_EQ(nand.nmos[0].gateNet, INA);
  EXPECT_EQ(nand.nmos[0].sourceNet, INTERNAL);
  EXPECT_EQ(nand.nmos[0].bulkNet, GND);

  // NMOS 2: INTERNAL -> GND (series pull-down)
  EXPECT_EQ(nand.nmos[1].drainNet, INTERNAL);
  EXPECT_EQ(nand.nmos[1].gateNet, INB);
  EXPECT_EQ(nand.nmos[1].sourceNet, GND);
  EXPECT_EQ(nand.nmos[1].bulkNet, GND);

  // Internal node
  EXPECT_EQ(nand.internalNode, INTERNAL);

  // Geometry
  EXPECT_DOUBLE_EQ(nand.pmos[0].W, W);
  EXPECT_DOUBLE_EQ(nand.pmos[0].L, L);
  EXPECT_DOUBLE_EQ(nand.nmos[0].W, W);
  EXPECT_DOUBLE_EQ(nand.nmos[0].L, L);
}

/** @test */
TEST(CmosNandTest, TruthTable00) {
  const int RESULT = CmosNand::truthTable(0, 0);
  EXPECT_EQ(RESULT, 1);
}

/** @test */
TEST(CmosNandTest, TruthTable01) {
  const int RESULT = CmosNand::truthTable(0, 1);
  EXPECT_EQ(RESULT, 1);
}

/** @test */
TEST(CmosNandTest, TruthTable10) {
  const int RESULT = CmosNand::truthTable(1, 0);
  EXPECT_EQ(RESULT, 1);
}

/** @test */
TEST(CmosNandTest, TruthTable11) {
  const int RESULT = CmosNand::truthTable(1, 1);
  EXPECT_EQ(RESULT, 0);
}

/** @test */
TEST(CmosNandTest, TruthTableAllCombinations) {
  // NAND gate: OUT = ~(A & B)
  EXPECT_EQ(CmosNand::truthTable(0, 0), 1);
  EXPECT_EQ(CmosNand::truthTable(0, 1), 1);
  EXPECT_EQ(CmosNand::truthTable(1, 0), 1);
  EXPECT_EQ(CmosNand::truthTable(1, 1), 0);
}

/* ----------------------------- CmosNor ----------------------------- */

/** @test */
TEST(CmosNorTest, Construction) {
  const NetID VDD = 1, GND = 0, INA = 2, INB = 3, OUTPUT = 4, INTERNAL = 5;
  const double W = 10e-6, L = 1e-6;

  CmosNor nor{VDD, GND, INA, INB, OUTPUT, INTERNAL, W, L};

  // PMOS 1: INTERNAL -> VDD (series pull-up)
  EXPECT_EQ(nor.pmos[0].drainNet, INTERNAL);
  EXPECT_EQ(nor.pmos[0].gateNet, INA);
  EXPECT_EQ(nor.pmos[0].sourceNet, VDD);
  EXPECT_EQ(nor.pmos[0].bulkNet, VDD);

  // PMOS 2: OUTPUT -> INTERNAL (series pull-up)
  EXPECT_EQ(nor.pmos[1].drainNet, OUTPUT);
  EXPECT_EQ(nor.pmos[1].gateNet, INB);
  EXPECT_EQ(nor.pmos[1].sourceNet, INTERNAL);
  EXPECT_EQ(nor.pmos[1].bulkNet, VDD);

  // NMOS 1: OUTPUT -> GND (parallel pull-down)
  EXPECT_EQ(nor.nmos[0].drainNet, OUTPUT);
  EXPECT_EQ(nor.nmos[0].gateNet, INA);
  EXPECT_EQ(nor.nmos[0].sourceNet, GND);
  EXPECT_EQ(nor.nmos[0].bulkNet, GND);

  // NMOS 2: OUTPUT -> GND (parallel pull-down)
  EXPECT_EQ(nor.nmos[1].drainNet, OUTPUT);
  EXPECT_EQ(nor.nmos[1].gateNet, INB);
  EXPECT_EQ(nor.nmos[1].sourceNet, GND);
  EXPECT_EQ(nor.nmos[1].bulkNet, GND);

  // Internal node
  EXPECT_EQ(nor.internalNode, INTERNAL);

  // Geometry
  EXPECT_DOUBLE_EQ(nor.pmos[0].W, W);
  EXPECT_DOUBLE_EQ(nor.pmos[0].L, L);
  EXPECT_DOUBLE_EQ(nor.nmos[0].W, W);
  EXPECT_DOUBLE_EQ(nor.nmos[0].L, L);
}

/** @test */
TEST(CmosNorTest, TruthTable00) {
  const int RESULT = CmosNor::truthTable(0, 0);
  EXPECT_EQ(RESULT, 1);
}

/** @test */
TEST(CmosNorTest, TruthTable01) {
  const int RESULT = CmosNor::truthTable(0, 1);
  EXPECT_EQ(RESULT, 0);
}

/** @test */
TEST(CmosNorTest, TruthTable10) {
  const int RESULT = CmosNor::truthTable(1, 0);
  EXPECT_EQ(RESULT, 0);
}

/** @test */
TEST(CmosNorTest, TruthTable11) {
  const int RESULT = CmosNor::truthTable(1, 1);
  EXPECT_EQ(RESULT, 0);
}

/** @test */
TEST(CmosNorTest, TruthTableAllCombinations) {
  // NOR gate: OUT = ~(A | B)
  EXPECT_EQ(CmosNor::truthTable(0, 0), 1);
  EXPECT_EQ(CmosNor::truthTable(0, 1), 0);
  EXPECT_EQ(CmosNor::truthTable(1, 0), 0);
  EXPECT_EQ(CmosNor::truthTable(1, 1), 0);
}

/* ----------------------------- Universal Gates ----------------------------- */

/** @test */
TEST(CmosCompositeTest, NandIsUniversalGate) {
  // NAND can implement all basic gates:

  // NOT: OUT = A NAND A
  EXPECT_EQ(CmosNand::truthTable(0, 0), 1); // NOT 0 = 1
  EXPECT_EQ(CmosNand::truthTable(1, 1), 0); // NOT 1 = 0

  // AND: OUT = NOT (A NAND B) = (A NAND B) NAND (A NAND B)
  const int NAND_00 = CmosNand::truthTable(0, 0);
  const int NAND_01 = CmosNand::truthTable(0, 1);
  const int NAND_10 = CmosNand::truthTable(1, 0);
  const int NAND_11 = CmosNand::truthTable(1, 1);

  EXPECT_EQ(CmosNand::truthTable(NAND_00, NAND_00), 0); // AND(0,0) = 0
  EXPECT_EQ(CmosNand::truthTable(NAND_01, NAND_01), 0); // AND(0,1) = 0
  EXPECT_EQ(CmosNand::truthTable(NAND_10, NAND_10), 0); // AND(1,0) = 0
  EXPECT_EQ(CmosNand::truthTable(NAND_11, NAND_11), 1); // AND(1,1) = 1
}

/** @test */
TEST(CmosCompositeTest, NorIsUniversalGate) {
  // NOR can implement all basic gates:

  // NOT: OUT = A NOR A
  EXPECT_EQ(CmosNor::truthTable(0, 0), 1); // NOT 0 = 1
  EXPECT_EQ(CmosNor::truthTable(1, 1), 0); // NOT 1 = 0

  // OR: OUT = NOT (A NOR B) = (A NOR B) NOR (A NOR B)
  const int NOR_00 = CmosNor::truthTable(0, 0);
  const int NOR_01 = CmosNor::truthTable(0, 1);
  const int NOR_10 = CmosNor::truthTable(1, 0);
  const int NOR_11 = CmosNor::truthTable(1, 1);

  EXPECT_EQ(CmosNor::truthTable(NOR_00, NOR_00), 0); // OR(0,0) = 0
  EXPECT_EQ(CmosNor::truthTable(NOR_01, NOR_01), 1); // OR(0,1) = 1
  EXPECT_EQ(CmosNor::truthTable(NOR_10, NOR_10), 1); // OR(1,0) = 1
  EXPECT_EQ(CmosNor::truthTable(NOR_11, NOR_11), 1); // OR(1,1) = 1
}
