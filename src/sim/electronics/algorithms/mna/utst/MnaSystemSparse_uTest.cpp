/**
 * @file MnaSystemSparse_uTest.cpp
 * @brief Unit tests for sparse MNA solver.
 *
 * Covers stamping, factorize/solve, what factorize() reuses of an earlier
 * factorization and what it does not, the right-hand-side-only path
 * (tripletsMatch, updateRhs), and accessor invariants.
 *
 * Tests are platform-agnostic: they assert circuit invariants (KCL,
 * superposition) rather than exact KLU internals.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::MnaSystemSparse;

/* ----------------------------- Helper ----------------------------- */

static constexpr double TOL = 1e-9;

/* ----------------------------- Basic Stamping ----------------------------- */

/**
 * @test MnaSystemSparse_VoltageDivider_MatchesExpected
 * @brief 10V supply with 10 ohm and 20 ohm resistors in series.
 *        Current: I = 10V / 30 ohm = 0.333A
 *        V_middle = I * R1 = 0.333A * 10 ohm = 3.33V
 */
TEST(MnaSystemSparseTest, VoltageDivider_MatchesExpected) {
  // 3 nets: ground (0), middle (1), top (2)
  MnaSystemSparse mna(3);

  // 10 ohm from ground to middle
  mna.addConductance(0, 1, 1.0 / 10.0);

  // 20 ohm from middle to top
  mna.addConductance(1, 2, 1.0 / 20.0);

  // 10V source between ground and top
  mna.addVoltageSource(2, 0, 10.0);

  // Factorize and solve
  ASSERT_TRUE(mna.factorize());
  auto result = mna.solve();

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_EQ(result.nodeVoltages.size(), 3u);

  // Check voltages
  EXPECT_NEAR(result.nodeVoltages[0], 0.0, TOL);                // Ground
  EXPECT_NEAR(result.nodeVoltages[1], 10.0 * 10.0 / 30.0, TOL); // Middle: 3.33V
  EXPECT_NEAR(result.nodeVoltages[2], 10.0, TOL);               // Top
}

/**
 * @test MnaSystemSparse_SimplestCircuit_OneResistor
 * @brief Single 10 ohm resistor between 5V supply and ground.
 *        Expected current: 5V / 10 ohm = 0.5A (negative = into voltage source)
 */
TEST(MnaSystemSparseTest, SimplestCircuit_OneResistor) {
  // 2 nets: ground (0), top (1)
  MnaSystemSparse mna(2);

  // 10 ohm resistor between ground and top
  mna.addConductance(0, 1, 1.0 / 10.0);

  // 5V source
  mna.addVoltageSource(1, 0, 5.0);

  ASSERT_TRUE(mna.factorize());
  auto result = mna.solve();

  ASSERT_TRUE(result.success);
  EXPECT_NEAR(result.nodeVoltages[0], 0.0, TOL);
  EXPECT_NEAR(result.nodeVoltages[1], 5.0, TOL);

  // Check current through voltage source (negative = flowing into source)
  ASSERT_EQ(result.branchCurrents.size(), 1u);
  EXPECT_NEAR(result.branchCurrents[0], -0.5, TOL); // I = -V/R = -5/10 = -0.5A
}

/**
 * @test MnaSystemSparse_ParallelResistors_EquivalentConductance
 * @brief Two 10 ohm resistors in parallel (equivalent 5 ohm) with 10V supply.
 *        Total current: 10V / 5 ohm = 2A (negative = into source)
 */
TEST(MnaSystemSparseTest, ParallelResistors_EquivalentConductance) {
  // 2 nets: ground (0), top (1)
  MnaSystemSparse mna(2);

  // Two 10 ohm resistors in parallel
  mna.addConductance(0, 1, 1.0 / 10.0);
  mna.addConductance(0, 1, 1.0 / 10.0);

  // 10V source
  mna.addVoltageSource(1, 0, 10.0);

  ASSERT_TRUE(mna.factorize());
  auto result = mna.solve();

  ASSERT_TRUE(result.success);
  EXPECT_NEAR(result.nodeVoltages[1], 10.0, TOL);

  // Total current: -10V / 5 ohm (equivalent resistance) = -2A
  EXPECT_NEAR(result.branchCurrents[0], -2.0, TOL);
}

