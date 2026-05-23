/**
 * @file MnaSystem_uTest.cpp
 * @brief Unit tests for the dense `MnaSystem` solver.
 *
 * Mirrors the per-test surface of MnaSystemSparse_uTest.cpp but exercises
 * the dense LAPACK path: stamping, voltage-source augmentation, single-shot
 * solve, cached factorize+solveFactorized, clear-and-reuse, workspace
 * invalidate, augmented-matrix construction, and dimension/voltage-source
 * accessors.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using sim::electronics::algorithms::mna::MnaFactorizedWorkspace;
using sim::electronics::algorithms::mna::MnaSolveWorkspace;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;

namespace {

constexpr double TOL = 1e-9;

}

/* ----------------------------- Construction ----------------------------- */

/** @test Default state -- net count, no voltage sources, augmented dim equals netCount */
TEST(MnaSystemTest, ConstructionSetsNetCount) {
  const MnaSystem MNA{4};
  EXPECT_EQ(MNA.netCount(), 4u);
  EXPECT_EQ(MNA.voltageSourceCount(), 0u);
  EXPECT_EQ(MNA.augmentedDim(), 4u);
}

/** @test addVoltageSource returns sequential indices; augmentedDim grows */
TEST(MnaSystemTest, AddVoltageSourceGrowsAugmentedDim) {
  MnaSystem mna(3);
  EXPECT_EQ(mna.addVoltageSource(/*pos=*/1, /*neg=*/0, /*v=*/1.0), 0u);
  EXPECT_EQ(mna.augmentedDim(), 4u);
  EXPECT_EQ(mna.addVoltageSource(2, 0, 2.5), 1u);
  EXPECT_EQ(mna.augmentedDim(), 5u);
}

/* ----------------------------- Single-shot solve ----------------------------- */

/** @test Voltage divider: two equal resistors split a 10V source in half */
TEST(MnaSystemTest, VoltageDividerSolveIsAccurate) {
  // Net 0 = GND, 1 = VIN, 2 = MID
  MnaSystem mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0); // R = 1k
  mna.addConductance(2, 0, 1.0 / 1000.0); // R = 1k
  mna.addVoltageSource(1, 0, 10.0);

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  EXPECT_NEAR(R.nodeVoltages[1], 10.0, TOL);
  EXPECT_NEAR(R.nodeVoltages[2], 5.0, TOL);
}

/** @test One-resistor circuit with a current source produces V = I*R */
TEST(MnaSystemTest, CurrentSourceWithResistorComputesVoltage) {
  MnaSystem mna(2);
  mna.addConductance(1, 0, 1.0 / 500.0); // R = 500 ohm
  mna.addCurrent(1, 0, 1e-3);            // 1 mA into net 1

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  // V = I * R = 1mA * 500 = 0.5V
  EXPECT_NEAR(R.nodeVoltages[1], 0.5, TOL);
}

/** @test Parallel resistors give equivalent conductance */
TEST(MnaSystemTest, ParallelResistorsEquivalentConductance) {
  MnaSystem mna(2);
  mna.addConductance(1, 0, 1.0 / 1000.0); // 1k
  mna.addConductance(1, 0, 1.0 / 1000.0); // 1k -- 500 in parallel
  mna.addCurrent(1, 0, 1e-3);

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  // V = I * (R1 || R2) = 1mA * 500 = 0.5V
  EXPECT_NEAR(R.nodeVoltages[1], 0.5, TOL);
}

/* ----------------------------- factorize + solveFactorized ----------------------------- */

/** @test Cached factorize+solveFactorized matches single-shot solve */
TEST(MnaSystemTest, FactorizedSolveMatchesSingleShot) {
  MnaSystem ref(3);
  ref.addConductance(1, 2, 1.0 / 1000.0);
  ref.addConductance(2, 0, 1.0 / 1000.0);
  ref.addVoltageSource(1, 0, 5.0);
  const auto SHOT = ref.solve();
  ASSERT_TRUE(SHOT.success);

  MnaSystem mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 5.0);

  MnaFactorizedWorkspace ws;
  ws.prepare(/*maxSize=*/8);
  EXPECT_FALSE(ws.isFactorized());

  ASSERT_TRUE(mna.factorize(ws));
  EXPECT_TRUE(ws.isFactorized());

  std::vector<double> nodeV(3);
  std::vector<double> vsrc(1);
  ASSERT_TRUE(mna.solveFactorized(ws, nodeV.data(), vsrc.data()));

  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(nodeV[i], SHOT.nodeVoltages[i], TOL);
  }
}

