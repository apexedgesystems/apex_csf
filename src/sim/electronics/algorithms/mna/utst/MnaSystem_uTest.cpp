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

#include <algorithm>
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

/** @test A transconductance moves gm * Vctrl between its output nets and loads neither control net
 */
TEST(MnaSystemTest, TransconductanceDrivesOutputFromControlVoltage) {
  // Net 0 = GND. Control nets 1 and 2 are held by sources; output nets 3 and
  // 4 each have a load to ground. No output or control net is ground, so each
  // of the four stamped entries decides a result.
  constexpr double GM = 2e-3;
  constexpr double V_CTRL_POS = 1.5;
  constexpr double V_CTRL_NEG = 0.5;
  constexpr double R_FROM = 1000.0;
  constexpr double R_TO = 500.0;

  MnaSystem mna(5);
  const std::size_t POS_SOURCE = mna.addVoltageSource(1, 0, V_CTRL_POS);
  const std::size_t NEG_SOURCE = mna.addVoltageSource(2, 0, V_CTRL_NEG);
  mna.addConductance(3, 0, 1.0 / R_FROM);
  mna.addConductance(4, 0, 1.0 / R_TO);
  mna.addTransconductance(/*from=*/3, /*to=*/4, /*ctrlPos=*/1, /*ctrlNeg=*/2, GM);

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);

  // 2 mA leaves net 3 through the element and enters net 4.
  const double I_OUT = GM * (V_CTRL_POS - V_CTRL_NEG);
  EXPECT_NEAR(R.nodeVoltages[3], -I_OUT * R_FROM, TOL);
  EXPECT_NEAR(R.nodeVoltages[4], I_OUT * R_TO, TOL);
  EXPECT_NEAR(R.branchCurrents[POS_SOURCE], 0.0, TOL);
  EXPECT_NEAR(R.branchCurrents[NEG_SOURCE], 0.0, TOL);
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

  std::vector<double> nodeV(3);
  std::vector<double> vsrc(1);
#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(mna.factorize(ws));
  EXPECT_TRUE(ws.isFactorized());

  ASSERT_TRUE(mna.solveFactorized(ws, nodeV.data(), vsrc.data()));

  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(nodeV[i], SHOT.nodeVoltages[i], TOL);
  }
#else
  // Kept factors need LAPACKE: without it both calls refuse.
  EXPECT_FALSE(mna.factorize(ws));
  EXPECT_FALSE(ws.isFactorized());
  EXPECT_FALSE(mna.solveFactorized(ws, nodeV.data(), vsrc.data()));
#endif
}