/* ----------------------------- Current Injection ----------------------------- */

/**
 * @test MnaSystemSparse_CurrentSource_ComputesVoltage
 * @brief 1A current source through 10 ohm resistor.
 *        addCurrent(0, 1, 1.0) means +1A into node 0, -1A into node 1.
 *        This drives current from ground into node 1, making V[1] negative.
 */
TEST(MnaSystemSparseTest, CurrentSource_ComputesVoltage) {
  // 2 nets: ground (0), top (1)
  MnaSystemSparse mna(2);

  // 10 ohm resistor
  mna.addConductance(0, 1, 1.0 / 10.0);

  // 1A current injection: +1A into node 0, -1A into node 1
  // Negative: convention is node 0 to node 1, so V[1] = -I*R = -10V
  mna.addCurrent(0, 1, 1.0);

  ASSERT_TRUE(mna.factorize());
  auto result = mna.solve();

  ASSERT_TRUE(result.success);
  EXPECT_NEAR(result.nodeVoltages[0], 0.0, TOL);
  EXPECT_NEAR(result.nodeVoltages[1], -10.0, TOL);
}

/* ----------------------------- Comparison with Dense ----------------------------- */

/**
 * @test MnaSystemSparse_MatchesDense_VoltageDivider
 * @brief Sparse and dense MNA solvers should produce identical results.
 */
TEST(MnaSystemSparseTest, MatchesDense_VoltageDivider) {
  // Build same circuit with sparse and dense
  MnaSystemSparse sparse(3);
  MnaSystem dense(3);

  // Same topology: voltage divider
  sparse.addConductance(0, 1, 1.0 / 10.0);
  sparse.addConductance(1, 2, 1.0 / 20.0);
  sparse.addVoltageSource(2, 0, 10.0);

  dense.addConductance(0, 1, 1.0 / 10.0);
  dense.addConductance(1, 2, 1.0 / 20.0);
  dense.addVoltageSource(2, 0, 10.0);

  // Solve both
  ASSERT_TRUE(sparse.factorize());
  auto sparseResult = sparse.solve();
  auto denseResult = dense.solve();

  ASSERT_TRUE(sparseResult.success);
  ASSERT_TRUE(denseResult.success);

  // Compare voltages
  ASSERT_EQ(sparseResult.nodeVoltages.size(), denseResult.nodeVoltages.size());
  for (std::size_t i = 0; i < sparseResult.nodeVoltages.size(); ++i) {
    EXPECT_NEAR(sparseResult.nodeVoltages[i], denseResult.nodeVoltages[i], TOL)
        << "Mismatch at node " << i;
  }

  // Compare currents
  ASSERT_EQ(sparseResult.branchCurrents.size(), denseResult.branchCurrents.size());
  for (std::size_t i = 0; i < sparseResult.branchCurrents.size(); ++i) {
    EXPECT_NEAR(sparseResult.branchCurrents[i], denseResult.branchCurrents[i], TOL)
        << "Mismatch in branch current " << i;
  }
}

/* ----------------------------- Workflow ----------------------------- */

/**
 * @test MnaSystemSparse_SolveWithoutFactorize_Fails
 * @brief Calling solve() before factorize() should fail gracefully.
 */
TEST(MnaSystemSparseTest, SolveWithoutFactorize_Fails) {
  MnaSystemSparse mna(2);
  mna.addConductance(0, 1, 1.0 / 10.0);
  mna.addVoltageSource(1, 0, 5.0);

  // Try to solve without factorizing
  auto result = mna.solve();
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.errorMessage.empty());
}

/* ----------------------------- RT-safe solveInto ----------------------------- */

/**
 * @test MnaSystemSparse_SolveInto_WritesToBuffers
 * @brief solveInto() should write results to pre-allocated buffers.
 */
