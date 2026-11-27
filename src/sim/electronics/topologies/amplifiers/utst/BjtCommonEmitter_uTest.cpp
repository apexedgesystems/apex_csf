/**
 * @file BjtCommonEmitter_uTest.cpp
 * @brief Unit tests for BJT common-emitter amplifier circuit model.
 *
 * Notes:
 *  - Reference operating points come from bisection on the two node
 *    equations of the stage, with the transistor in the transport form of the
 *    Ebers-Moll equations, evaluated outside this library.
 */

#include "src/sim/electronics/topologies/amplifiers/inc/BjtCommonEmitter.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;
using sim::electronics::topologies::amplifiers::BjtCommonEmitter;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Before a DC solve the stage reports its bias network and a zeroed operating point */
TEST(BjtCommonEmitterDefaultTest, ReportsBiasNetworkAndZeroedOperatingPoint) {
  const BjtCommonEmitter AMP(12.0, 1e3, 100e3);

  EXPECT_DOUBLE_EQ(AMP.vcc(), 12.0);
  EXPECT_DOUBLE_EQ(AMP.rc(), 1e3);
  EXPECT_DOUBLE_EQ(AMP.rb(), 100e3);
  EXPECT_DOUBLE_EQ(AMP.collectorVoltage(), 0.0);
  EXPECT_DOUBLE_EQ(AMP.baseVoltage(), 0.0);
  EXPECT_DOUBLE_EQ(AMP.collectorCurrent(), 0.0);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test DC solve reaches the reference operating point in forward-active and saturated bias */
TEST(BjtCommonEmitterTest, ComputeDcMatchesReferenceOperatingPoint) {
  // Default transistor: Is = 1e-14 A, Bf = 100, Br = 1, Vt = 0.026 V.
  struct Row {
    double vcc;
    double rc;
    double rb;
    double vb;
    double vc;
    double ic;
    const char* region;
  };
  const std::array<Row, 4> ROWS = {{
      // Hand check of the first row: Ib = (12 - 0.66) / 1M = 11.3 uA,
      // Ic = 100 * Ib = 1.13 mA, Vc = 12 - 1.13 = 10.87 V.
      Row{12.0, 1e3, 1e6, 0.6618047297996406, 10.86618047195996, 0.001133819528040039, "active"},
      Row{9.0, 4.7e3, 1e6, 0.653838967799425, 5.077304310071726, 0.0008346161042400583, "active"},
      Row{5.0, 1e3, 100e3, 0.6964845745632098, 0.6964845745632124, 0.004303515425436787, "edge"},
      Row{12.0, 4.7e3, 100e3, 0.6844245110198739, 0.08964576506874927, 0.002534117922325798,
          "saturation"},
  }};

  for (const Row& row : ROWS) {
    BjtCommonEmitter amp(row.vcc, row.rc, row.rb);
    ASSERT_TRUE(amp.computeDC()) << row.region;

    EXPECT_NEAR(amp.baseVoltage(), row.vb, 1e-9) << row.region;
    EXPECT_NEAR(amp.collectorVoltage(), row.vc, 1e-7) << row.region;
    EXPECT_NEAR(amp.collectorCurrent(), row.ic, 1e-9 * row.ic + 1e-10) << row.region;
  }
}

/** @test After a successful DC solve, one more linear solve leaves every node where it is */
TEST(BjtCommonEmitterTest, ComputeDcSuccessImpliesFixedPoint) {
  BjtCommonEmitter amp(12.0, 1e3, 1e6);
  ASSERT_TRUE(amp.computeDC());

  // Copied before the solve: a DC solve overwrites the solver's previous
  // voltages with its own solution.
  const std::vector<double> LINEARIZED_AT = amp.circuit().solver().prevVoltages();

  TransientState state;
  ASSERT_EQ(amp.circuit().computeDC(state), TransientStatus::SUCCESS);
  ASSERT_EQ(state.nodeVoltages.size(), LINEARIZED_AT.size());
  for (std::size_t net = 0; net < LINEARIZED_AT.size(); ++net) {
    EXPECT_NEAR(state.nodeVoltages[net], LINEARIZED_AT[net], 1e-6) << "net " << net;
  }
}

/**
 * @test A DC solve that fails returns false and leaves the operating point of the last
 * solve that succeeded: a stage is solved, a current that is not a number is then stamped
 * into its base through its public circuit, and the second solve fails with the first
 * solve's three values still reported. A stage that has never been solved reports zeros.
 */
TEST(BjtCommonEmitterTest, ComputeDcFailureKeepsCachedOperatingPoint) {
  BjtCommonEmitter amp(12.0, 1e3, 1e6);
  ASSERT_TRUE(amp.computeDC());
  const double VB = amp.baseVoltage();
  const double VC = amp.collectorVoltage();
  const double IC = amp.collectorCurrent();
  ASSERT_GT(VB, 0.6);
  ASSERT_GT(VC, 10.0);
  ASSERT_GT(IC, 1e-3);

  amp.circuit().addStamp([](auto& mna, double, const std::vector<double>&) {
    mna.addCurrent(1, 0, std::numeric_limits<double>::quiet_NaN());
  });

  EXPECT_FALSE(amp.computeDC());
  EXPECT_DOUBLE_EQ(amp.baseVoltage(), VB);
  EXPECT_DOUBLE_EQ(amp.collectorVoltage(), VC);
  EXPECT_DOUBLE_EQ(amp.collectorCurrent(), IC);

  // A supply that is not a number has no operating point to converge to.
  BjtCommonEmitter unsolved(std::numeric_limits<double>::quiet_NaN(), 1e3, 1e6);
  EXPECT_FALSE(unsolved.computeDC());
  EXPECT_DOUBLE_EQ(unsolved.collectorVoltage(), 0.0);
  EXPECT_DOUBLE_EQ(unsolved.baseVoltage(), 0.0);
  EXPECT_DOUBLE_EQ(unsolved.collectorCurrent(), 0.0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Two DC solves of one stage give the same operating point */
TEST(BjtCommonEmitterDeterminismTest, ComputeDcIsRepeatable) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);
  ASSERT_TRUE(amp.computeDC());
  const double VC = amp.collectorVoltage();
  const double VB = amp.baseVoltage();
  const double IC = amp.collectorCurrent();

  ASSERT_TRUE(amp.computeDC());
  EXPECT_DOUBLE_EQ(amp.collectorVoltage(), VC);
  EXPECT_DOUBLE_EQ(amp.baseVoltage(), VB);
  EXPECT_DOUBLE_EQ(amp.collectorCurrent(), IC);
}
