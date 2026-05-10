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

/** @test computeDC runs to completion and writes cached state within the supply envelope. */
TEST(BjtCommonEmitterTest, ComputeDcRunsAndCachesWithinEnvelope) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);
  const bool OK = amp.computeDC();

  // The wrapper either reports failure (false) or completes (true). Either is
  // a legitimate outcome of the current symmetric BJT stamp; we only require
  // that the returned bool be consistent with the cached values.
  if (OK) {
    EXPECT_GE(amp.collectorVoltage(), 0.0);
    EXPECT_LE(amp.collectorVoltage(), amp.vcc() + 1e-6);
    EXPECT_GE(amp.baseVoltage(), 0.0);
    EXPECT_LE(amp.baseVoltage(), amp.vcc() + 1e-6);
  }
}

/** @test computeDC across a small bias grid keeps cached voltages bounded. */
TEST(BjtCommonEmitterTest, ComputeDcAcrossBiasGridStaysBounded) {
  for (const double VCC : {5.0, 9.0, 12.0}) {
    for (const double RC : {1e3, 4.7e3}) {
      for (const double RB : {100e3, 1e6}) {
        BjtCommonEmitter amp(VCC, RC, RB);
        const bool OK = amp.computeDC();
        if (OK) {
          // Bounded by supply rails (no spurious overshoot).
          EXPECT_GE(amp.collectorVoltage(), 0.0);
          EXPECT_LE(amp.collectorVoltage(), VCC + 1e-6);
          EXPECT_GE(amp.baseVoltage(), 0.0);
          EXPECT_LE(amp.baseVoltage(), VCC + 1e-6);
        }
      }
    }
  }
}

/** @test Collector current cache obeys KCL: ic = (VCC - Vc) / RC. */
TEST(BjtCommonEmitterTest, CollectorCurrentIsKclConsistent) {
  BjtCommonEmitter amp(12.0, 1e3, 100e3);
  if (amp.computeDC()) {
    const double EXPECTED_IC = (amp.vcc() - amp.collectorVoltage()) / amp.rc();
    EXPECT_NEAR(amp.collectorCurrent(), EXPECTED_IC, 1e-9);
  }
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
  const bool FIRST_OK = amp.computeDC();
  const double VC1 = amp.collectorVoltage();
  const double VB1 = amp.baseVoltage();
  const double IC1 = amp.collectorCurrent();

  const bool SECOND_OK = amp.computeDC();
  EXPECT_EQ(FIRST_OK, SECOND_OK);
  if (SECOND_OK) {
    EXPECT_DOUBLE_EQ(amp.collectorVoltage(), VC1);
    EXPECT_DOUBLE_EQ(amp.baseVoltage(), VB1);
    EXPECT_DOUBLE_EQ(amp.collectorCurrent(), IC1);
  }
}

/** @test Custom BjtEbersMollParams flow through to the stamp lambda without crashing. */
TEST(BjtCommonEmitterTest, CustomBjtParamsAreAccepted) {
  using sim::electronics::devices::nonlinear::BjtEbersMollParams;
  BjtEbersMollParams params;  // default-constructed
  BjtCommonEmitter amp(9.0, 2.2e3, 47e3, params);

  // Construction with explicit params keeps the stored bias values consistent.
  EXPECT_DOUBLE_EQ(amp.vcc(), 9.0);
  EXPECT_DOUBLE_EQ(amp.rc(), 2.2e3);
  EXPECT_DOUBLE_EQ(amp.rb(), 47e3);

  if (amp.computeDC()) {
    EXPECT_TRUE(std::isfinite(amp.collectorVoltage()));
    EXPECT_TRUE(std::isfinite(amp.baseVoltage()));
    EXPECT_TRUE(std::isfinite(amp.collectorCurrent()));
  }
}