TEST(MnaSystemSparseTest, SolveInto_WritesToBuffers) {
  MnaSystemSparse mna(3);
  mna.addConductance(0, 1, 1.0 / 10.0);
  mna.addConductance(1, 2, 1.0 / 20.0);
  mna.addVoltageSource(2, 0, 10.0);

  ASSERT_TRUE(mna.factorize());

  // Pre-allocate buffers
  std::vector<double> voltages(3);
  std::vector<double> currents(1);

  ASSERT_TRUE(mna.solveInto(voltages.data(), currents.data()));

  EXPECT_NEAR(voltages[0], 0.0, TOL);
  EXPECT_NEAR(voltages[1], 10.0 * 10.0 / 30.0, TOL); // 3.33V
  EXPECT_NEAR(voltages[2], 10.0, TOL);
}

/* ----------------------------- Sparsity Metrics ----------------------------- */

/**
 * @test MnaSystemSparse_NnzCount_ReflectsStamps
 * @brief nnz() should return number of non-zero entries stamped.
 */
TEST(MnaSystemSparseTest, NnzCount_ReflectsStamps) {
  MnaSystemSparse mna(3);

  // Initial: 0 entries
  EXPECT_EQ(mna.nnz(), 0u);

  // Add one conductance: 4 entries (a-a, b-b, a-b, b-a)
  mna.addConductance(0, 1, 1.0 / 10.0);
  EXPECT_EQ(mna.nnz(), 4u);

  // Add another: 8 entries total
  mna.addConductance(1, 2, 1.0 / 20.0);
  EXPECT_EQ(mna.nnz(), 8u);

  // Clear resets
  mna.clear();
  EXPECT_EQ(mna.nnz(), 0u);
}

/* ----------------------------- Right-Hand Side Only ----------------------------- */

/**
 * @test tripletsMatch() is true after factorize when stamps are unchanged,
 *       and false before any factorization, with stamps or without.
 */
TEST(MnaSystemSparseTest, TripletsMatchAfterFactorize) {
  MnaSystemSparse unstamped(3);
  EXPECT_FALSE(unstamped.tripletsMatch());

  MnaSystemSparse mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 5.0);
  EXPECT_FALSE(mna.tripletsMatch());
  ASSERT_TRUE(mna.factorize());

  EXPECT_TRUE(mna.tripletsMatch());

  // Re-stamping with the exact same values keeps the match true.
  mna.clearStamps();
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 5.0);
  EXPECT_TRUE(mna.tripletsMatch());
}

/** @test tripletsMatch() becomes false when conductance values change. */
TEST(MnaSystemSparseTest, TripletsMatchFalseAfterValueChange) {
  MnaSystemSparse mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 5.0);
  ASSERT_TRUE(mna.factorize());

  // Change one value -- triplet vector now differs from the cached copy.
  mna.clearStamps();
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 2000.0);
  mna.addVoltageSource(1, 0, 5.0);
  EXPECT_FALSE(mna.tripletsMatch());
}

/** @test updateRhs() reuses LU factors to apply a new RHS without re-factorization. */
TEST(MnaSystemSparseTest, UpdateRhsReusesFactors) {
  MnaSystemSparse mna(3);
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 10.0);
  ASSERT_TRUE(mna.factorize());
  const auto AT_10V = mna.solve();
  ASSERT_TRUE(AT_10V.success);

  // Same matrix, different supply -- only the RHS changes. Voltage-source
  // values affect b_, not the triplet matrix A_, so tripletsMatch() must
  // remain true (the precondition for updateRhs).
  mna.clearStamps();
  mna.addConductance(1, 2, 1.0 / 1000.0);
  mna.addConductance(2, 0, 1.0 / 1000.0);
  mna.addVoltageSource(1, 0, 20.0);
  ASSERT_TRUE(mna.tripletsMatch());
  mna.updateRhs();
  EXPECT_TRUE(mna.isFactorized());

  const auto AT_20V = mna.solve();
  ASSERT_TRUE(AT_20V.success);

  // Linear: doubling the supply doubles every node voltage.
  for (std::size_t i = 0; i < AT_10V.nodeVoltages.size(); ++i) {
    EXPECT_NEAR(AT_20V.nodeVoltages[i], 2.0 * AT_10V.nodeVoltages[i], TOL);
  }
}

/* ----------------------------- Factor Reuse ----------------------------- */

