/**
 * @file JfetShichman_uTest.cpp
 * @brief Unit tests for JfetShichman.
 */

#include "src/sim/electronics/devices/nonlinear/inc/JfetShichman.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::JfetShichman;
using sim::electronics::devices::nonlinear::JfetShichmanParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(JfetShichman, DefaultParameters) {
  JfetShichmanParams params;
  EXPECT_DOUBLE_EQ(params.Beta, 1e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -2.0);
  EXPECT_DOUBLE_EQ(params.lambda, 0.01);
}

/** @test */
TEST(JfetShichman, CustomParameters) {
  JfetShichmanParams params{.Beta = 2e-3, .Vp = -3.0, .lambda = 0.02};
  EXPECT_DOUBLE_EQ(params.Beta, 2e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -3.0);
  EXPECT_DOUBLE_EQ(params.lambda, 0.02);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(JfetShichman, CurrentCutoff) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double vgs = -2.5; // Below Vp (cutoff)
  const double vds = 5.0;
  const double id = JfetShichman::current(vgs, vds, params);

  // Should be zero (cutoff region)
  EXPECT_DOUBLE_EQ(id, 0.0);
}

/** @test */
TEST(JfetShichman, CurrentLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double vgs = -0.5; // Below Vp
  const double vds = 0.5;  // Vds < Vgst = -0.5 - (-2.0) = 1.5
  const double id = JfetShichman::current(vgs, vds, params);

  // Linear region: Id = 2*Beta*[(Vgs - Vp)*Vds - 0.5*Vds^2]
  const double vgst = vgs - params.Vp; // 1.5
  const double expected = 2.0 * params.Beta * (vgst * vds - 0.5 * vds * vds);
  EXPECT_NEAR(id, expected, std::abs(expected) * 1e-10);
  EXPECT_GT(id, 0.0); // Should conduct
}