/** @test FactorizedWorkspace.invalidate forces a re-factorization */
TEST(MnaSystemTest, FactorizedWorkspaceInvalidate) {
  MnaFactorizedWorkspace ws;
  ws.prepare(4);
  EXPECT_FALSE(ws.isFactorized());

  MnaSystem mna(2);
  mna.addConductance(1, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 1.0);
#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(mna.factorize(ws));
  EXPECT_TRUE(ws.isFactorized());
#else
  EXPECT_FALSE(mna.factorize(ws));
#endif

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

/* ----------------------------- External-Matrix Mode ----------------------------- */

namespace {

/** @brief 10 V on net 1 over `upper` ohms to net 2 over `lower` ohms to ground. */
void stampDivider(MnaSystem& mna, double upper, double lower) {
  mna.addConductance(1, 2, 1.0 / upper);
  mna.addConductance(2, 0, 1.0 / lower);
  mna.addVoltageSource(1, 0, 10.0);
}

} // namespace

/** @test clear() in external-matrix mode leaves none of the earlier stamps */
TEST(MnaSystemTest, ClearInExternalModeRemovesEarlierStamps) {
  MnaSolveWorkspace ws;
  ws.prepare(4);
  std::fill(ws.A.begin(), ws.A.end(), 0.0);
  MnaSystem mna(3, ws.A.data(), 4);
  std::vector<double> voltages(3);
  std::vector<double> currents(1);

  stampDivider(mna, 1000.0, 1000.0);
  mna.clear();
  stampDivider(mna, 1000.0, 3000.0);

  // The external matrix (column-major, four to a column) holds the second
  // divider alone.
  EXPECT_NEAR(ws.A[1 * 4 + 1], 1e-3, TOL);
  EXPECT_NEAR(ws.A[2 * 4 + 2], 1e-3 + 1.0 / 3000.0, TOL);
  EXPECT_NEAR(ws.A[2 * 4 + 1], -1e-3, TOL);
  EXPECT_NEAR(ws.A[1 * 4 + 2], -1e-3, TOL);

#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(mna.solveInto(ws, voltages.data(), currents.data()));
  EXPECT_NEAR(voltages[2], 7.5, TOL);
#else
  // Without LAPACKE an external matrix is not solved.
  EXPECT_FALSE(mna.solveInto(ws, voltages.data(), currents.data()));
#endif
}

/** @test clear() after a solve in external-matrix mode makes the system reusable */
TEST(MnaSystemTest, ClearInExternalModeAfterSolveAllowsReuse) {
  MnaSolveWorkspace ws;
  ws.prepare(4);
  std::fill(ws.A.begin(), ws.A.end(), 0.0);
  MnaSystem mna(3, ws.A.data(), 4);
  std::vector<double> voltages(3);
  std::vector<double> currents(1);

  stampDivider(mna, 1000.0, 1000.0);
#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(mna.solveInto(ws, voltages.data(), currents.data()));
  EXPECT_NEAR(voltages[2], 5.0, TOL);
#else
  // Without LAPACKE an external matrix is not solved; what a solve would
  // leave outside the nets' block stands in for it.
  EXPECT_FALSE(mna.solveInto(ws, voltages.data(), currents.data()));
  ws.A[3 * 4 + 3] = 9.0;
#endif

  // The solve left its factors in the matrix; clear() leaves none of them.
  mna.clear();
  EXPECT_EQ(std::count(ws.A.begin(), ws.A.end(), 0.0), 16);

  stampDivider(mna, 1000.0, 3000.0);
#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(mna.solveInto(ws, voltages.data(), currents.data()));
  EXPECT_NEAR(voltages[2], 7.5, TOL);
  EXPECT_NEAR(currents[0], -10.0 / 4000.0, TOL);
#endif
}

/** @test The solves that read the system's own matrix refuse a system that has none */
TEST(MnaSystemTest, SolvesThatNeedOwnMatrixRefuseBorrowedStorage) {
  MnaSolveWorkspace ws;
  ws.prepare(4);
  std::fill(ws.A.begin(), ws.A.end(), 0.0);
  MnaSystem external(3, ws.A.data(), 4);
  stampDivider(external, 1000.0, 1000.0);

  const std::vector<double> X(4, 1.0);
  std::vector<double> ax(4, 0.0);
  MnaSystem matvec(3, X.data(), ax.data());
  stampDivider(matvec, 1000.0, 1000.0);

  for (const MnaSystem* SYSTEM : {&external, &matvec}) {
    const auto RESULT = SYSTEM->solve();
    EXPECT_FALSE(RESULT.success);
    EXPECT_FALSE(RESULT.errorMessage.empty());

    MnaFactorizedWorkspace factors;
    factors.prepare(4);
    EXPECT_FALSE(SYSTEM->factorize(factors));
    EXPECT_FALSE(factors.factorized);

    // Nothing of its own to copy: the outputs stay as given.
    std::vector<double> augmented(16, 0.0);
    std::vector<double> rhs(4, 0.0);
    SYSTEM->buildAugmentedMatrix(augmented.data(), rhs.data());
    EXPECT_EQ(std::count(augmented.begin(), augmented.end(), 0.0), 16);
    EXPECT_EQ(std::count(rhs.begin(), rhs.end(), 0.0), 4);
  }

  // A matvec system has no matrix at all to solve, and an external-matrix
  // system solves in the workspace it stamps into and no other.
  // The other workspace holds a divider of its own, which would solve.
  MnaSolveWorkspace other;
  other.prepare(4);
  std::fill(other.A.begin(), other.A.end(), 0.0);
  MnaSystem elsewhere(3, other.A.data(), 4);
  stampDivider(elsewhere, 1000.0, 3000.0);
  std::vector<double> voltages(3);
  std::vector<double> currents(1);
  EXPECT_FALSE(matvec.solveInto(other, voltages.data(), currents.data()));
  EXPECT_FALSE(external.solveInto(other, voltages.data(), currents.data()));
#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(external.solveInto(ws, voltages.data(), currents.data()));
  EXPECT_NEAR(voltages[2], 5.0, TOL);
#else
  // Without LAPACKE an external matrix is not solved in its own workspace either.
  EXPECT_FALSE(external.solveInto(ws, voltages.data(), currents.data()));
#endif

  // An external matrix of the nets' size has no room for a source's row.
  std::fill(ws.A.begin(), ws.A.end(), 0.0);
  MnaSystem narrow(3, ws.A.data(), 3);
  stampDivider(narrow, 1000.0, 1000.0);
  EXPECT_FALSE(narrow.solveInto(ws, voltages.data(), currents.data()));
  // Refused before anything is written past the nets' block.
  EXPECT_EQ(std::count(ws.A.begin() + 9, ws.A.end(), 0.0), 7);
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
