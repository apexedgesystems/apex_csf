/**
 * @file MosfetLevel3_uTest.cpp
 * @brief Unit tests for MosfetLevel3.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel3.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetLevel3;
using sim::electronics::devices::nonlinear::MosfetLevel3Params;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(MosfetLevel3, DefaultParameters) {
  MosfetLevel3Params params;
  EXPECT_DOUBLE_EQ(params.Kp, 200e-6);
  EXPECT_DOUBLE_EQ(params.Vth0, 0.5);
  EXPECT_DOUBLE_EQ(params.lambda, 0.05);
  EXPECT_DOUBLE_EQ(params.W, 1e-6);
  EXPECT_DOUBLE_EQ(params.L, 0.18e-6);
  EXPECT_DOUBLE_EQ(params.eta, 0.05);
  EXPECT_DOUBLE_EQ(params.kappa, 0.1);
  EXPECT_DOUBLE_EQ(params.vsat, 1e5);
}

/** @test */
TEST(MosfetLevel3, CustomParameters) {
  MosfetLevel3Params params{
      .Kp = 400e-6, .Vth0 = 0.4, .lambda = 0.1, .W = 0.5e-6, .L = 0.13e-6, .eta = 0.1};
  EXPECT_DOUBLE_EQ(params.Kp, 400e-6);
  EXPECT_DOUBLE_EQ(params.Vth0, 0.4);
  EXPECT_DOUBLE_EQ(params.eta, 0.1);
}

/* ----------------------------- Effective Dimensions Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3, EffectiveDimensionsNoDeltas) {
  MosfetLevel3Params params{.W = 1e-6, .L = 0.18e-6, .delta_w = 0.0, .delta_l = 0.0};
  const auto [W_eff, L_eff] = MosfetLevel3::effectiveDimensions(params.W, params.L, params);

  EXPECT_DOUBLE_EQ(W_eff, params.W);
  EXPECT_DOUBLE_EQ(L_eff, params.L);
}

/** @test */
TEST(MosfetLevel3, EffectiveDimensionsWithDeltas) {
  MosfetLevel3Params params{.W = 1e-6, .L = 0.18e-6, .delta_w = 0.05e-6, .delta_l = 0.02e-6};
  const auto [W_eff, L_eff] = MosfetLevel3::effectiveDimensions(params.W, params.L, params);

  EXPECT_DOUBLE_EQ(W_eff, 0.95e-6);
  EXPECT_DOUBLE_EQ(L_eff, 0.16e-6);
}

/* ----------------------------- DIBL Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3, ThresholdVoltageDIBL) {
  MosfetLevel3Params params{.Vth0 = 0.5, .eta = 0.1};

  const double vth_low_vds = MosfetLevel3::thresholdVoltage(0.5, 0.0, params);
  const double vth_high_vds = MosfetLevel3::thresholdVoltage(2.0, 0.0, params);

  // DIBL reduces threshold at high Vds
  EXPECT_LT(vth_high_vds, vth_low_vds);
}

/** @test */
TEST(MosfetLevel3, ThresholdVoltageBodyEffect) {
  MosfetLevel3Params params{.Vth0 = 0.5, .gamma = 0.4, .eta = 0.0};

  const double vth_no_bias = MosfetLevel3::thresholdVoltage(1.0, 0.0, params);
  const double vth_reverse = MosfetLevel3::thresholdVoltage(1.0, -1.0, params);

  // Body effect increases threshold
  EXPECT_GT(vth_reverse, vth_no_bias);
}

/* ----------------------------- Saturation Voltage Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3, DrainSaturationVoltageCutoff) {
  MosfetLevel3Params params;
  const double vgs = 0.0;
  const double vth = 0.5;
  const double vdsat = MosfetLevel3::drainSaturationVoltage(vgs, vth, params);

  EXPECT_DOUBLE_EQ(vdsat, 0.0);
}

/** @test */
TEST(MosfetLevel3, DrainSaturationVoltageActive) {
  MosfetLevel3Params params{.L = 0.18e-6, .kappa = 0.1, .vsat = 1e5};
  const double vgs = 1.5;
  const double vth = 0.5;
  const double vdsat = MosfetLevel3::drainSaturationVoltage(vgs, vth, params);

  const double vgst = vgs - vth;
  EXPECT_GT(vdsat, 0.0);
  EXPECT_LT(vdsat, vgst); // Velocity saturation reduces below ideal
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3, CurrentCutoff) {
  MosfetLevel3Params params;
  const double vgs = 0.0, vds = 1.0, vbs = 0.0;
  const double id = MosfetLevel3::current(vgs, vds, vbs, params);

  // Subthreshold current (non-zero but very small)
  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-9);
}

