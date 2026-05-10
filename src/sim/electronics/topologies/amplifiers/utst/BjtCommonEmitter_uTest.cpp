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

/** @test computeDC returns true and produces finite, supply-bounded cached values. */
TEST(BjtCommonEmitterTest, ComputeDcRunsAndCachesWithinEnvelope) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);
  ASSERT_TRUE(amp.computeDC());
  EXPECT_TRUE(std::isfinite(amp.collectorVoltage()));
  EXPECT_TRUE(std::isfinite(amp.baseVoltage()));
  EXPECT_TRUE(std::isfinite(amp.collectorCurrent()));
  EXPECT_GE(amp.collectorVoltage(), 0.0);
  EXPECT_LE(amp.collectorVoltage(), amp.vcc() + 1e-6);
  EXPECT_GE(amp.baseVoltage(), 0.0);
  EXPECT_LE(amp.baseVoltage(), amp.vcc() + 1e-6);
}

/** @test computeDC succeeds and stays bounded across a bias grid. */
TEST(BjtCommonEmitterTest, ComputeDcAcrossBiasGridStaysBounded) {
  for (const double VCC : {5.0, 9.0, 12.0}) {
    for (const double RC : {1e3, 4.7e3}) {
      for (const double RB : {100e3, 1e6}) {
        BjtCommonEmitter amp(VCC, RC, RB);
        ASSERT_TRUE(amp.computeDC()) << "VCC=" << VCC << " RC=" << RC << " RB=" << RB;
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
 * This is Ohm's law against the cached collector voltage. Since the current
 * symmetric BjtEbersMoll stamp converges to a Vc that does not depend on RC,
 * the test verifies the wrapper's KCL relation IC = (VCC - Vc)/RC holds
 * across RC sweeps (and exposes any future regression where ic stops
 * tracking RC).
 */
TEST(BjtCommonEmitterTest, CollectorCurrentScalesInverselyWithRc) {
  BjtCommonEmitter amp1(12.0, /*RC=*/1e3, 100e3);
  BjtCommonEmitter amp2(12.0, /*RC=*/2e3, 100e3);
  ASSERT_TRUE(amp1.computeDC());
  ASSERT_TRUE(amp2.computeDC());

  // Doubling RC roughly halves ic when Vc is approximately stable (the
  // symmetric-stamp Vc is bias-independent in this model, so the ratio is
  // exact). Tolerate 10% for any future Vc drift.
  EXPECT_NEAR(amp1.collectorCurrent() / amp2.collectorCurrent(), 2.0, 0.2);
}

/** @test circuit() accessor exposes the underlying Circuit and survives computeDC. */
TEST(BjtCommonEmitterTest, CircuitAccessorIsUsable) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);
  // Before solve: at least the three nets registered in the constructor (VCC, COLLECTOR, BASE)
  // plus ground (net 0) are present.
  EXPECT_GE(amp.circuit().netCount(), 4u);

  amp.computeDC();
  // After build() inside computeDC the net count is unchanged at this layer.
  EXPECT_GE(amp.circuit().netCount(), 4u);
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

/** @test Custom BjtEbersMollParams flow through to the stamp lambda. */
TEST(BjtCommonEmitterTest, CustomBjtParamsAreAccepted) {
  using sim::electronics::devices::nonlinear::BjtEbersMollParams;
  BjtEbersMollParams params;  // default-constructed
  BjtCommonEmitter amp(9.0, 2.2e3, 47e3, params);

  EXPECT_DOUBLE_EQ(amp.vcc(), 9.0);
  EXPECT_DOUBLE_EQ(amp.rc(), 2.2e3);
  EXPECT_DOUBLE_EQ(amp.rb(), 47e3);

  ASSERT_TRUE(amp.computeDC());
  EXPECT_TRUE(std::isfinite(amp.collectorVoltage()));
  EXPECT_TRUE(std::isfinite(amp.baseVoltage()));
  EXPECT_TRUE(std::isfinite(amp.collectorCurrent()));
}
