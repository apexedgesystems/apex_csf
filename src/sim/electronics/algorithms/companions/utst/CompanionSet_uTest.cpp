/**
 * @file CompanionSet_uTest.cpp
 * @brief Unit tests for the CompanionSet aggregate.
 *
 * CompanionSet manages a collection of CapacitorCompanion and
 * InductorCompanion records and exposes bulk stamp / update / reset
 * operations used by the transient solver.
 *
 * Notes:
 *  - Tests use a small dense MnaSystem so we can read back individual
 *    matrix / RHS entries and compare against the per-device formulas
 *    already verified in CompanionModels_uTest.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using sim::electronics::algorithms::companions::CompanionSet;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::transient::IntegrationMethod;

namespace {

constexpr double TOL = 1e-12;

// Read G[row, col] from a flat row-major MNA conductance matrix.
double g(const MnaSystem& mna, std::size_t row, std::size_t col) {
  return mna.conductanceMatrix()[row * mna.netCount() + col];
}

double rhs(const MnaSystem& mna, std::size_t row) { return mna.currentVector()[row]; }

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed CompanionSet is empty */
TEST(CompanionSetDefaultTest, DefaultIsEmpty) {
  const CompanionSet SET{};
  EXPECT_EQ(SET.capacitorCount(), 0u);
  EXPECT_EQ(SET.inductorCount(), 0u);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test addCapacitor stores parameters and returns sequential index */
TEST(CompanionSetTest, AddCapacitorReturnsSequentialIndex) {
  CompanionSet set;
  EXPECT_EQ(set.addCapacitor(/*pos=*/1, /*neg=*/2, /*C=*/1e-6), 0u);
  EXPECT_EQ(set.addCapacitor(/*pos=*/3, /*neg=*/0, /*C=*/2e-6), 1u);
  EXPECT_EQ(set.capacitorCount(), 2u);

  const auto& C0 = set.capacitor(0);
  EXPECT_EQ(C0.posNet, 1u);
  EXPECT_EQ(C0.negNet, 2u);
  EXPECT_DOUBLE_EQ(C0.capacitance, 1e-6);
}

/** @test addInductor stores parameters and returns sequential index */
TEST(CompanionSetTest, AddInductorReturnsSequentialIndex) {
  CompanionSet set;
  EXPECT_EQ(set.addInductor(/*pos=*/1, /*neg=*/2, /*L=*/1e-3), 0u);
  EXPECT_EQ(set.addInductor(/*pos=*/4, /*neg=*/0, /*L=*/5e-3), 1u);
  EXPECT_EQ(set.inductorCount(), 2u);

  const auto& I1 = set.inductor(1);
  EXPECT_EQ(I1.posNet, 4u);
  EXPECT_EQ(I1.negNet, 0u);
  EXPECT_DOUBLE_EQ(I1.inductance, 5e-3);
}

/* ----------------------------- stampAll ----------------------------- */

/** @test stampAll writes capacitor Geq + Ieq into the MNA system */
TEST(CompanionSetTest, StampAllStampsCapacitorBackwardEuler) {
  CompanionSet set;
  set.addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/1e-6);
  set.capacitor(0).prevVoltage = 0.5;

  MnaSystem mna(/*nodeCount=*/2);
  const double DT = 1e-3;
  set.stampAll(mna, DT, IntegrationMethod::BACKWARD_EULER);

  const double EXPECTED_G = 1e-6 / DT;
  const double EXPECTED_I = 1e-6 * 0.5 / DT;
  EXPECT_NEAR(g(mna, 1, 1), EXPECTED_G, TOL);
  EXPECT_NEAR(rhs(mna, 1), EXPECTED_I, TOL);
}

/** @test stampAll writes inductor Geq + Ieq into the MNA system */
TEST(CompanionSetTest, StampAllStampsInductorBackwardEuler) {
  CompanionSet set;
  set.addInductor(/*pos=*/1, /*neg=*/0, /*L=*/1e-3);
  set.inductor(0).prevCurrent = 0.05; // 50 mA

  MnaSystem mna(/*nodeCount=*/2);
  const double DT = 1e-6;
  set.stampAll(mna, DT, IntegrationMethod::BACKWARD_EULER);

  const double EXPECTED_G = DT / 1e-3;
  EXPECT_NEAR(g(mna, 1, 1), EXPECTED_G, TOL);
  // Inductor stamps current into negNet (0 = ground); pos receives -Ieq.
  EXPECT_NEAR(rhs(mna, 1), -0.05, TOL);
}

/* ----------------------------- stampConductanceAll / stampCurrentAll -----------------------------
 */