namespace {

// Circuits of four nets (0 is ground) with 10 V held on net 1. Ladders A and
// C stamp sixteen conductance entries each, at different positions; divider B
// stamps twelve.

/** @brief R(1,2), R(2,0), R(2,3), R(3,0) at 1 kohm: net 2 at 4 V, net 3 at 2 V. */
template <class System> void stampLadderA(System& mna) {
  mna.addConductance(1, 2, 1e-3);
  mna.addConductance(2, 0, 1e-3);
  mna.addConductance(2, 3, 1e-3);
  mna.addConductance(3, 0, 1e-3);
  mna.addVoltageSource(1, 0, 10.0);
}

/** @brief R(1,3) = 1 kohm over R(3,0) = 3 kohm, R(2,0) = 2 kohm alone: net 3 at 7.5 V, net 2 at 0
 * V. */
template <class System> void stampDividerB(System& mna) {
  mna.addConductance(1, 3, 1e-3);
  mna.addConductance(3, 0, 1.0 / 3000.0);
  mna.addConductance(2, 0, 0.5e-3);
  mna.addVoltageSource(1, 0, 10.0);
}

/** @brief R(1,3), R(3,0), R(3,2) at 1 kohm, R(2,0) = 0.5 kohm: net 3 at 3.75 V, net 2 at 1.25 V. */
template <class System> void stampLadderC(System& mna) {
  mna.addConductance(1, 3, 1e-3);
  mna.addConductance(3, 0, 1e-3);
  mna.addConductance(3, 2, 1e-3);
  mna.addConductance(2, 0, 2e-3);
  mna.addVoltageSource(1, 0, 10.0);
}

constexpr double SWITCH_CLOSED_SIEMENS = 1.0;
constexpr double SWITCH_OPEN_SIEMENS = 1e-9;

// Net 2 of the switched ladder sees 1 mS to the source, the switch to ground
// and 0.5 mS to ground through R(2,3) and R(3,0) in series.
constexpr double SWITCH_CLOSED_VOLTS = 10.0 * 1e-3 / (1e-3 + SWITCH_CLOSED_SIEMENS + 0.5e-3);
constexpr double SWITCH_OPEN_VOLTS = 10.0 * 1e-3 / (1e-3 + SWITCH_OPEN_SIEMENS + 0.5e-3);

/** @brief Ladder A with R(2,0) replaced by a switch: 1 S closed, 1 nS open. */
template <class System> void stampSwitched(System& mna, bool closed) {
  mna.addConductance(1, 2, 1e-3);
  mna.addConductance(2, 0, closed ? SWITCH_CLOSED_SIEMENS : SWITCH_OPEN_SIEMENS);
  mna.addConductance(2, 3, 1e-3);
  mna.addConductance(3, 0, 1e-3);
  mna.addVoltageSource(1, 0, 10.0);
}

/**
 * @brief Expects `voltages` to be what a new sparse system and the dense
 *        system give for the same stamps.
 */
template <class Stamp>
void expectMatchesFreshAndDense(const std::vector<double>& voltages, Stamp stamp) {
  MnaSystemSparse fresh(4);
  stamp(fresh);
  ASSERT_TRUE(fresh.factorize());
  const auto FRESH = fresh.solve();
  ASSERT_TRUE(FRESH.success) << FRESH.errorMessage;

  MnaSystem dense(4);
  stamp(dense);
  const auto DENSE = dense.solve();
  ASSERT_TRUE(DENSE.success) << DENSE.errorMessage;

  ASSERT_EQ(voltages.size(), 4U);
  ASSERT_EQ(FRESH.nodeVoltages.size(), 4U);
  ASSERT_EQ(DENSE.nodeVoltages.size(), 4U);
  for (std::size_t net = 0; net < 4; ++net) {
    EXPECT_NEAR(voltages[net], FRESH.nodeVoltages[net], TOL) << "net " << net << ", new sparse";
    EXPECT_NEAR(voltages[net], DENSE.nodeVoltages[net], TOL) << "net " << net << ", dense";
  }
}

} // namespace

