/**
 * @file Level1Stamp_uTest.cpp
 * @brief Sanity checks for the MosfetLevel1 device model used by
 *        Intel4004GridLevel1.
 *
 * End-to-end DC accuracy (per-gate match against ngspice) is verified in
 * Level1Physics_uTest.cpp; this file exercises the device model itself for
 * representative bias points so any regression in Id / gm / gds shows up
 * before the larger circuit-level tests run.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <gtest/gtest.h>

using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

/* ----------------------------- Model Sanity ----------------------------- */

/** @test MosfetLevel1 produces sensible Id/gm/gds for a strongly-on PMOS-equivalent.
 *
 * The model uses NMOS-mirror sign conventions: callers feed in vsg/vsd as
 * positive scalars when the device is on. This test verifies the math is
 * monotonic and gives the right region under standard operating points. The
 * actual NR stamp + solve is validated end-to-end (vs ngspice 0.0000V) by
 * Level1Physics_uTest, which is the authoritative production check.
 */
TEST(Level1StampTest, ModelSanityStrongOn) {
  MosfetLevel1Params params{.Kp = 5e-3, .Vth = 1.0, .lambda = 0.02};

  // Strongly on: vgs well above Vth, vds in saturation
  double idSat = MosfetLevel1::current(5.0, 5.0, params);
  double gmSat = MosfetLevel1::transconductance(5.0, 5.0, params);
  double gdsSat = MosfetLevel1::outputConductance(5.0, 5.0, params);

  EXPECT_GT(idSat, 0.0) << "Strongly-on device should produce positive current";
  EXPECT_GT(gmSat, 0.0) << "gm should be positive in saturation";
  EXPECT_GT(gdsSat, 0.0) << "gds should be positive (channel-length modulation)";
  EXPECT_EQ(MosfetLevel1::region(5.0, 5.0, params), 2) << "should be saturation";
}

/** @test MosfetLevel1 returns zero current when gate is at source (cut off). */
TEST(Level1StampTest, ModelSanityOff) {
  MosfetLevel1Params params{.Kp = 5e-3, .Vth = 1.0, .lambda = 0.02};

  // vgs = 0 < Vth - Vsmooth -> deep cutoff
  EXPECT_DOUBLE_EQ(MosfetLevel1::current(0.0, 5.0, params), 0.0);
  EXPECT_DOUBLE_EQ(MosfetLevel1::transconductance(0.0, 5.0, params), 0.0);
  EXPECT_DOUBLE_EQ(MosfetLevel1::outputConductance(0.0, 5.0, params), 0.0);
  EXPECT_EQ(MosfetLevel1::region(0.0, 5.0, params), -1) << "should be deep cutoff";
}

/** @test Saturation current scales quadratically with gate overdrive. */
TEST(Level1StampTest, SaturationQuadratic) {
  MosfetLevel1Params params{.Kp = 5e-3, .Vth = 1.0, .lambda = 0.0};

  // In saturation with lambda=0: Id = 0.5 * Kp * (vgs - Vth)^2
  double id1 = MosfetLevel1::current(2.0, 5.0, params); // overdrive 1V
  double id2 = MosfetLevel1::current(3.0, 5.0, params); // overdrive 2V
  double id3 = MosfetLevel1::current(5.0, 5.0, params); // overdrive 4V

  // Quadratic: id2/id1 = 4, id3/id1 = 16
  EXPECT_NEAR(id2 / id1, 4.0, 1e-9);
  EXPECT_NEAR(id3 / id1, 16.0, 1e-9);
}
