/**
 * @file StampContext_uTest.cpp
 * @brief Unit tests for the StampContext stamping helper.
 *
 * Covers the three construction modes (internal G_, external LAPACK
 * column-major matrix, sparse matvec), the stamping API
 * (stampConductance, stampGEntry, stampCurrent, stampVoltageSource),
 * mutation helpers (clear, clearCurrents, clearRHS,
 * updateVoltageSourceValue), and accessors.
 *
 * Tests are platform-agnostic: they assert structural invariants of the
 * stamped matrix/vector (KCL-consistent values at specific positions,
 * voltage-source vector contents) rather than full circuit behavior.
 */

#include "src/sim/electronics/algorithms/mna/inc/StampContext.hpp"

#include <gtest/gtest.h>

#include <vector>

using sim::electronics::algorithms::mna::NetID;
using sim::electronics::algorithms::mna::StampContext;

constexpr double STAMP_TOL = 1e-12;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed context records the net count and zeros G + I */
TEST(StampContextTest, ConstructionInitializesZero) {
  const StampContext CTX(4);
  EXPECT_EQ(CTX.netCount(), 4u);
  EXPECT_EQ(CTX.g().size(), 16u);
  EXPECT_EQ(CTX.i().size(), 4u);
  EXPECT_TRUE(CTX.voltageSources().empty());
  EXPECT_FALSE(CTX.hasExternalMatrix());
}

/* ----------------------------- Stamping API ----------------------------- */

/** @test stampConductance writes the symmetric resistor stamp into G */
TEST(StampContextTest, StampConductanceFormsSymmetricStamp) {
  StampContext ctx(3);
  ctx.stampConductance(/*a=*/1, /*b=*/2, /*g=*/0.5);

  // Symmetric 2x2 block: G[a,a]+=g, G[b,b]+=g, G[a,b]-=g, G[b,a]-=g
  const auto& G = ctx.g();
  EXPECT_NEAR(G[1 * 3 + 1], 0.5, STAMP_TOL);
  EXPECT_NEAR(G[2 * 3 + 2], 0.5, STAMP_TOL);
  EXPECT_NEAR(G[1 * 3 + 2], -0.5, STAMP_TOL);
  EXPECT_NEAR(G[2 * 3 + 1], -0.5, STAMP_TOL);
}

/** @test stampConductance with a == b accumulates only the diagonal */
TEST(StampContextTest, StampConductanceSelfLoopHitsDiagonal) {
  StampContext ctx(2);
  ctx.stampConductance(1, 1, 0.25);
  EXPECT_NEAR(ctx.g()[1 * 2 + 1], 0.25, STAMP_TOL);
  // The off-diagonal must remain zero.
  EXPECT_NEAR(ctx.g()[1 * 2 + 0], 0.0, STAMP_TOL);
}

/** @test stampGEntry writes the asymmetric VCCS contribution into G[row,col] */
TEST(StampContextTest, StampGEntryWritesAsymmetricContribution) {
  StampContext ctx(3);
  ctx.stampGEntry(/*row=*/2, /*col=*/1, /*value=*/0.7);
  EXPECT_NEAR(ctx.g()[2 * 3 + 1], 0.7, STAMP_TOL);
  // Reflected position must remain zero (no symmetric counterpart).
  EXPECT_NEAR(ctx.g()[1 * 3 + 2], 0.0, STAMP_TOL);
}

/** @test stampCurrent updates I with +i at a and -i at b */
TEST(StampContextTest, StampCurrentSplitsBetweenNets) {
  StampContext ctx(3);
  ctx.stampCurrent(/*a=*/1, /*b=*/2, /*i=*/1e-3);
  EXPECT_NEAR(ctx.i()[1], 1e-3, STAMP_TOL);
  EXPECT_NEAR(ctx.i()[2], -1e-3, STAMP_TOL);
  EXPECT_NEAR(ctx.i()[0], 0.0, STAMP_TOL);
}

/** @test stampVoltageSource records the source descriptor */
TEST(StampContextTest, StampVoltageSourceRecordsDescriptor) {
  StampContext ctx(3);
  ctx.stampVoltageSource(/*pos=*/1, /*neg=*/0, /*v=*/5.0);
  ASSERT_EQ(ctx.voltageSources().size(), 1u);
  EXPECT_EQ(ctx.voltageSources()[0].pos, 1u);
  EXPECT_EQ(ctx.voltageSources()[0].neg, 0u);
  EXPECT_NEAR(ctx.voltageSources()[0].v, 5.0, STAMP_TOL);
}

/* ----------------------------- Mutation helpers ----------------------------- */

/** @test clear() zeros G, I, and the voltage-source list */
TEST(StampContextTest, ClearResetsAllState) {
  StampContext ctx(3);
  ctx.stampConductance(1, 2, 0.5);
  ctx.stampCurrent(1, 0, 1e-3);
  ctx.stampVoltageSource(2, 0, 1.0);

  ctx.clear();
  for (const double V : ctx.g())
    EXPECT_NEAR(V, 0.0, STAMP_TOL);
  for (const double V : ctx.i())
    EXPECT_NEAR(V, 0.0, STAMP_TOL);
  EXPECT_TRUE(ctx.voltageSources().empty());
}