/** @test Conductance-only and current-only stamps reconstruct full stamp */
TEST(CompanionSetTest, ConductanceAndCurrentDecompositionMatchesFullStamp) {
  CompanionSet set;
  set.addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/1e-6);
  set.addInductor(/*pos=*/2, /*neg=*/0, /*L=*/1e-3);
  set.capacitor(0).prevVoltage = 0.7;
  set.inductor(0).prevCurrent = 0.02;

  const double DT = 1e-4;
  MnaSystem fullMna(/*nodeCount=*/3);
  MnaSystem splitMna(/*nodeCount=*/3);

  set.stampAll(fullMna, DT, IntegrationMethod::BACKWARD_EULER);
  set.stampConductanceAll(splitMna, DT, IntegrationMethod::BACKWARD_EULER);
  set.stampCurrentAll(splitMna, DT, IntegrationMethod::BACKWARD_EULER);

  for (std::size_t i = 1; i < 3; ++i) {
    EXPECT_NEAR(g(fullMna, i, i), g(splitMna, i, i), TOL);
    EXPECT_NEAR(rhs(fullMna, i), rhs(splitMna, i), TOL);
  }
}

/* ----------------------------- updateAll ----------------------------- */

/** @test updateAll advances each capacitor's prevVoltage from solved nodes */
TEST(CompanionSetTest, UpdateAllAdvancesCapacitorState) {
  CompanionSet set;
  set.addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/1e-6);
  set.capacitor(0).prevVoltage = 0.0;

  const std::vector<double> V = {0.0, 0.5, 0.0};
  const double DT = 1e-3;
  set.updateAll(V, DT);

  EXPECT_DOUBLE_EQ(set.capacitor(0).prevVoltage, 0.5);
  EXPECT_NEAR(set.capacitor(0).current, 1e-6 * 0.5 / DT, TOL);
}

/** @test updateAll advances each inductor's prevCurrent integral */
TEST(CompanionSetTest, UpdateAllAdvancesInductorState) {
  CompanionSet set;
  set.addInductor(/*pos=*/1, /*neg=*/0, /*L=*/1e-3);
  set.inductor(0).prevCurrent = 0.0;

  const std::vector<double> V = {0.0, 0.1, 0.0};
  const double DT = 1e-6;
  set.updateAll(V, DT);

  EXPECT_DOUBLE_EQ(set.inductor(0).voltage, 0.1);
  EXPECT_NEAR(set.inductor(0).prevCurrent, (DT / 1e-3) * 0.1, TOL);
}

/* ----------------------------- reset ----------------------------- */

/** @test reset zeros every companion's history */
TEST(CompanionSetTest, ResetZerosAllHistory) {
  CompanionSet set;
  set.addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/1e-6);
  set.addInductor(/*pos=*/2, /*neg=*/0, /*L=*/1e-3);
  set.capacitor(0).prevVoltage = 1.0;
  set.capacitor(0).prev2Voltage = 0.5;
  set.capacitor(0).current = 1e-3;
  set.inductor(0).prevCurrent = 0.05;
  set.inductor(0).prev2Current = 0.02;
  set.inductor(0).voltage = 0.1;

  set.reset();

  EXPECT_DOUBLE_EQ(set.capacitor(0).prevVoltage, 0.0);
  EXPECT_DOUBLE_EQ(set.capacitor(0).prev2Voltage, 0.0);
  EXPECT_DOUBLE_EQ(set.capacitor(0).current, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).prevCurrent, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).prev2Current, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).voltage, 0.0);
}

/* ----------------------------- initializeFromDC ----------------------------- */

/** @test initializeFromDC seeds capacitor prevVoltage from node voltages */
TEST(CompanionSetTest, InitializeFromDCSeedsCapacitorVoltage) {
  CompanionSet set;
  set.addCapacitor(/*pos=*/2, /*neg=*/1, /*C=*/1e-6);
  set.capacitor(0).current = 1e-4; // any non-zero starting current

  const std::vector<double> V_DC = {0.0, 0.7, 1.5}; // node 0=GND, 1=0.7V, 2=1.5V
  set.initializeFromDC(V_DC);

  // V_pos - V_neg = 1.5 - 0.7 = 0.8
  EXPECT_DOUBLE_EQ(set.capacitor(0).prevVoltage, 0.8);
  // No current in steady state.
  EXPECT_DOUBLE_EQ(set.capacitor(0).current, 0.0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test stampAll is deterministic across repeated calls */
TEST(CompanionSetDeterminismTest, StampAllIsRepeatable) {
  CompanionSet set;
  set.addCapacitor(/*pos=*/1, /*neg=*/0, /*C=*/2e-9);
  set.capacitor(0).prevVoltage = 0.3;

  const double DT = 1e-6;
  MnaSystem first(/*nodeCount=*/2);
  set.stampAll(first, DT, IntegrationMethod::TRAPEZOIDAL);

  for (int i = 0; i < 5; ++i) {
    MnaSystem repeat(/*nodeCount=*/2);
    set.stampAll(repeat, DT, IntegrationMethod::TRAPEZOIDAL);
    EXPECT_DOUBLE_EQ(g(first, 1, 1), g(repeat, 1, 1));
    EXPECT_DOUBLE_EQ(rhs(first, 1), rhs(repeat, 1));
  }
}