/** @test factorize() after clear() and a different circuit solves that circuit. */
TEST(MnaSystemSparseTest, FactorizeAfterTopologyChangeMatchesFresh) {
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  EXPECT_FALSE(mna.isPatternAnalyzed());
  ASSERT_TRUE(mna.factorize());
  EXPECT_TRUE(mna.isPatternAnalyzed());

  mna.clear();
  stampDividerB(mna);
  ASSERT_TRUE(mna.factorize());
  const auto RESULT = mna.solve();
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;

  EXPECT_NEAR(RESULT.nodeVoltages[2], 0.0, TOL);
  EXPECT_NEAR(RESULT.nodeVoltages[3], 7.5, TOL);
  expectMatchesFreshAndDense(RESULT.nodeVoltages, [](auto& system) { stampDividerB(system); });
}

/** @test factorize() with as many entries at other positions solves the new matrix. */
TEST(MnaSystemSparseTest, FactorizeWithEntriesMovedDoesNotReuseFactors) {
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());
  const std::size_t ENTRIES = mna.nnz();

  mna.clear();
  stampLadderC(mna);
  ASSERT_EQ(mna.nnz(), ENTRIES);
  ASSERT_FALSE(mna.tripletsMatch());
  ASSERT_TRUE(mna.factorize());

  std::vector<double> voltages(4);
  std::vector<double> currents(1);
  ASSERT_TRUE(mna.solveInto(voltages.data(), currents.data()));

  EXPECT_NEAR(voltages[2], 1.25, TOL);
  EXPECT_NEAR(voltages[3], 3.75, TOL);
  expectMatchesFreshAndDense(voltages, [](auto& system) { stampLadderC(system); });
}

/** @test factorize() after a voltage source was added solves the larger system. */
TEST(MnaSystemSparseTest, FactorizeAfterVoltageSourceAddedSolvesNewSystem) {
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());

  // Net 3 is now held at 1 V: net 2 sits at (10 + 1) / 3 V.
  const auto WITH_SECOND_SOURCE = [](auto& system) {
    stampLadderA(system);
    system.addVoltageSource(3, 0, 1.0);
  };
  mna.clearStamps();
  WITH_SECOND_SOURCE(mna);
  ASSERT_TRUE(mna.factorize());
  const auto RESULT = mna.solve();
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;

  EXPECT_NEAR(RESULT.nodeVoltages[2], 11.0 / 3.0, TOL);
  EXPECT_NEAR(RESULT.nodeVoltages[3], 1.0, TOL);
  expectMatchesFreshAndDense(RESULT.nodeVoltages, WITH_SECOND_SOURCE);
}

/**
 * @test tripletsMatch() follows the factors in hand through a factorization
 *       that kept the pivot order, across nine decades in one conductance.
 */
TEST(MnaSystemSparseTest, TripletsMatchFollowsTheFactorsAfterAKeptOrder) {
  MnaSystemSparse mna(4);
  stampSwitched(mna, true);
  ASSERT_TRUE(mna.factorize());
  ASSERT_TRUE(mna.tripletsMatch());

  // Opened: same pattern, so the pivot order of the closed switch is kept.
  mna.clearStamps();
  stampSwitched(mna, false);
  ASSERT_FALSE(mna.tripletsMatch());
  ASSERT_TRUE(mna.factorize());
  const auto OPEN = mna.solve();
  ASSERT_TRUE(OPEN.success) << OPEN.errorMessage;
  EXPECT_NEAR(OPEN.nodeVoltages[2], SWITCH_OPEN_VOLTS, TOL);
  expectMatchesFreshAndDense(OPEN.nodeVoltages, [](auto& system) { stampSwitched(system, false); });

  // Open again: the factors in hand are the open switch's, and a new
  // right-hand side is all it takes.
  mna.clearStamps();
  stampSwitched(mna, false);
  ASSERT_TRUE(mna.tripletsMatch());
  mna.updateRhs();
  const auto OPEN_AGAIN = mna.solve();
  ASSERT_TRUE(OPEN_AGAIN.success) << OPEN_AGAIN.errorMessage;
  EXPECT_NEAR(OPEN_AGAIN.nodeVoltages[2], SWITCH_OPEN_VOLTS, TOL);

  // Closed again. The stamps are those of the first factorization; the
  // factors in hand are the open switch's. The choice below is a caller's:
  // a new right-hand side alone for an unchanged matrix, new factors otherwise.
  mna.clearStamps();
  stampSwitched(mna, true);
  EXPECT_FALSE(mna.tripletsMatch());
  if (mna.tripletsMatch()) {
    mna.updateRhs();
  } else {
    ASSERT_TRUE(mna.factorize());
  }
  const auto CLOSED = mna.solve();
  ASSERT_TRUE(CLOSED.success) << CLOSED.errorMessage;
  EXPECT_NEAR(CLOSED.nodeVoltages[2], SWITCH_CLOSED_VOLTS, TOL);
  expectMatchesFreshAndDense(CLOSED.nodeVoltages,
                             [](auto& system) { stampSwitched(system, true); });
}

