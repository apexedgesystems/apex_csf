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
TEST(MosfetLevel2Test, DefaultParameters) {
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
TEST(MosfetLevel2Test, CustomParameters) {
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
TEST(MosfetLevel2Test, ThresholdVoltageZeroBias) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5, .phi = 0.6};
  const double VBS = 0.0;
  const double VTH = MosfetLevel2::thresholdVoltage(VBS, params);

  // No body effect at Vbs=0
  EXPECT_DOUBLE_EQ(VTH, params.Vth0);
}

/** @test */
TEST(MosfetLevel2Test, ThresholdVoltageReverseBias) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5, .phi = 0.6};
  const double VBS = -1.0; // Reverse bias (source above bulk)
  const double VTH = MosfetLevel2::thresholdVoltage(VBS, params);

  // Body effect increases threshold
  EXPECT_GT(VTH, params.Vth0);
}

/** @test */
TEST(MosfetLevel2Test, ThresholdVoltageForwardBias) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5, .phi = 0.6};
  const double VBS = 0.5; // Forward bias (bulk above source)
  const double VTH = MosfetLevel2::thresholdVoltage(VBS, params);

  // No body effect for forward bias (clamped to Vth0)
  EXPECT_DOUBLE_EQ(VTH, params.Vth0);
}

/* ----------------------------- Mobility Degradation Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2Test, MobilityFactorCutoff) {
  MosfetLevel2Params params{.theta = 0.1};
  const double VGS = 0.0;
  const double VTH = 0.7;
  const double MU = MosfetLevel2::mobilityFactor(VGS, VTH, params);

  // No degradation in cutoff
  EXPECT_DOUBLE_EQ(MU, 1.0);
}

/** @test */
TEST(MosfetLevel2Test, MobilityFactorActive) {
  MosfetLevel2Params params{.theta = 0.1};
  const double VGS = 2.0;
  const double VTH = 0.7;
  const double MU = MosfetLevel2::mobilityFactor(VGS, VTH, params);

  // Degradation: mu = 1 / (1 + theta * (Vgs - Vth))
  const double EXPECTED = 1.0 / (1.0 + params.theta * (VGS - VTH));
  EXPECT_DOUBLE_EQ(MU, EXPECTED);
  EXPECT_LT(MU, 1.0);
}

/* ----------------------------- Velocity Saturation Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2Test, DrainSaturationVoltageCutoff) {
  MosfetLevel2Params params{.L = 1e-6, .E_crit = 1e6};
  const double VGS = 0.0;
  const double VTH = 0.7;
  const double VDSAT = MosfetLevel2::drainSaturationVoltage(VGS, VTH, params);

  EXPECT_DOUBLE_EQ(VDSAT, 0.0);
}

/** @test */
TEST(MosfetLevel2Test, DrainSaturationVoltageActive) {
  MosfetLevel2Params params{.L = 1e-6, .E_crit = 1e6};
  const double VGS = 2.0;
  const double VTH = 0.7;
  const double VDSAT = MosfetLevel2::drainSaturationVoltage(VGS, VTH, params);

  // Velocity saturation reduces Vdsat below ideal (Vgs - Vth)
  const double VGST = VGS - VTH;
  EXPECT_LT(VDSAT, VGST);
  EXPECT_GT(VDSAT, 0.0);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2Test, CurrentCutoff) {
  MosfetLevel2Params params;
  const double VGS = 0.0; // Below threshold
  const double VDS = 1.0;
  const double VBS = 0.0;
  const double ID = MosfetLevel2::current(VGS, VDS, VBS, params);

  // Should have small subthreshold current (not zero)
  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-9); // Very small (nA range)
}

/** @test */
TEST(MosfetLevel2Test, CurrentLinear) {
  MosfetLevel2Params params{.Kp = 100e-6, .Vth0 = 0.7, .W = 10e-6, .L = 1e-6};
  const double VGS = 2.0;
  const double VDS = 0.5; // Small Vds -> linear
  const double VBS = 0.0;
  const double ID = MosfetLevel2::current(VGS, VDS, VBS, params);

  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-3); // mA range
}

/** @test */
TEST(MosfetLevel2Test, CurrentSaturation) {
  MosfetLevel2Params params{.Kp = 100e-6, .Vth0 = 0.7, .W = 10e-6, .L = 1e-6};
  const double VGS = 2.0;
  const double VDS = 5.0; // Large Vds -> saturation
  const double VBS = 0.0;
  const double ID = MosfetLevel2::current(VGS, VDS, VBS, params);

  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-3); // mA range
}

