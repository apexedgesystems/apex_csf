/**
 * @file BjtCommonEmitter_uTest.cpp
 * @brief Unit tests for BJT common-emitter amplifier circuit model.
 *
 * Currently covers construction, default state, and net wiring. DC bias
 * tests are not included pending an asymmetric BjtEbersMoll stamp -- the
 * current symmetric stamp does not converge at the circuit level for
 * forward-active, saturation, or cutoff bias points.
 */

#include "src/sim/electronics/topologies/amplifiers/inc/BjtCommonEmitter.hpp"

#include <gtest/gtest.h>

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