/** @test Factorizing the same matrix again leaves it matched, with a new right-hand side solved */
TEST(MnaSystemSparseTest, FactorizingTheSameMatrixAgainKeepsTheMatch) {
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());
  mna.clearStamps();
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());

  // Three more passes with other supplies: the matrix is the factorized one
  // each time, and net 2 sits at four tenths of the supply.
  for (const double SUPPLY : {10.0, 20.0, -5.0}) {
    mna.clearStamps();
    mna.addConductance(1, 2, 1e-3);
    mna.addConductance(2, 0, 1e-3);
    mna.addConductance(2, 3, 1e-3);
    mna.addConductance(3, 0, 1e-3);
    mna.addVoltageSource(1, 0, SUPPLY);
    ASSERT_TRUE(mna.tripletsMatch()) << "supply " << SUPPLY;
    mna.updateRhs();
    const auto RESULT = mna.solve();
    ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
    EXPECT_NEAR(RESULT.nodeVoltages[2], 0.4 * SUPPLY, TOL);
  }
}

/**
 * @test A stamp added after the factorization is not part of the factorized
 *       matrix, and a pass that leaves a stamp out is not that matrix.
 */
TEST(MnaSystemSparseTest, StampAddedAfterFactorizationIsNotMatched) {
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());
  ASSERT_TRUE(mna.tripletsMatch());

  mna.addConductance(3, 0, 1e-3);
  EXPECT_FALSE(mna.tripletsMatch());

  // Cleared and stamped as it was factorized, it matches again: what was
  // added later was not kept with the factors' stamps.
  mna.clearStamps();
  stampLadderA(mna);
  EXPECT_TRUE(mna.tripletsMatch());
  mna.clearStamps();
  stampLadderA(mna);
  mna.addConductance(3, 0, 1e-3);
  EXPECT_FALSE(mna.tripletsMatch());

  // The ladder's first three conductances and its source: every stamp of
  // this pass equals the factorized one at its place, and one is missing.
  mna.clearStamps();
  mna.addConductance(1, 2, 1e-3);
  mna.addConductance(2, 0, 1e-3);
  mna.addConductance(2, 3, 1e-3);
  mna.addVoltageSource(1, 0, 10.0);
  EXPECT_FALSE(mna.tripletsMatch());
}

/**
 * @test A kept pivot order under which U grows past the floor is given up,
 *       though no pivot is zero; one above the floor is kept.
 *
 * [[1, 1], [1, 2]] is pivoted on its diagonal. [[e, 1], [1, 1]] at the same
 * positions is well conditioned (about 4) for any small e, with the solution
 * v1 = 1 / (1 - e), v2 = (1 - 2e) / (1 - e) for the right-hand side [1, 2].
 * On the kept order its first pivot is e and U's last entry is 1 - 1/e, a
 * reciprocal growth of about e. With e = 1e-20 that order returns
 * v = [0, 1]. e = 1e-20 and e = 1e-9 are below the floor and are pivoted
 * anew: both are held to 1e-12. e = 1e-6 is above it and keeps its order:
 * it is held to 1e-8, which is what this case is asked for, not what the
 * floor promises of any other matrix.
 */
