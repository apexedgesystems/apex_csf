/**
 * @file MosfetLevel2_uTest.cpp
 * @brief Unit tests for MosfetLevel2.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel2.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetLevel2;
using sim::electronics::devices::nonlinear::MosfetLevel2Params;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(MosfetLevel2, DefaultParameters) {
  MosfetLevel2Params params;
  EXPECT_DOUBLE_EQ(params.Kp, 100e-6);
  EXPECT_DOUBLE_EQ(params.Vth0, 0.7);
  EXPECT_DOUBLE_EQ(params.lambda, 0.02);
  EXPECT_DOUBLE_EQ(params.W, 10e-6);
  EXPECT_DOUBLE_EQ(params.L, 1e-6);
  EXPECT_DOUBLE_EQ(params.theta, 0.1);
  EXPECT_DOUBLE_EQ(params.E_crit, 1e6);
  EXPECT_DOUBLE_EQ(params.gamma, 0.5);
  EXPECT_DOUBLE_EQ(params.phi, 0.6);
  EXPECT_DOUBLE_EQ(params.n_sub, 1.5);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
}

/** @test */
TEST(MosfetLevel2, CustomParameters) {
  MosfetLevel2Params params{
      .Kp = 200e-6, .Vth0 = 0.5, .lambda = 0.01, .W = 20e-6, .L = 0.5e-6, .theta = 0.2};
  EXPECT_DOUBLE_EQ(params.Kp, 200e-6);
  EXPECT_DOUBLE_EQ(params.Vth0, 0.5);
  EXPECT_DOUBLE_EQ(params.lambda, 0.01);
  EXPECT_DOUBLE_EQ(params.W, 20e-6);
  EXPECT_DOUBLE_EQ(params.L, 0.5e-6);
  EXPECT_DOUBLE_EQ(params.theta, 0.2);
}

/* ----------------------------- Threshold Voltage Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2, ThresholdVoltageZeroBias) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5, .phi = 0.6};
  const double vbs = 0.0;
  const double vth = MosfetLevel2::thresholdVoltage(vbs, params);

  // No body effect at Vbs=0
  EXPECT_DOUBLE_EQ(vth, params.Vth0);
}

/** @test */
TEST(MosfetLevel2, ThresholdVoltageReverseBias) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5, .phi = 0.6};
  const double vbs = -1.0; // Reverse bias (source above bulk)
  const double vth = MosfetLevel2::thresholdVoltage(vbs, params);

  // Body effect increases threshold
  EXPECT_GT(vth, params.Vth0);
}

/** @test */
TEST(MosfetLevel2, ThresholdVoltageForwardBias) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5, .phi = 0.6};
  const double vbs = 0.5; // Forward bias (bulk above source)
  const double vth = MosfetLevel2::thresholdVoltage(vbs, params);

  // No body effect for forward bias (clamped to Vth0)
  EXPECT_DOUBLE_EQ(vth, params.Vth0);
}

/* ----------------------------- Mobility Degradation Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2, MobilityFactorCutoff) {
  MosfetLevel2Params params{.theta = 0.1};
  const double vgs = 0.0;
  const double vth = 0.7;
  const double mu = MosfetLevel2::mobilityFactor(vgs, vth, params);

  // No degradation in cutoff
  EXPECT_DOUBLE_EQ(mu, 1.0);
}

/** @test */
TEST(MosfetLevel2, MobilityFactorActive) {
  MosfetLevel2Params params{.theta = 0.1};
  const double vgs = 2.0;
  const double vth = 0.7;
  const double mu = MosfetLevel2::mobilityFactor(vgs, vth, params);

  // Degradation: mu = 1 / (1 + theta * (Vgs - Vth))
  const double expected = 1.0 / (1.0 + params.theta * (vgs - vth));
  EXPECT_DOUBLE_EQ(mu, expected);
  EXPECT_LT(mu, 1.0);
}

/* ----------------------------- Velocity Saturation Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2, DrainSaturationVoltageCutoff) {
  MosfetLevel2Params params{.L = 1e-6, .E_crit = 1e6};
  const double vgs = 0.0;
  const double vth = 0.7;
  const double vdsat = MosfetLevel2::drainSaturationVoltage(vgs, vth, params);

  EXPECT_DOUBLE_EQ(vdsat, 0.0);
}

/** @test */
TEST(MosfetLevel2, DrainSaturationVoltageActive) {
  MosfetLevel2Params params{.L = 1e-6, .E_crit = 1e6};
  const double vgs = 2.0;
  const double vth = 0.7;
  const double vdsat = MosfetLevel2::drainSaturationVoltage(vgs, vth, params);

  // Velocity saturation reduces Vdsat below ideal (Vgs - Vth)
  const double vgst = vgs - vth;
  EXPECT_LT(vdsat, vgst);
  EXPECT_GT(vdsat, 0.0);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2, CurrentCutoff) {
  MosfetLevel2Params params;
  const double vgs = 0.0; // Below threshold
  const double vds = 1.0;
  const double vbs = 0.0;
  const double id = MosfetLevel2::current(vgs, vds, vbs, params);

  // Should have small subthreshold current (not zero)
  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-9); // Very small (nA range)
}