/** @test */
TEST(MosfetLevel3, CurrentLinear) {
  MosfetLevel3Params params;
  const double vgs = 1.5, vds = 0.3, vbs = 0.0;
  const double id = MosfetLevel3::current(vgs, vds, vbs, params);

  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-3);
}

/** @test */
TEST(MosfetLevel3, CurrentSaturation) {
  MosfetLevel3Params params;
  const double vgs = 1.5, vds = 2.0, vbs = 0.0;
  const double id = MosfetLevel3::current(vgs, vds, vbs, params);

  EXPECT_GT(id, 0.0);
  EXPECT_LT(id, 1e-3);
}

/** @test */
TEST(MosfetLevel3, CurrentDIBLEffect) {
  MosfetLevel3Params params{.eta = 0.1};
  const double vgs = 1.2, vbs = 0.0;

  const double id_low = MosfetLevel3::current(vgs, 0.5, vbs, params);
  const double id_high = MosfetLevel3::current(vgs, 2.0, vbs, params);

  // DIBL reduces Vth at high Vds, increasing current
  EXPECT_GT(id_high, id_low);
}

/* ----------------------------- Transconductance Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3, TransconductanceSaturation) {
  MosfetLevel3Params params{.eta = 0.02}; // Reduced DIBL for stability
  const double vgs = 1.2, vds = 1.5, vbs = 0.0;
  const double gm = MosfetLevel3::transconductance(vgs, vds, vbs, params);

  EXPECT_GT(gm, 1e-6);
}

/** @test */
TEST(MosfetLevel3, OutputConductanceSaturation) {
  MosfetLevel3Params params{.lambda = 0.05, .eta = 0.02};
  const double vgs = 1.2, vds = 1.5, vbs = 0.0;
  const double gds = MosfetLevel3::outputConductance(vgs, vds, vbs, params);

  // Non-zero due to CLM + DIBL
  EXPECT_GT(gds, 0.0);
  EXPECT_LT(gds, 1e-3);
}

/** @test */
TEST(MosfetLevel3, BulkTransconductance) {
  MosfetLevel3Params params{.gamma = 0.4, .eta = 0.02};
  const double vgs = 1.2, vds = 1.2, vbs = -0.3;
  const double gmb = MosfetLevel3::bulkTransconductance(vgs, vds, vbs, params);

  EXPECT_GT(gmb, 0.0);
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(MosfetLevel3, TransconductanceNumericalDerivative) {
  MosfetLevel3Params params{.eta = 0.02};
  const double vgs = 1.2, vds = 1.2, vbs = 0.0;
  const double dv = 1e-8;

  const double gm = MosfetLevel3::transconductance(vgs, vds, vbs, params);

  const double i1 = MosfetLevel3::current(vgs - dv, vds, vbs, params);
  const double i2 = MosfetLevel3::current(vgs + dv, vds, vbs, params);
  const double gmNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gm, gmNumerical, std::abs(gmNumerical) * 0.01);
}

/** @test */
TEST(MosfetLevel3, OutputConductanceNumericalDerivative) {
  MosfetLevel3Params params{.eta = 0.02};
  const double vgs = 1.2, vds = 1.2, vbs = 0.0;
  const double dv = 1e-8;

  const double gds = MosfetLevel3::outputConductance(vgs, vds, vbs, params);

  const double i1 = MosfetLevel3::current(vgs, vds - dv, vbs, params);
  const double i2 = MosfetLevel3::current(vgs, vds + dv, vbs, params);
  const double gdsNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gds, gdsNumerical, std::abs(gdsNumerical) * 0.01);
}