TEST(MnaSystemSparseTest, FactorizeRepivotsWhenKeptOrderGrowsItsPivots) {
  const auto STAMP = [](auto& system, double first, double last) {
    system.addMatrixEntry(1, 1, first);
    system.addMatrixEntry(1, 2, 1.0);
    system.addMatrixEntry(2, 1, 1.0);
    system.addMatrixEntry(2, 2, last);
    system.addCurrent(1, 0, 1.0);
    system.addCurrent(2, 0, 2.0);
  };
  struct Case {
    double first;
    double tolerance;
  };
  for (const Case CASE : {Case{1e-20, 1e-12}, Case{1e-9, 1e-12}, Case{1e-6, 1e-8}}) {
    SCOPED_TRACE(CASE.first);
    MnaSystemSparse mna(3);
    STAMP(mna, 1.0, 2.0);
    ASSERT_TRUE(mna.factorize());

    mna.clearStamps();
    STAMP(mna, CASE.first, 1.0);
    ASSERT_TRUE(mna.factorize());
    const auto RESULT = mna.solve();
    ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
    EXPECT_NEAR(RESULT.nodeVoltages[1], 1.0 / (1.0 - CASE.first), CASE.tolerance);
    EXPECT_NEAR(RESULT.nodeVoltages[2], (1.0 - 2.0 * CASE.first) / (1.0 - CASE.first),
                CASE.tolerance);

    // The residual of each row, and a system that never held other factors.
    EXPECT_NEAR(CASE.first * RESULT.nodeVoltages[1] + RESULT.nodeVoltages[2], 1.0, CASE.tolerance);
    EXPECT_NEAR(RESULT.nodeVoltages[1] + RESULT.nodeVoltages[2], 2.0, CASE.tolerance);
    MnaSystemSparse fresh(3);
    STAMP(fresh, CASE.first, 1.0);
    ASSERT_TRUE(fresh.factorize());
    const auto FRESH = fresh.solve();
    ASSERT_TRUE(FRESH.success);
    EXPECT_NEAR(RESULT.nodeVoltages[1], FRESH.nodeVoltages[1], CASE.tolerance);
    EXPECT_NEAR(RESULT.nodeVoltages[2], FRESH.nodeVoltages[2], CASE.tolerance);
  }
}

/** @test The same conductances with the voltage source on another net are another matrix. */
TEST(MnaSystemSparseTest, TripletsMatchFalseWhenVoltageSourceMoved) {
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());

  // 10 V on net 3 in place of net 1. Net 1 then hangs on net 2 alone, and
  // net 2 sits halfway between net 3 and ground.
  const auto SOURCE_ON_NET_3 = [](auto& system) {
    system.addConductance(1, 2, 1e-3);
    system.addConductance(2, 0, 1e-3);
    system.addConductance(2, 3, 1e-3);
    system.addConductance(3, 0, 1e-3);
    system.addVoltageSource(3, 0, 10.0);
  };
  mna.clearStamps();
  SOURCE_ON_NET_3(mna);
  EXPECT_FALSE(mna.tripletsMatch());
  if (mna.tripletsMatch()) {
    mna.updateRhs();
  } else {
    ASSERT_TRUE(mna.factorize());
  }
  const auto RESULT = mna.solve();
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;

  EXPECT_NEAR(RESULT.nodeVoltages[1], 5.0, TOL);
  EXPECT_NEAR(RESULT.nodeVoltages[2], 5.0, TOL);
  EXPECT_NEAR(RESULT.nodeVoltages[3], 10.0, TOL);
  expectMatchesFreshAndDense(RESULT.nodeVoltages, SOURCE_ON_NET_3);
}

/** @test The same conductances with one more voltage source, or one fewer, are another matrix. */
TEST(MnaSystemSparseTest, TripletsMatchFalseWhenVoltageSourceAddedOrRemoved) {
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());

  mna.clearStamps();
  stampLadderA(mna);
  mna.addVoltageSource(3, 0, 1.0);
  EXPECT_FALSE(mna.tripletsMatch());
  ASSERT_TRUE(mna.factorize());
  ASSERT_TRUE(mna.tripletsMatch());

  mna.clearStamps();
  stampLadderA(mna);
  EXPECT_FALSE(mna.tripletsMatch());
}

