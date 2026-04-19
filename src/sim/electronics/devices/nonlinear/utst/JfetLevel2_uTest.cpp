/**
 * @file JfetLevel2_uTest.cpp
 * @brief Unit tests for JfetLevel2.
 */

#include "src/sim/electronics/devices/nonlinear/inc/JfetLevel2.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::JfetLevel2;
using sim::electronics::devices::nonlinear::JfetLevel2Params;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(JfetLevel2, DefaultParameters) {
  JfetLevel2Params params;
  EXPECT_DOUBLE_EQ(params.Beta, 1e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -2.0);
  EXPECT_DOUBLE_EQ(params.lambda, 0.01);
  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.alpha, 1.0);
}

/** @test */
TEST(JfetLevel2, CustomParameters) {
  JfetLevel2Params params{.Beta = 2e-3, .Vp = -2.5, .lambda = 0.02, .Is = 1e-13};
  EXPECT_DOUBLE_EQ(params.Beta, 2e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -2.5);
  EXPECT_DOUBLE_EQ(params.lambda, 0.02);
  EXPECT_DOUBLE_EQ(params.Is, 1e-13);
}

/* ----------------------------- Drain Current Tests ----------------------------- */

/** @test */
TEST(JfetLevel2, DrainCurrentCutoff) {
  JfetLevel2Params params{.Vp = -2.0};
  const double vgs = -2.5; // Below pinch-off
  const double vds = 5.0;
  const double id = JfetLevel2::drainCurrent(vgs, vds, params);

  // Subthreshold (non-zero but very small)
  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-9);
}

/** @test */
TEST(JfetLevel2, DrainCurrentLinear) {
  JfetLevel2Params params;
  const double vgs = -0.5; // Above pinch-off
  const double vds = 0.5;  // Small Vds
  const double id = JfetLevel2::drainCurrent(vgs, vds, params);

  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-2);
}

/** @test */
TEST(JfetLevel2, DrainCurrentSaturation) {
  JfetLevel2Params params;
  const double vgs = -0.5;
  const double vds = 5.0; // Large Vds
  const double id = JfetLevel2::drainCurrent(vgs, vds, params);

  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-2);
}

/** @test */
TEST(JfetLevel2, DrainCurrentZeroGateVoltage) {
  JfetLevel2Params params{.Vp = -2.0};
  const double vgs = 0.0; // Zero gate bias
  const double vds = 5.0;
  const double id = JfetLevel2::drainCurrent(vgs, vds, params);

  // Should conduct (Vgs > Vp)
  EXPECT_GT(id, 1e-4);
}

/* ----------------------------- Gate Current Tests ----------------------------- */

/** @test */
TEST(JfetLevel2, GateCurrentReverseBias) {
  JfetLevel2Params params{.Is = 1e-14};
  const double vgs = -1.0; // Reverse bias (normal for N-channel)
  const double ig = JfetLevel2::gateCurrent(vgs, params);

  // Reverse leakage (small negative)
  EXPECT_LT(ig, 0.0);
  EXPECT_GT(ig, -params.Is * 2.0);
}

/** @test */
TEST(JfetLevel2, GateCurrentZeroBias) {
  JfetLevel2Params params;
  const double vgs = 0.0;
  const double ig = JfetLevel2::gateCurrent(vgs, params);

  EXPECT_NEAR(ig, 0.0, params.Is);
}

/** @test */
TEST(JfetLevel2, GateCurrentForwardBias) {
  JfetLevel2Params params{.Is = 1e-14};
  const double vgs = 0.6; // Forward bias (unusual, but possible)
  const double ig = JfetLevel2::gateCurrent(vgs, params);

  // Forward current (exponential)
  EXPECT_GT(ig, 1e-6);
}

/* ----------------------------- Transconductance Tests ----------------------------- */

/** @test */
TEST(JfetLevel2, TransconductanceCutoff) {
  JfetLevel2Params params{.Vp = -2.0};
  const double vgs = -2.5;
  const double vds = 5.0;
  const double gm = JfetLevel2::transconductance(vgs, vds, params);

  // Small in cutoff (subthreshold)
  EXPECT_GT(gm, 0.0);
  EXPECT_LT(gm, 1e-6);
}

/** @test */
TEST(JfetLevel2, TransconductanceSaturation) {
  JfetLevel2Params params;
  const double vgs = -0.5;
  const double vds = 5.0;
  const double gm = JfetLevel2::transconductance(vgs, vds, params);

  EXPECT_GT(gm, 1e-4);
}

/** @test */
TEST(JfetLevel2, OutputConductanceSaturation) {
  JfetLevel2Params params{.lambda = 0.01};
  const double vgs = -0.5;
  const double vds = 5.0;
  const double gds = JfetLevel2::outputConductance(vgs, vds, params);

  // Non-zero due to channel-length modulation
  EXPECT_GT(gds, 0.0);
  EXPECT_LT(gds, 1e-4);
}