/** @test */
TEST(MosfetLevel2Test, CurrentBodyEffect) {
  MosfetLevel2Params params{.Vth0 = 0.7, .gamma = 0.5};
  const double VGS = 2.0;
  const double VDS = 3.0;

  const double ID_NO_BODY = MosfetLevel2::current(VGS, VDS, 0.0, params);
  const double ID_WITH_BODY = MosfetLevel2::current(VGS, VDS, -1.0, params);

  // Reverse bulk bias increases Vth, reduces current
  EXPECT_LT(ID_WITH_BODY, ID_NO_BODY);
}

/* ----------------------------- Transconductance Tests ----------------------------- */

/** @test */
TEST(MosfetLevel2Test, TransconductanceCutoff) {
  MosfetLevel2Params params;
  const double VGS = 0.0;
  const double VDS = 1.0;
  const double VBS = 0.0;
  const double GM = MosfetLevel2::transconductance(VGS, VDS, VBS, params);

  // Small but non-zero due to subthreshold
  EXPECT_GT(GM, 0.0);
  EXPECT_LT(GM, 1e-6);
}

/** @test */
TEST(MosfetLevel2Test, TransconductanceSaturation) {
  MosfetLevel2Params params;
  const double VGS = 2.0;
  const double VDS = 5.0;
  const double VBS = 0.0;
  const double GM = MosfetLevel2::transconductance(VGS, VDS, VBS, params);

  EXPECT_GT(GM, 1e-5); // Significant in saturation
}

/** @test */
TEST(MosfetLevel2Test, OutputConductanceSaturation) {
  MosfetLevel2Params params{.lambda = 0.02};
  const double VGS = 2.0;
  const double VDS = 5.0;
  const double VBS = 0.0;
  const double GDS = MosfetLevel2::outputConductance(VGS, VDS, VBS, params);

  // Non-zero due to channel-length modulation
  EXPECT_GT(GDS, 0.0);
  EXPECT_LT(GDS, 1e-4); // Smaller than gm
}

/** @test */
TEST(MosfetLevel2Test, BulkTransconductance) {
  MosfetLevel2Params params{.gamma = 0.5};
  const double VGS = 2.0;
  const double VDS = 3.0;
  const double VBS = -0.5;
  const double GMB = MosfetLevel2::bulkTransconductance(VGS, VDS, VBS, params);

  // Non-zero body effect
  EXPECT_GT(GMB, 0.0);
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(MosfetLevel2Test, TransconductanceNumericalDerivative) {
  MosfetLevel2Params params;
  const double VGS = 2.0;
  const double VDS = 3.0;
  const double VBS = 0.0;
  const double DV = 1e-8;

  const double GM = MosfetLevel2::transconductance(VGS, VDS, VBS, params);

  const double I1 = MosfetLevel2::current(VGS - DV, VDS, VBS, params);
  const double I2 = MosfetLevel2::current(VGS + DV, VDS, VBS, params);
  const double GM_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GM, GM_NUMERICAL, std::abs(GM_NUMERICAL) * 0.01);
}

/** @test */
TEST(MosfetLevel2Test, OutputConductanceNumericalDerivative) {
  MosfetLevel2Params params;
  const double VGS = 2.0;
  const double VDS = 3.0;
  const double VBS = 0.0;
  const double DV = 1e-8;

  const double GDS = MosfetLevel2::outputConductance(VGS, VDS, VBS, params);

  const double I1 = MosfetLevel2::current(VGS, VDS - DV, VBS, params);
  const double I2 = MosfetLevel2::current(VGS, VDS + DV, VBS, params);
  const double GDS_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GDS, GDS_NUMERICAL, std::abs(GDS_NUMERICAL) * 0.01);
}