/** @test A pivot that is zero for the new values under the kept pivot order is pivoted anew. */
TEST(MnaSystemSparseTest, FactorizePivotsAnewWhenKeptOrderMeetsZeroPivot) {
  // Nets 1 and 2 with all four entries stamped. The first matrix is
  // diagonally dominant, so its pivots are the diagonal entries.
  const auto STAMP = [](MnaSystemSparse& system, double diagonal, double offDiagonal) {
    system.addMatrixEntry(1, 1, diagonal);
    system.addMatrixEntry(1, 2, offDiagonal);
    system.addMatrixEntry(2, 1, offDiagonal);
    system.addMatrixEntry(2, 2, diagonal);
    system.addCurrent(1, 0, 3.0);
    system.addCurrent(2, 0, 5.0);
  };
  MnaSystemSparse mna(3);
  STAMP(mna, 1.0, 0.5);
  ASSERT_TRUE(mna.factorize());

  // Same pattern with a zero diagonal: v2 = 3 and v1 = 5, reached only by
  // exchanging the rows.
  mna.clearStamps();
  STAMP(mna, 0.0, 1.0);
  ASSERT_TRUE(mna.factorize());
  EXPECT_TRUE(mna.tripletsMatch());
  const auto RESULT = mna.solve();
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
  EXPECT_NEAR(RESULT.nodeVoltages[1], 5.0, TOL);
  EXPECT_NEAR(RESULT.nodeVoltages[2], 3.0, TOL);
}

/** @test Singular values under the factorized pattern are refused, and the system no longer reads
 * as factorized. */
TEST(MnaSystemSparseTest, FactorizeRefusesSingularValuesUnderSamePattern) {
  const auto STAMP = [](MnaSystemSparse& system, double diagonal, double offDiagonal) {
    system.addMatrixEntry(1, 1, diagonal);
    system.addMatrixEntry(1, 2, offDiagonal);
    system.addMatrixEntry(2, 1, offDiagonal);
    system.addMatrixEntry(2, 2, diagonal);
    system.addCurrent(1, 0, 3.0);
    system.addCurrent(2, 0, 5.0);
  };
  MnaSystemSparse mna(3);
  STAMP(mna, 1.0, 0.5);
  ASSERT_TRUE(mna.factorize());

  // Two equal rows: no pivot order factorizes it.
  mna.clearStamps();
  STAMP(mna, 1.0, 1.0);
  ASSERT_TRUE(mna.isFactorized());
  EXPECT_FALSE(mna.factorize());
  EXPECT_FALSE(mna.isFactorized());
  EXPECT_FALSE(mna.tripletsMatch());
  EXPECT_FALSE(mna.solve().success);

  // The first matrix again: its factors went with the refused
  // factorization, so its stamps match nothing.
  mna.clearStamps();
  STAMP(mna, 1.0, 0.5);
  EXPECT_FALSE(mna.tripletsMatch());
  ASSERT_TRUE(mna.factorize());
  const auto RESULT = mna.solve();
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
  // [1 0.5; 0.5 1] v = [3; 5]: v1 = 2/3, v2 = 14/3.
  EXPECT_NEAR(RESULT.nodeVoltages[1], 2.0 / 3.0, TOL);
  EXPECT_NEAR(RESULT.nodeVoltages[2], 14.0 / 3.0, TOL);
}

/** @test A floating net is refused, new or after a factorization, and the next circuit solves. */
TEST(MnaSystemSparseTest, FactorizeRefusesFloatingNetAndRecovers) {
  // Net 3 has no entry at all.
  const auto NET_3_FLOATING = [](MnaSystemSparse& system) {
    system.addConductance(1, 2, 1e-3);
    system.addConductance(2, 0, 1e-3);
    system.addVoltageSource(1, 0, 10.0);
  };
  MnaSystemSparse fresh(4);
  NET_3_FLOATING(fresh);
  EXPECT_FALSE(fresh.factorize());
  EXPECT_FALSE(fresh.isFactorized());

  // clearStamps() leaves the system reading as factorized until the refusal.
  MnaSystemSparse mna(4);
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());
  mna.clearStamps();
  NET_3_FLOATING(mna);
  ASSERT_TRUE(mna.isFactorized());
  EXPECT_FALSE(mna.factorize());
  EXPECT_FALSE(mna.isFactorized());
  EXPECT_FALSE(mna.tripletsMatch());
  EXPECT_FALSE(mna.solve().success);

  mna.clear();
  stampLadderA(mna);
  ASSERT_TRUE(mna.factorize());
  const auto RESULT = mna.solve();
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
  EXPECT_NEAR(RESULT.nodeVoltages[2], 4.0, TOL);
  EXPECT_NEAR(RESULT.nodeVoltages[3], 2.0, TOL);
}