/** @test */
TEST(JfetLevel2, GateConductance) {
  JfetLevel2Params params{.Is = 1e-14};
  const double vgs = -1.0;
  const double gg = JfetLevel2::gateConductance(vgs, params);

  EXPECT_GT(gg, 0.0);
  EXPECT_LT(gg, 1e-9); // Very small for reverse bias
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(JfetLevel2, TransconductanceNumericalDerivative) {
  JfetLevel2Params params;
  const double vgs = -0.5, vds = 5.0;
  const double dv = 1e-8;

  const double gm = JfetLevel2::transconductance(vgs, vds, params);

  const double i1 = JfetLevel2::drainCurrent(vgs - dv, vds, params);
  const double i2 = JfetLevel2::drainCurrent(vgs + dv, vds, params);
  const double gmNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gm, gmNumerical, std::abs(gmNumerical) * 0.01);
}

/** @test */
TEST(JfetLevel2, OutputConductanceNumericalDerivative) {
  JfetLevel2Params params;
  const double vgs = -0.5, vds = 5.0;
  const double dv = 1e-8;

  const double gds = JfetLevel2::outputConductance(vgs, vds, params);

  const double i1 = JfetLevel2::drainCurrent(vgs, vds - dv, params);
  const double i2 = JfetLevel2::drainCurrent(vgs, vds + dv, params);
  const double gdsNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gds, gdsNumerical, std::abs(gdsNumerical) * 0.01);
}

/** @test */
TEST(JfetLevel2, GateConductanceNumericalDerivative) {
  JfetLevel2Params params;
  const double vgs = 0.5; // Forward bias for measurable conductance
  const double dv = 1e-8;

  const double gg = JfetLevel2::gateConductance(vgs, params);

  const double i1 = JfetLevel2::gateCurrent(vgs - dv, params);
  const double i2 = JfetLevel2::gateCurrent(vgs + dv, params);
  const double ggNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gg, ggNumerical, std::max(std::abs(ggNumerical) * 0.01, 1e-20));
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(JfetLevel2, StampSaturation) {
  MnaSystem mna(4);
  JfetLevel2Params params;
  const NetID drain = 1, gate = 2, source = 3;
  const double vgs = -0.5, vds = 5.0;

  // Should stamp both drain and gate currents
  JfetLevel2::stamp(mna, drain, gate, source, vgs, vds, params);
}

/** @test */
TEST(JfetLevel2, StampLinear) {
  MnaSystem mna(4);
  JfetLevel2Params params;
  const NetID drain = 1, gate = 2, source = 3;
  const double vgs = -0.5, vds = 0.5;

  JfetLevel2::stamp(mna, drain, gate, source, vgs, vds, params);
}

/** @test */
TEST(JfetLevel2, StampCutoff) {
  MnaSystem mna(4);
  JfetLevel2Params params;
  const NetID drain = 1, gate = 2, source = 3;
  const double vgs = -2.5, vds = 5.0;

  JfetLevel2::stamp(mna, drain, gate, source, vgs, vds, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(JfetLevel2, ChannelLengthModulation) {
  JfetLevel2Params params{.lambda = 0.02};
  const double vgs = -0.5;

  const double id_low = JfetLevel2::drainCurrent(vgs, 3.0, params);
  const double id_high = JfetLevel2::drainCurrent(vgs, 6.0, params);

  // Higher Vds increases current in saturation (CLM)
  EXPECT_GT(id_high, id_low);
}

/** @test */
TEST(JfetLevel2, SubthresholdConduction) {
  JfetLevel2Params params{.Vp = -2.0, .n_sub = 1.5};
  const double vgs = -2.2; // Slightly below pinch-off
  const double vds = 3.0;

  const double id = JfetLevel2::drainCurrent(vgs, vds, params);

  // Subthreshold current (exponential in Vgs)
  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-7); // Subthreshold range (nA to sub-nA)
}

/** @test */
TEST(JfetLevel2, GateLeakageIncreasesWithForwardBias) {
  JfetLevel2Params params;

  const double ig_reverse = JfetLevel2::gateCurrent(-1.0, params);
  const double ig_less_reverse = JfetLevel2::gateCurrent(-0.5, params);

  // Less reverse bias -> larger (less negative) leakage
  EXPECT_GT(ig_less_reverse, ig_reverse);
}

/** @test */
TEST(JfetLevel2, SaturationTransitionSmoothness) {
  JfetLevel2Params smooth{.alpha = 2.0}; // Smoother transition
  JfetLevel2Params sharp{.alpha = 0.5};  // Sharper transition

  const double vgs = -0.5;
  const double vds = 1.5; // Near saturation

  const double id_smooth = JfetLevel2::drainCurrent(vgs, vds, smooth);
  const double id_sharp = JfetLevel2::drainCurrent(vgs, vds, sharp);

  // Different alpha values give different currents in transition region
  EXPECT_NE(id_smooth, id_sharp);
}

/** @test */
TEST(JfetLevel2, TransconductanceIncreasesWithVgs) {
  JfetLevel2Params params;
  const double vds = 5.0;

  const double gm_low = JfetLevel2::transconductance(-1.0, vds, params);
  const double gm_high = JfetLevel2::transconductance(-0.3, vds, params);

  // Higher Vgs (less negative) -> higher gm
  EXPECT_GT(gm_high, gm_low);
}