/** @test FactorizedWorkspace.invalidate forces a re-factorization */
TEST(MnaSystemTest, FactorizedWorkspaceInvalidate) {
  MnaFactorizedWorkspace ws;
  ws.prepare(4);
  EXPECT_FALSE(ws.isFactorized());

  MnaSystem mna(2);
  mna.addConductance(1, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 1.0);
  ASSERT_TRUE(mna.factorize(ws));
  EXPECT_TRUE(ws.isFactorized());

  ws.invalidate();
  EXPECT_FALSE(ws.isFactorized());
}

/* ----------------------------- solveInto into caller buffers ----------------------------- */

/** @test solveInto writes the solve result into externally-owned buffers */
TEST(MnaSystemTest, SolveIntoWritesToBuffers) {
  MnaSystem mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 4.0);

  MnaSolveWorkspace ws;
  ws.prepare(8);
  EXPECT_TRUE(ws.canHandle(8));
  EXPECT_TRUE(ws.canHandle(3));
  EXPECT_FALSE(ws.canHandle(9));

  std::vector<double> nodeV(3);
  std::vector<double> vsrc(1);
  ASSERT_TRUE(mna.solveInto(ws, nodeV.data(), vsrc.data()));

  EXPECT_NEAR(nodeV[1], 4.0, TOL);
  EXPECT_NEAR(nodeV[2], 2.0, TOL);
}

/* ----------------------------- clear() / clearCurrents() / clearRHS()
 * ----------------------------- */

/** @test clear() resets the matrix and voltage-source list, allowing reuse */
TEST(MnaSystemTest, ClearResetsAndAllowsReuse) {
  MnaSystem mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 6.0);
  EXPECT_TRUE(mna.solve().success);

  mna.clear();
  EXPECT_EQ(mna.voltageSourceCount(), 0u);
  EXPECT_EQ(mna.augmentedDim(), 3u);

  // Restamp; second solve should still work.
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 8.0);
  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  EXPECT_NEAR(R.nodeVoltages[2], 4.0, TOL);
}

/** @test clearCurrents leaves the conductance matrix intact for re-solve */
TEST(MnaSystemTest, ClearCurrentsLeavesConductanceMatrix) {
  MnaSystem mna(2);
  mna.addConductance(1, 0, 1.0 / 500.0);
  mna.addCurrent(1, 0, 1e-3);
  EXPECT_TRUE(mna.solve().success);

  mna.clearCurrents();
  // Conductance matrix kept; restamp current and re-solve.
  mna.addCurrent(1, 0, 2e-3);
  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  EXPECT_NEAR(R.nodeVoltages[1], 2e-3 * 500.0, TOL);
}

/** @test clearRHS zeroes the RHS, leaving the matrix intact */
TEST(MnaSystemTest, ClearRhsZeroesCurrentVector) {
  MnaSystem mna(2);
  mna.addConductance(1, 0, 1.0 / 500.0);
  mna.addCurrent(1, 0, 1e-3);
  EXPECT_TRUE(mna.solve().success);

  mna.clearRHS();
  // No RHS left -- solve gives zero.
  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  EXPECT_NEAR(R.nodeVoltages[1], 0.0, TOL);
}

/* ----------------------------- buildAugmentedMatrix ----------------------------- */

/** @test buildAugmentedMatrix copies the augmented [G | B; C | 0] matrix */
TEST(MnaSystemTest, BuildAugmentedMatrixWritesShape) {
  MnaSystem mna(3);
  mna.addConductance(1, 2, 1.0);
  mna.addVoltageSource(1, 0, 5.0);

  const std::size_t DIM = mna.augmentedDim();
  EXPECT_EQ(DIM, 4u);
  std::vector<double> A(DIM * DIM, 0.0);
  std::vector<double> b(DIM, 0.0);
  mna.buildAugmentedMatrix(A.data(), b.data());

  // The voltage source row stamps a 1.0 in the C block at (3, 1).
  EXPECT_NEAR(A[3 * DIM + 1], 1.0, TOL);
  // RHS at the voltage-source slot should be 5 V.
  EXPECT_NEAR(b[3], 5.0, TOL);
}

/* ----------------------------- Determinism ----------------------------- */

/** @test Repeated solves return identical node voltages */
TEST(MnaSystemDeterminismTest, SolveIsRepeatable) {
  MnaSystem mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 3.3);

  const auto FIRST = mna.solve();
  ASSERT_TRUE(FIRST.success);
  for (int i = 0; i < 10; ++i) {
    const auto SAMPLE = mna.solve();
    ASSERT_TRUE(SAMPLE.success);
    EXPECT_DOUBLE_EQ(SAMPLE.nodeVoltages[2], FIRST.nodeVoltages[2]);
  }
}