/** @test */
TEST(JfetShichman, CurrentSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double vgs = -0.5; // Below Vp
  const double vds = 5.0;  // Vds > Vgst = 1.5 (saturation)
  const double id = JfetShichman::current(vgs, vds, params);

  // Saturation region: Id = Beta*(Vgs - Vp)^2
  const double vgst = vgs - params.Vp; // 1.5
  const double expected = params.Beta * vgst * vgst;
  EXPECT_NEAR(id, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(JfetShichman, CurrentChannelModulation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 5.0;
  const double id = JfetShichman::current(vgs, vds, params);

  // Saturation with channel modulation: Id = Beta*Vgst^2 * (1 + lambda*Vds)
  const double vgst = vgs - params.Vp;
  const double expected = params.Beta * vgst * vgst * (1.0 + params.lambda * vds);
  EXPECT_NEAR(id, expected, std::abs(expected) * 1e-10);
  EXPECT_GT(id, params.Beta * vgst * vgst); // Should be higher than no modulation
}

/** @test */
TEST(JfetShichman, CurrentAtPinchoff) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double vgs = -2.0; // Exactly at Vp
  const double vds = 5.0;
  const double id = JfetShichman::current(vgs, vds, params);

  // At pinch-off, current should be very small (Vgst = 0)
  EXPECT_NEAR(id, 0.0, 1e-10);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(JfetShichman, TransconductanceCutoff) {
  JfetShichmanParams params{.Vp = -2.0};
  const double vgs = -2.5; // Below Vp (cutoff)
  const double vds = 5.0;
  const double gm = JfetShichman::transconductance(vgs, vds, params);

  EXPECT_DOUBLE_EQ(gm, 0.0);
}

/** @test */
TEST(JfetShichman, TransconductanceLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double vgs = -0.5;
  const double vds = 0.5;
  const double gm = JfetShichman::transconductance(vgs, vds, params);

  // Linear: gm = 2*Beta*Vds
  const double expected = 2.0 * params.Beta * vds;
  EXPECT_NEAR(gm, expected, std::abs(expected) * 1e-10);
  EXPECT_GT(gm, 0.0);
}

/** @test */
TEST(JfetShichman, TransconductanceSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double vgs = -0.5;
  const double vds = 5.0;
  const double gm = JfetShichman::transconductance(vgs, vds, params);

  // Saturation: gm = 2*Beta*Vgst
  const double vgst = vgs - params.Vp;
  const double expected = 2.0 * params.Beta * vgst;
  EXPECT_NEAR(gm, expected, std::abs(expected) * 1e-10);
  EXPECT_GT(gm, 0.0);
}

/** @test */
TEST(JfetShichman, OutputConductanceCutoff) {
  JfetShichmanParams params{.Vp = -2.0};
  const double vgs = -2.5; // Below Vp (cutoff)
  const double vds = 5.0;
  const double gds = JfetShichman::outputConductance(vgs, vds, params);

  EXPECT_DOUBLE_EQ(gds, 0.0);
}

/** @test */
TEST(JfetShichman, OutputConductanceLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 0.5;
  const double gds = JfetShichman::outputConductance(vgs, vds, params);

  // Linear region has higher output conductance
  EXPECT_GT(gds, 0.0);
}

/** @test */
TEST(JfetShichman, OutputConductanceSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 5.0;
  const double gds = JfetShichman::outputConductance(vgs, vds, params);

  // Saturation: gds = lambda*Beta*Vgst^2 (channel modulation only)
  const double vgst = vgs - params.Vp;
  const double expected = params.lambda * params.Beta * vgst * vgst;
  EXPECT_NEAR(gds, expected, std::abs(expected) * 1e-10);
  EXPECT_GT(gds, 0.0);
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(JfetShichman, TransconductanceNumericalDerivativeSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 5.0;
  const double dvgs = 1e-8;

  const double gmAnalytical = JfetShichman::transconductance(vgs, vds, params);

  const double id1 = JfetShichman::current(vgs - dvgs, vds, params);
  const double id2 = JfetShichman::current(vgs + dvgs, vds, params);
  const double gmNumerical = (id2 - id1) / (2.0 * dvgs);

  EXPECT_NEAR(gmAnalytical, gmNumerical, std::abs(gmNumerical) * 0.01);
}

/** @test */
TEST(JfetShichman, TransconductanceNumericalDerivativeLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 0.5;
  const double dvgs = 1e-8;

  const double gmAnalytical = JfetShichman::transconductance(vgs, vds, params);

  const double id1 = JfetShichman::current(vgs - dvgs, vds, params);
  const double id2 = JfetShichman::current(vgs + dvgs, vds, params);
  const double gmNumerical = (id2 - id1) / (2.0 * dvgs);

  EXPECT_NEAR(gmAnalytical, gmNumerical, std::abs(gmNumerical) * 0.01);
}

/** @test */
TEST(JfetShichman, OutputConductanceNumericalDerivativeSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 5.0;
  const double dvds = 1e-8;

  const double gdsAnalytical = JfetShichman::outputConductance(vgs, vds, params);

  const double id1 = JfetShichman::current(vgs, vds - dvds, params);
  const double id2 = JfetShichman::current(vgs, vds + dvds, params);
  const double gdsNumerical = (id2 - id1) / (2.0 * dvds);

  EXPECT_NEAR(gdsAnalytical, gdsNumerical, std::abs(gdsNumerical) * 0.01);
}

/** @test */
TEST(JfetShichman, OutputConductanceNumericalDerivativeLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 0.5;
  const double dvds = 1e-8;

  const double gdsAnalytical = JfetShichman::outputConductance(vgs, vds, params);

  const double id1 = JfetShichman::current(vgs, vds - dvds, params);
  const double id2 = JfetShichman::current(vgs, vds + dvds, params);
  const double gdsNumerical = (id2 - id1) / (2.0 * dvds);

  EXPECT_NEAR(gdsAnalytical, gdsNumerical, std::abs(gdsNumerical) * 0.01);
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test */
TEST(JfetShichman, RegionCutoff) {
  JfetShichmanParams params{.Vp = -2.0};
  EXPECT_EQ(JfetShichman::region(-3.0, 5.0, params), 0); // Below Vp (cutoff)
  EXPECT_EQ(JfetShichman::region(-2.5, 5.0, params), 0); // Below Vp (cutoff)
  EXPECT_EQ(JfetShichman::region(-2.0, 5.0, params), 0); // At Vp (cutoff)
}

/** @test */
TEST(JfetShichman, RegionLinear) {
  JfetShichmanParams params{.Vp = -2.0};
  const double vgs = -0.5;
  const double vgst = vgs - params.Vp; // 1.5

  EXPECT_EQ(JfetShichman::region(vgs, 0.5, params), 1); // Vds < Vgst
  EXPECT_EQ(JfetShichman::region(vgs, 1.0, params), 1); // Vds < Vgst
  EXPECT_EQ(JfetShichman::region(vgs, 1.4, params), 1); // Vds < Vgst
}

/** @test */
TEST(JfetShichman, RegionSaturation) {
  JfetShichmanParams params{.Vp = -2.0};
  const double vgs = -0.5;
  const double vgst = vgs - params.Vp; // 1.5

  EXPECT_EQ(JfetShichman::region(vgs, vgst, params), 2); // Vds = Vgst (boundary)
  EXPECT_EQ(JfetShichman::region(vgs, 2.0, params), 2);  // Vds > Vgst
  EXPECT_EQ(JfetShichman::region(vgs, 5.0, params), 2);  // Vds > Vgst
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(JfetShichman, StampSaturation) {
  MnaSystem mna(4);
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const NetID drain = 1;
  const NetID gate = 2;
  const NetID source = 0;
  const double vgs = -0.5;
  const double vds = 5.0;

  // Should stamp transconductance and output conductance
  JfetShichman::stamp(mna, drain, gate, source, vgs, vds, params);
}

/** @test */
TEST(JfetShichman, StampLinear) {
  MnaSystem mna(4);
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const NetID drain = 1;
  const NetID gate = 2;
  const NetID source = 0;
  const double vgs = -0.5;
  const double vds = 0.5;

  // Should stamp linear region conductances
  JfetShichman::stamp(mna, drain, gate, source, vgs, vds, params);
}

/** @test */
TEST(JfetShichman, StampCutoff) {
  MnaSystem mna(4);
  JfetShichmanParams params{.Vp = -2.0};
  const NetID drain = 1;
  const NetID gate = 2;
  const NetID source = 0;
  const double vgs = -2.5; // Below Vp (cutoff)
  const double vds = 5.0;

  // Should stamp zero conductances (cutoff)
  JfetShichman::stamp(mna, drain, gate, source, vgs, vds, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(JfetShichman, SaturationConstantCurrent) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double vgs = -0.5;

  // In saturation (no channel modulation), current should be constant vs Vds
  const double id1 = JfetShichman::current(vgs, 3.0, params);
  const double id2 = JfetShichman::current(vgs, 5.0, params);
  const double id3 = JfetShichman::current(vgs, 10.0, params);

  // All should be nearly equal (saturation)
  EXPECT_NEAR(id1, id2, 1e-10);
  EXPECT_NEAR(id2, id3, 1e-10);
}

/** @test */
TEST(JfetShichman, LinearOhmicBehavior) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double vgs = -0.5;

  // In linear region, current should increase with Vds
  const double id1 = JfetShichman::current(vgs, 0.1, params);
  const double id2 = JfetShichman::current(vgs, 0.5, params);
  const double id3 = JfetShichman::current(vgs, 1.0, params);

  EXPECT_GT(id2, id1); // Current increases with Vds
  EXPECT_GT(id3, id2);
}

/** @test */
TEST(JfetShichman, TransconductanceIncreasesWithVgs) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double vds = 5.0; // Saturation

  // More negative Vgs (closer to Vp) should reduce transconductance
  const double gm1 = JfetShichman::transconductance(-0.2, vds, params);
  const double gm2 = JfetShichman::transconductance(-0.5, vds, params);
  const double gm3 = JfetShichman::transconductance(-1.0, vds, params);

  EXPECT_GT(gm1, gm2); // Less negative Vgs -> higher gm
  EXPECT_GT(gm2, gm3);
}

/** @test */
TEST(JfetShichman, PinchoffCutoff) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double vds = 5.0;

  // Vgs at or below Vp should cut off
  const double id_cutoff = JfetShichman::current(-2.5, vds, params);  // Below Vp
  const double id_conduct = JfetShichman::current(-0.5, vds, params); // Above Vp

  EXPECT_DOUBLE_EQ(id_cutoff, 0.0);
  EXPECT_GT(id_conduct, 1e-6);
}

/** @test */
TEST(JfetShichman, ChannelModulationEffect) {
  JfetShichmanParams noMod{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  JfetShichmanParams withMod{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.02};
  const double vgs = -0.5;
  const double vds = 5.0;

  const double id_noMod = JfetShichman::current(vgs, vds, noMod);
  const double id_withMod = JfetShichman::current(vgs, vds, withMod);

  // Channel modulation should increase current
  EXPECT_GT(id_withMod, id_noMod);
}