/** @test */
TEST(MosfetLevel2Test, BulkTransconductanceNumericalDerivative) {
  MosfetLevel2Params params;
  const double VGS = 2.0;
  const double VDS = 3.0;
  const double VBS = -0.5;
  const double DV = 1e-8;

  const double GMB = MosfetLevel2::bulkTransconductance(VGS, VDS, VBS, params);

  const double I1 = MosfetLevel2::current(VGS, VDS, VBS - DV, params);
  const double I2 = MosfetLevel2::current(VGS, VDS, VBS + DV, params);
  const double GMB_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GMB, GMB_NUMERICAL, std::abs(GMB_NUMERICAL) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(MosfetLevel2Test, StampSaturation) {
  MnaSystem mna(5);
  MosfetLevel2Params params;
  const NetID DRAIN = 1, gate = 2, source = 3, bulk = 4;
  const double VGS = 2.0, vds = 5.0, vbs = 0.0;

  // Should stamp four-terminal transconductances
  MosfetLevel2::stamp(mna, DRAIN, gate, source, bulk, VGS, vds, vbs, params);
}

/** @test */
TEST(MosfetLevel2Test, StampLinear) {
  MnaSystem mna(5);
  MosfetLevel2Params params;
  const NetID DRAIN = 1, gate = 2, source = 3, bulk = 4;
  const double VGS = 2.0, vds = 0.5, vbs = 0.0;

  MosfetLevel2::stamp(mna, DRAIN, gate, source, bulk, VGS, vds, vbs, params);
}

/** @test */
TEST(MosfetLevel2Test, StampWithBodyBias) {
  MnaSystem mna(5);
  MosfetLevel2Params params;
  const NetID DRAIN = 1, gate = 2, source = 3, bulk = 4;
  const double VGS = 2.0, vds = 3.0, vbs = -1.0;

  MosfetLevel2::stamp(mna, DRAIN, gate, source, bulk, VGS, vds, vbs, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(MosfetLevel2Test, GeometryScaling) {
  MosfetLevel2Params small{.W = 10e-6, .L = 1e-6};
  MosfetLevel2Params large{.W = 20e-6, .L = 1e-6}; // Double width
  const double VGS = 2.0, vds = 3.0, vbs = 0.0;

  const double ID_SMALL = MosfetLevel2::current(VGS, vds, vbs, small);
  const double ID_LARGE = MosfetLevel2::current(VGS, vds, vbs, large);

  // Doubling W doubles current (for fixed L)
  EXPECT_NEAR(ID_LARGE / ID_SMALL, 2.0, 0.1);
}

/** @test */
TEST(MosfetLevel2Test, ChannelLengthModulation) {
  MosfetLevel2Params params{.lambda = 0.02};
  const double VGS = 2.0, vbs = 0.0;

  const double ID_LOW = MosfetLevel2::current(VGS, 3.0, vbs, params);
  const double ID_HIGH = MosfetLevel2::current(VGS, 5.0, vbs, params);

  // In saturation, higher Vds increases current (CLM)
  EXPECT_GT(ID_HIGH, ID_LOW);
}

/** @test */
TEST(MosfetLevel2Test, MobilityDegradationEffect) {
  MosfetLevel2Params no_deg{.theta = 0.0};
  MosfetLevel2Params with_deg{.theta = 0.2};
  const double VGS = 3.0, vds = 5.0, vbs = 0.0;

  const double ID_NO_DEG = MosfetLevel2::current(VGS, vds, vbs, no_deg);
  const double ID_WITH_DEG = MosfetLevel2::current(VGS, vds, vbs, with_deg);

  // Mobility degradation reduces current
  EXPECT_LT(ID_WITH_DEG, ID_NO_DEG);
}

/** @test */
TEST(MosfetLevel2Test, VelocitySaturationEffect) {
  MosfetLevel2Params high_field{.E_crit = 1e5}; // Low critical field (more saturation)
  MosfetLevel2Params low_field{.E_crit = 1e7};  // High critical field (less saturation)
  const double VGS = 3.0, vds = 5.0, vbs = 0.0;

  const double VDSAT_HIGH = MosfetLevel2::drainSaturationVoltage(VGS, 0.7, high_field);
  const double VDSAT_LOW = MosfetLevel2::drainSaturationVoltage(VGS, 0.7, low_field);

  // Lower E_crit causes stronger velocity saturation (lower Vdsat)
  EXPECT_LT(VDSAT_HIGH, VDSAT_LOW);
}

/** @test */
TEST(MosfetLevel2Test, SubthresholdConduction) {
  MosfetLevel2Params params;
  const double VGS = 0.5; // Below threshold
  const double VDS = 1.0, vbs = 0.0;

  const double ID = MosfetLevel2::current(VGS, VDS, vbs, params);

  // Should have weak inversion current (exponential in Vgs)
  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-8); // Subthreshold (nA range)
}

/** @test */
TEST(MosfetLevel2Test, BodyEffectStrength) {
  MosfetLevel2Params weak{.gamma = 0.1};
  MosfetLevel2Params strong{.gamma = 0.8};
  const double VBS = -1.0;

  const double VTH_WEAK = MosfetLevel2::thresholdVoltage(VBS, weak);
  const double VTH_STRONG = MosfetLevel2::thresholdVoltage(VBS, strong);

  // Stronger body effect -> larger threshold shift
  EXPECT_GT(VTH_STRONG - strong.Vth0, VTH_WEAK - weak.Vth0);
}