/** @test */
TEST(MosfetLevel2, CurrentLinear) {
  MosfetLevel2Params params{.Kp = 100e-6, .Vth0 = 0.7, .W = 10e-6, .L = 1e-6};
  const double vgs = 2.0;
  const double vds = 0.5; // Small Vds -> linear
  const double vbs = 0.0;
  const double id = MosfetLevel2::current(vgs, vds, vbs, params);

  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-3); // mA range
}

/** @test */
TEST(MosfetLevel2, CurrentSaturation) {
  MosfetLevel2Params params{.Kp = 100e-6, .Vth0 = 0.7, .W = 10e-6, .L = 1e-6};
  const double vgs = 2.0;
  const double vds = 5.0; // Large Vds -> saturation
  const double vbs = 0.0;
  const double id = MosfetLevel2::current(vgs, vds, vbs, params);

  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-3); // mA range
}

/** @test */
TEST(MosfetLevel2, CurrentBodyEffect) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5};
  const double vgs = 2.0;
  const double vds = 3.0;

  const double id_no_body = MosfetLevel2::current(vgs, vds, 0.0, params);
  const double id_with_body = MosfetLevel2::current(vgs, vds, -1.0, params);

  // Reverse bulk bias increases Vth, reduces current
  EXPECT_LT(id_with_body, id_no_body);
}

/* ----------------------------- Transconductance Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2, TransconductanceCutoff) {
  MosfetLevel2Params params;
  const double vgs = 0.0;
  const double vds = 1.0;
  const double vbs = 0.0;
  const double gm = MosfetLevel2::transconductance(vgs, vds, vbs, params);

  // Small but non-zero due to subthreshold
  EXPECT_GT(gm, 0.0);
  EXPECT_LT(gm, 1e-6);
}

/** @test */
TEST(MosfetLevel2, TransconductanceSaturation) {
  MosfetLevel2Params params;
  const double vgs = 2.0;
  const double vds = 5.0;
  const double vbs = 0.0;
  const double gm = MosfetLevel2::transconductance(vgs, vds, vbs, params);

  EXPECT_GT(gm, 1e-5); // Significant in saturation
}

/** @test */
TEST(MosfetLevel2, OutputConductanceSaturation) {
  MosfetLevel2Params params{.lambda = 0.02};
  const double vgs = 2.0;
  const double vds = 5.0;
  const double vbs = 0.0;
  const double gds = MosfetLevel2::outputConductance(vgs, vds, vbs, params);

  // Non-zero due to channel-length modulation
  EXPECT_GT(gds, 0.0);
  EXPECT_LT(gds, 1e-4); // Smaller than gm
}

/** @test */
TEST(MosfetLevel2, BulkTransconductance) {
  MosfetLevel2Params params{.gamma = 0.5};
  const double vgs = 2.0;
  const double vds = 3.0;
  const double vbs = -0.5;
  const double gmb = MosfetLevel2::bulkTransconductance(vgs, vds, vbs, params);

  // Non-zero body effect
  EXPECT_GT(gmb, 0.0);
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(MosfetLevel2, TransconductanceNumericalDerivative) {
  MosfetLevel2Params params;
  const double vgs = 2.0;
  const double vds = 3.0;
  const double vbs = 0.0;
  const double dv = 1e-8;

  const double gm = MosfetLevel2::transconductance(vgs, vds, vbs, params);

  const double i1 = MosfetLevel2::current(vgs - dv, vds, vbs, params);
  const double i2 = MosfetLevel2::current(vgs + dv, vds, vbs, params);
  const double gmNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gm, gmNumerical, std::abs(gmNumerical) * 0.01);
}

/** @test */
TEST(MosfetLevel2, OutputConductanceNumericalDerivative) {
  MosfetLevel2Params params;
  const double vgs = 2.0;
  const double vds = 3.0;
  const double vbs = 0.0;
  const double dv = 1e-8;

  const double gds = MosfetLevel2::outputConductance(vgs, vds, vbs, params);

  const double i1 = MosfetLevel2::current(vgs, vds - dv, vbs, params);
  const double i2 = MosfetLevel2::current(vgs, vds + dv, vbs, params);
  const double gdsNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gds, gdsNumerical, std::abs(gdsNumerical) * 0.01);
}