/** @test clearCurrents() zeros I but leaves G and sources intact */
TEST(StampContextTest, ClearCurrentsLeavesMatrixIntact) {
  StampContext ctx(3);
  ctx.stampConductance(1, 2, 0.5);
  ctx.stampCurrent(1, 0, 1e-3);

  ctx.clearCurrents();
  EXPECT_NEAR(ctx.i()[1], 0.0, STAMP_TOL);
  EXPECT_NEAR(ctx.g()[1 * 3 + 1], 0.5, STAMP_TOL);
}

/** @test clearRHS() zeros I and clears voltage sources but keeps G */
TEST(StampContextTest, ClearRhsZeroesCurrentsAndDropsSources) {
  StampContext ctx(3);
  ctx.stampConductance(1, 2, 0.5);
  ctx.stampCurrent(1, 0, 1e-3);
  ctx.stampVoltageSource(2, 0, 1.0);

  ctx.clearRHS();
  EXPECT_NEAR(ctx.i()[1], 0.0, STAMP_TOL);
  EXPECT_TRUE(ctx.voltageSources().empty());
  EXPECT_NEAR(ctx.g()[1 * 3 + 1], 0.5, STAMP_TOL);
}

/** @test updateVoltageSourceValue() rewrites the v field at a valid index */
TEST(StampContextTest, UpdateVoltageSourceValueAtValidIndex) {
  StampContext ctx(3);
  ctx.stampVoltageSource(1, 0, 5.0);
  ctx.stampVoltageSource(2, 0, 10.0);

  ctx.updateVoltageSourceValue(0, 7.5);
  EXPECT_NEAR(ctx.voltageSources()[0].v, 7.5, STAMP_TOL);
  // Other slots untouched.
  EXPECT_NEAR(ctx.voltageSources()[1].v, 10.0, STAMP_TOL);
}

/** @test updateVoltageSourceValue() is a no-op when the index is out of range */
TEST(StampContextTest, UpdateVoltageSourceValueOutOfRangeIsNoOp) {
  StampContext ctx(3);
  ctx.stampVoltageSource(1, 0, 5.0);

  ctx.updateVoltageSourceValue(42, 99.0);
  EXPECT_NEAR(ctx.voltageSources()[0].v, 5.0, STAMP_TOL);
}

/* ----------------------------- External-matrix mode ----------------------------- */

/** @test External-matrix constructor reports hasExternalMatrix and externalDim */
TEST(StampContextTest, ExternalMatrixConstructorAdvertisesMode) {
  std::vector<double> A(16, 0.0);
  StampContext ctx(3, A.data(), /*augDim=*/4);
  EXPECT_TRUE(ctx.hasExternalMatrix());
  EXPECT_EQ(ctx.externalDim(), 4u);
}

/** @test stampConductance writes column-major into the external matrix */
TEST(StampContextTest, StampConductanceWritesIntoExternalMatrixColumnMajor) {
  std::vector<double> A(4 * 4, 0.0);
  StampContext ctx(3, A.data(), 4);
  ctx.stampConductance(/*a=*/1, /*b=*/2, /*g=*/0.5);

  // Column-major: A[row, col] -> A[col * dim + row]
  EXPECT_NEAR(A[1 * 4 + 1], 0.5, STAMP_TOL);  // (1,1)
  EXPECT_NEAR(A[2 * 4 + 2], 0.5, STAMP_TOL);  // (2,2)
  EXPECT_NEAR(A[2 * 4 + 1], -0.5, STAMP_TOL); // (1,2): col=2, row=1
  EXPECT_NEAR(A[1 * 4 + 2], -0.5, STAMP_TOL); // (2,1): col=1, row=2
}

/* ----------------------------- Matvec mode ----------------------------- */

/** @test Matvec-mode stampConductance accumulates g*(x[a]-x[b]) into ax */
TEST(StampContextTest, StampConductanceMatvecAccumulatesIntoOutput) {
  std::vector<double> x{0.0, 4.0, 2.0};
  std::vector<double> ax(3, 0.0);
  StampContext ctx(3, x.data(), ax.data());

  ctx.stampConductance(/*a=*/1, /*b=*/2, /*g=*/0.25);
  // ax[1] += 0.25 * (4 - 2) = 0.5; ax[2] -= 0.5
  EXPECT_NEAR(ax[1], 0.5, STAMP_TOL);
  EXPECT_NEAR(ax[2], -0.5, STAMP_TOL);
}

/** @test Matvec-mode stampGEntry accumulates value * x[col] into ax[row] */
TEST(StampContextTest, StampGEntryMatvecAccumulates) {
  std::vector<double> x{0.0, 3.0, 0.0};
  std::vector<double> ax(3, 0.0);
  StampContext ctx(3, x.data(), ax.data());

  ctx.stampGEntry(/*row=*/2, /*col=*/1, /*value=*/0.5);
  EXPECT_NEAR(ax[2], 1.5, STAMP_TOL);
}
