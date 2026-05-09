/**
 * @file MnaSystemSparse_uTest.cpp
 * @brief Unit tests for sparse MNA solver.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

#include <gtest/gtest.h>

#include <cmath>

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
 * @test MnaSystemSparse_ClearAndReuse_ProducesSameResult
 * @brief Clear stamps, rebuild circuit, solve again.
 */
TEST(MnaSystemSparseTest, ClearAndReuse_ProducesSameResult) {
  MnaSystemSparse mna(3);

  mna.addConductance(0, 1, 1.0 / 10.0);
  mna.addConductance(1, 2, 1.0 / 20.0);
  mna.addVoltageSource(2, 0, 10.0);
  ASSERT_TRUE(mna.factorize());
  auto result1 = mna.solve();
  ASSERT_TRUE(result1.success);

  // Clear and rebuild same circuit
  mna.clear();
  mna.addConductance(0, 1, 1.0 / 10.0);
  mna.addConductance(1, 2, 1.0 / 20.0);
  mna.addVoltageSource(2, 0, 10.0);
  ASSERT_TRUE(mna.factorize());
  auto result2 = mna.solve();
  ASSERT_TRUE(result2.success);

  // Results should be identical
  ASSERT_EQ(result1.nodeVoltages.size(), result2.nodeVoltages.size());
  for (std::size_t i = 0; i < result1.nodeVoltages.size(); ++i) {
    EXPECT_NEAR(result1.nodeVoltages[i], result2.nodeVoltages[i], TOL);
  }
}

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