/** @test */
TEST(MosfetLevel3, BulkTransconductanceNumericalDerivative) {
  MosfetLevel3Params params{.eta = 0.02};
  const double vgs = 1.2, vds = 1.2, vbs = -0.3;
  const double dv = 1e-8;

  const double gmb = MosfetLevel3::bulkTransconductance(vgs, vds, vbs, params);

  const double i1 = MosfetLevel3::current(vgs, vds, vbs - dv, params);
  const double i2 = MosfetLevel3::current(vgs, vds, vbs + dv, params);
  const double gmbNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gmb, gmbNumerical, std::abs(gmbNumerical) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(MosfetLevel3, StampSaturation) {
  MnaSystem mna(5);
  MosfetLevel3Params params{.eta = 0.02};
  const NetID drain = 1, gate = 2, source = 3, bulk = 4;
  const double vgs = 1.2, vds = 1.5, vbs = 0.0;

  MosfetLevel3::stamp(mna, drain, gate, source, bulk, vgs, vds, vbs, params);
}

/** @test */
TEST(MosfetLevel3, StampLinear) {
  MnaSystem mna(5);
  MosfetLevel3Params params{.eta = 0.02};
  const NetID drain = 1, gate = 2, source = 3, bulk = 4;
  const double vgs = 1.2, vds = 0.3, vbs = 0.0;

  MosfetLevel3::stamp(mna, drain, gate, source, bulk, vgs, vds, vbs, params);
}

/** @test */
TEST(MosfetLevel3, StampWithBodyBias) {
  MnaSystem mna(5);
  MosfetLevel3Params params{.eta = 0.02};
  const NetID drain = 1, gate = 2, source = 3, bulk = 4;
  const double vgs = 1.2, vds = 1.2, vbs = -0.5;

  MosfetLevel3::stamp(mna, drain, gate, source, bulk, vgs, vds, vbs, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(MosfetLevel3, DIBLStrength) {
  MosfetLevel3Params weak{.Vth0 = 0.5, .eta = 0.01};
  MosfetLevel3Params strong{.Vth0 = 0.5, .eta = 0.15};

  const double vth_weak = MosfetLevel3::thresholdVoltage(2.0, 0.0, weak);
  const double vth_strong = MosfetLevel3::thresholdVoltage(2.0, 0.0, strong);

  // Stronger DIBL -> larger threshold reduction
  EXPECT_LT(vth_strong, vth_weak);
}

/** @test */
TEST(MosfetLevel3, ShortChannelEffect) {
  MosfetLevel3Params long_channel{.L = 1e-6, .delta_l = 0.0};
  MosfetLevel3Params short_channel{.L = 0.18e-6, .delta_l = 0.02e-6};

  const double vgs = 1.5, vds = 1.5, vbs = 0.0;

  const double id_long = MosfetLevel3::current(vgs, vds, vbs, long_channel);
  const double id_short = MosfetLevel3::current(vgs, vds, vbs, short_channel);

  // Shorter effective channel -> higher current density
  const auto [W_long, L_long] =
      MosfetLevel3::effectiveDimensions(long_channel.W, long_channel.L, long_channel);
  const auto [W_short, L_short] =
      MosfetLevel3::effectiveDimensions(short_channel.W, short_channel.L, short_channel);

  const double current_density_long = id_long / (W_long / L_long);
  const double current_density_short = id_short / (W_short / L_short);

  EXPECT_GT(id_short, id_long);
}

/** @test */
TEST(MosfetLevel3, SaturationVoltageKappaParameter) {
  MosfetLevel3Params low_kappa{.kappa = 0.05};
  MosfetLevel3Params high_kappa{.kappa = 0.2};

  const double vgs = 2.0, vth = 0.5;

  const double vdsat_low = MosfetLevel3::drainSaturationVoltage(vgs, vth, low_kappa);
  const double vdsat_high = MosfetLevel3::drainSaturationVoltage(vgs, vth, high_kappa);

  // Higher kappa -> stronger reduction -> lower Vdsat
  EXPECT_LT(vdsat_high, vdsat_low);
}

/** @test */
TEST(MosfetLevel3, SubthresholdSlope) {
  MosfetLevel3Params params{.n_sub = 1.5};

  // Two voltages below threshold
  const double vgs1 = 0.3, vgs2 = 0.4;
  const double vds = 1.0, vbs = 0.0;

  const double id1 = MosfetLevel3::current(vgs1, vds, vbs, params);
  const double id2 = MosfetLevel3::current(vgs2, vds, vbs, params);

  // Subthreshold current is exponential in Vgs
  EXPECT_GT(id2, id1);
  const double ratio = id2 / id1;
  EXPECT_GT(ratio, 2.0); // Should increase significantly for 0.1V step
}