/** @test */
TEST(MosfetLevel2, BulkTransconductanceNumericalDerivative) {
  MosfetLevel2Params params;
  const double vgs = 2.0;
  const double vds = 3.0;
  const double vbs = -0.5;
  const double dv = 1e-8;

  const double gmb = MosfetLevel2::bulkTransconductance(vgs, vds, vbs, params);

  const double i1 = MosfetLevel2::current(vgs, vds, vbs - dv, params);
  const double i2 = MosfetLevel2::current(vgs, vds, vbs + dv, params);
  const double gmbNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gmb, gmbNumerical, std::abs(gmbNumerical) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(MosfetLevel2, StampSaturation) {
  MnaSystem mna(5);
  MosfetLevel2Params params;
  const NetID drain = 1, gate = 2, source = 3, bulk = 4;
  const double vgs = 2.0, vds = 5.0, vbs = 0.0;

  // Should stamp four-terminal transconductances
  MosfetLevel2::stamp(mna, drain, gate, source, bulk, vgs, vds, vbs, params);
}

/** @test */
TEST(MosfetLevel2, StampLinear) {
  MnaSystem mna(5);
  MosfetLevel2Params params;
  const NetID drain = 1, gate = 2, source = 3, bulk = 4;
  const double vgs = 2.0, vds = 0.5, vbs = 0.0;

  MosfetLevel2::stamp(mna, drain, gate, source, bulk, vgs, vds, vbs, params);
}

/** @test */
TEST(MosfetLevel2, StampWithBodyBias) {
  MnaSystem mna(5);
  MosfetLevel2Params params;
  const NetID drain = 1, gate = 2, source = 3, bulk = 4;
  const double vgs = 2.0, vds = 3.0, vbs = -1.0;

  MosfetLevel2::stamp(mna, drain, gate, source, bulk, vgs, vds, vbs, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(MosfetLevel2, GeometryScaling) {
  MosfetLevel2Params small{.W = 10e-6, .L = 1e-6};
  MosfetLevel2Params large{.W = 20e-6, .L = 1e-6}; // Double width
  const double vgs = 2.0, vds = 3.0, vbs = 0.0;

  const double id_small = MosfetLevel2::current(vgs, vds, vbs, small);
  const double id_large = MosfetLevel2::current(vgs, vds, vbs, large);

  // Doubling W doubles current (for fixed L)
  EXPECT_NEAR(id_large / id_small, 2.0, 0.1);
}

/** @test */
TEST(MosfetLevel2, ChannelLengthModulation) {
  MosfetLevel2Params params{.lambda = 0.02};
  const double vgs = 2.0, vbs = 0.0;

  const double id_low = MosfetLevel2::current(vgs, 3.0, vbs, params);
  const double id_high = MosfetLevel2::current(vgs, 5.0, vbs, params);

  // In saturation, higher Vds increases current (CLM)
  EXPECT_GT(id_high, id_low);
}

/** @test */
TEST(MosfetLevel2, MobilityDegradationEffect) {
  MosfetLevel2Params no_deg{.theta = 0.0};
  MosfetLevel2Params with_deg{.theta = 0.2};
  const double vgs = 3.0, vds = 5.0, vbs = 0.0;

  const double id_no_deg = MosfetLevel2::current(vgs, vds, vbs, no_deg);
  const double id_with_deg = MosfetLevel2::current(vgs, vds, vbs, with_deg);

  // Mobility degradation reduces current
  EXPECT_LT(id_with_deg, id_no_deg);
}

/** @test */
TEST(MosfetLevel2, VelocitySaturationEffect) {
  MosfetLevel2Params high_field{.E_crit = 1e5}; // Low critical field (more saturation)
  MosfetLevel2Params low_field{.E_crit = 1e7};  // High critical field (less saturation)
  const double vgs = 3.0, vds = 5.0, vbs = 0.0;

  const double vdsat_high = MosfetLevel2::drainSaturationVoltage(vgs, 0.7, high_field);
  const double vdsat_low = MosfetLevel2::drainSaturationVoltage(vgs, 0.7, low_field);

  // Lower E_crit causes stronger velocity saturation (lower Vdsat)
  EXPECT_LT(vdsat_high, vdsat_low);
}

/** @test */
TEST(MosfetLevel2, SubthresholdConduction) {
  MosfetLevel2Params params;
  const double vgs = 0.5; // Below threshold
  const double vds = 1.0, vbs = 0.0;

  const double id = MosfetLevel2::current(vgs, vds, vbs, params);

  // Should have weak inversion current (exponential in Vgs)
  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-8); // Subthreshold (nA range)
}

/** @test */
TEST(MosfetLevel2, BodyEffectStrength) {
  MosfetLevel2Params weak{.gamma = 0.1};
  MosfetLevel2Params strong{.gamma = 0.8};
  const double vbs = -1.0;

  const double vth_weak = MosfetLevel2::thresholdVoltage(vbs, weak);
  const double vth_strong = MosfetLevel2::thresholdVoltage(vbs, strong);

  // Stronger body effect -> larger threshold shift
  EXPECT_GT(vth_strong - strong.Vth0, vth_weak - weak.Vth0);
}
