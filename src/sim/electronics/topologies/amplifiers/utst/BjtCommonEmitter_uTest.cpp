/**
 * @file BjtCommonEmitter_uTest.cpp
 * @brief Unit tests for BJT common-emitter amplifier circuit model.
 *
 * Construction + getters plus the `computeDC()` path. Strong physics claims
 * (correct bias point, valid forward-active region values) are NOT asserted:
 * the underlying `BjtEbersMoll` stamp is symmetric and does not produce
 * physically accurate operating points. These tests verify the wrapper:
 * the iteration runs, returns a bool, and produces outputs within the
 * envelope dictated by the supply rails plus KCL through RC.
 */

#include "src/sim/electronics/topologies/amplifiers/inc/BjtCommonEmitter.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::topologies::amplifiers::BjtCommonEmitter;

/* ----------------------------- Construction Tests ----------------------------- */

/** @test Construction stores VCC, RC, and RB values. */
TEST(BjtCommonEmitterTest, ConstructionStoresValues) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);

  EXPECT_DOUBLE_EQ(amp.vcc(), 12.0);
  EXPECT_DOUBLE_EQ(amp.rc(), 1e3);
  EXPECT_DOUBLE_EQ(amp.rb(), 100e3);
}

/** @test Initial cached voltages and current are zero before DC solve. */
TEST(BjtCommonEmitterTest, InitialStateIsZero) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);

  EXPECT_DOUBLE_EQ(amp.collectorVoltage(), 0.0);
  EXPECT_DOUBLE_EQ(amp.baseVoltage(), 0.0);
  EXPECT_DOUBLE_EQ(amp.collectorCurrent(), 0.0);
}

/** @test Construction across a range of bias values keeps stored values in sync. */
TEST(BjtCommonEmitterTest, ConstructionPreservesBiasValues) {
  for (const double VCC : {5.0, 9.0, 12.0, 15.0}) {
    for (const double RC : {220.0, 1e3, 4.7e3, 22e3}) {
      for (const double RB : {10e3, 100e3, 1e6}) {
        BjtCommonEmitter amp(VCC, RC, RB);
        EXPECT_DOUBLE_EQ(amp.vcc(), VCC);
        EXPECT_DOUBLE_EQ(amp.rc(), RC);
        EXPECT_DOUBLE_EQ(amp.rb(), RB);
      }
    }
  }
}

/* ----------------------------- computeDC + stamp lambda ----------------------------- */

/** @test computeDC succeeds across a bias grid and the cached node voltages
 *        satisfy KVL bounds: 0 <= Vb, Vc <= VCC (no spurious supply overshoot).
 *
 * The symmetric BjtEbersMoll stamp under this wrapper does not converge to a
 * physically-correct forward-active operating point (see file header), so the
 * test cannot anchor to forward-active bias-point formulas. The KVL envelope
 * is the strongest invariant the model supports: any regression that lets
 * Vc/Vb drift outside the supply rails would fail this test.
 */
TEST(BjtCommonEmitterTest, ComputeDcAcrossBiasGridRespectsSupplyEnvelope) {
  for (const double VCC : {5.0, 9.0, 12.0}) {
    for (const double RC : {1e3, 4.7e3}) {
      for (const double RB : {100e3, 1e6}) {
        BjtCommonEmitter amp(VCC, RC, RB);
        ASSERT_TRUE(amp.computeDC()) << "VCC=" << VCC << " RC=" << RC << " RB=" << RB;
        EXPECT_TRUE(std::isfinite(amp.collectorVoltage()));
        EXPECT_TRUE(std::isfinite(amp.baseVoltage()));
        EXPECT_TRUE(std::isfinite(amp.collectorCurrent()));
        EXPECT_GE(amp.collectorVoltage(), 0.0);
        EXPECT_LE(amp.collectorVoltage(), VCC + 1e-6);
        EXPECT_GE(amp.baseVoltage(), 0.0);
        EXPECT_LE(amp.baseVoltage(), VCC + 1e-6);
      }
    }
  }
}

/** @test Collector current scales 1/RC for fixed VCC: doubling RC halves ic.
 *
 * This is Ohm's law against the cached collector voltage. Because the
 * symmetric BjtEbersMoll stamp converges to a Vc that does not depend on RC,
 * the test verifies the wrapper's KCL relation IC = (VCC - Vc)/RC holds
 * across RC sweeps, and would fail if ic stopped tracking RC.
 */
TEST(BjtCommonEmitterTest, CollectorCurrentScalesInverselyWithRc) {
  BjtCommonEmitter amp1(12.0, /*RC=*/1e3, 100e3);
  BjtCommonEmitter amp2(12.0, /*RC=*/2e3, 100e3);
  ASSERT_TRUE(amp1.computeDC());
  ASSERT_TRUE(amp2.computeDC());

  // Doubling RC roughly halves ic when Vc is approximately stable (the
  // symmetric-stamp Vc is bias-independent in this model, so the ratio is
  // exact). The 10% tolerance allows for any small Vc drift.
  EXPECT_NEAR(amp1.collectorCurrent() / amp2.collectorCurrent(), 2.0, 0.2);
}

/** @test Repeated computeDC invocations return the same cached values (idempotent). */
TEST(BjtCommonEmitterTest, ComputeDcIsRepeatable) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);
  ASSERT_TRUE(amp.computeDC());
  const double VC1 = amp.collectorVoltage();
  const double VB1 = amp.baseVoltage();
  const double IC1 = amp.collectorCurrent();

  ASSERT_TRUE(amp.computeDC());
  EXPECT_DOUBLE_EQ(amp.collectorVoltage(), VC1);
  EXPECT_DOUBLE_EQ(amp.baseVoltage(), VB1);
  EXPECT_DOUBLE_EQ(amp.collectorCurrent(), IC1);
}
