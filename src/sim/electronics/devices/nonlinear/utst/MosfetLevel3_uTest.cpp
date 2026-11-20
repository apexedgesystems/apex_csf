/**
 * @file MosfetLevel3_uTest.cpp
 * @brief Unit tests for MosfetLevel3.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel3.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetLevel3;
using sim::electronics::devices::nonlinear::MosfetLevel3Params;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, DefaultParameters) {
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
TEST(MosfetLevel3Test, CustomParameters) {
  MosfetLevel3Params params{
      .Kp = 400e-6, .Vth0 = 0.4, .lambda = 0.1, .W = 0.5e-6, .L = 0.13e-6, .eta = 0.1};
  EXPECT_DOUBLE_EQ(params.Kp, 400e-6);
  EXPECT_DOUBLE_EQ(params.Vth0, 0.4);
  EXPECT_DOUBLE_EQ(params.eta, 0.1);
}

/* ----------------------------- Effective Dimensions Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, EffectiveDimensionsNoDeltas) {
  MosfetLevel3Params params{.W = 1e-6, .L = 0.18e-6, .delta_w = 0.0, .delta_l = 0.0};
  const auto [W_eff, L_eff] = MosfetLevel3::effectiveDimensions(params.W, params.L, params);

  EXPECT_DOUBLE_EQ(W_eff, params.W);
  EXPECT_DOUBLE_EQ(L_eff, params.L);
}

/** @test */
TEST(MosfetLevel3Test, EffectiveDimensionsWithDeltas) {
  MosfetLevel3Params params{.W = 1e-6, .L = 0.18e-6, .delta_w = 0.05e-6, .delta_l = 0.02e-6};
  const auto [W_eff, L_eff] = MosfetLevel3::effectiveDimensions(params.W, params.L, params);

  EXPECT_DOUBLE_EQ(W_eff, 0.95e-6);
  EXPECT_DOUBLE_EQ(L_eff, 0.16e-6);
}

/* ----------------------------- DIBL Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, ThresholdVoltageDIBL) {
  MosfetLevel3Params params{.Vth0 = 0.5, .eta = 0.1};

  const double VTH_LOW_VDS = MosfetLevel3::thresholdVoltage(0.5, 0.0, params);
  const double VTH_HIGH_VDS = MosfetLevel3::thresholdVoltage(2.0, 0.0, params);

  // DIBL reduces threshold at high Vds
  EXPECT_LT(VTH_HIGH_VDS, VTH_LOW_VDS);
}

/** @test */
TEST(MosfetLevel3Test, ThresholdVoltageBodyEffect) {
  MosfetLevel3Params params{.Vth0 = 0.5, .gamma = 0.4, .eta = 0.0};

  const double VTH_NO_BIAS = MosfetLevel3::thresholdVoltage(1.0, 0.0, params);
  const double VTH_REVERSE = MosfetLevel3::thresholdVoltage(1.0, -1.0, params);

  // Body effect increases threshold
  EXPECT_GT(VTH_REVERSE, VTH_NO_BIAS);
}

/* ----------------------------- Saturation Voltage Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, DrainSaturationVoltageCutoff) {
  MosfetLevel3Params params;
  const double VGS = 0.0;
  const double VTH = 0.5;
  const double VDSAT = MosfetLevel3::drainSaturationVoltage(VGS, VTH, params);

  EXPECT_DOUBLE_EQ(VDSAT, 0.0);
}

/** @test */
TEST(MosfetLevel3Test, DrainSaturationVoltageActive) {
  MosfetLevel3Params params{.L = 0.18e-6, .kappa = 0.1, .vsat = 1e5};
  const double VGS = 1.5;
  const double VTH = 0.5;
  const double VDSAT = MosfetLevel3::drainSaturationVoltage(VGS, VTH, params);

  const double VGST = VGS - VTH;
  EXPECT_GT(VDSAT, 0.0);
  EXPECT_LT(VDSAT, VGST); // Velocity saturation reduces below ideal
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, CurrentCutoff) {
  MosfetLevel3Params params;
  const double VGS = 0.0, vds = 1.0, vbs = 0.0;
  const double ID = MosfetLevel3::current(VGS, vds, vbs, params);

  // Subthreshold current (non-zero but very small)
  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-9);
}

/** @test */
TEST(MosfetLevel3Test, CurrentLinear) {
  MosfetLevel3Params params;
  const double VGS = 1.5, vds = 0.3, vbs = 0.0;
  const double ID = MosfetLevel3::current(VGS, vds, vbs, params);

  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-3);
}

/** @test */
TEST(MosfetLevel3Test, CurrentSaturation) {
  MosfetLevel3Params params;
  const double VGS = 1.5, vds = 2.0, vbs = 0.0;
  const double ID = MosfetLevel3::current(VGS, vds, vbs, params);

  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-3);
}

/** @test */
TEST(MosfetLevel3Test, CurrentDIBLEffect) {
  MosfetLevel3Params params{.eta = 0.1};
  const double VGS = 1.2, vbs = 0.0;

  const double ID_LOW = MosfetLevel3::current(VGS, 0.5, vbs, params);
  const double ID_HIGH = MosfetLevel3::current(VGS, 2.0, vbs, params);

  // DIBL reduces Vth at high Vds, increasing current
  EXPECT_GT(ID_HIGH, ID_LOW);
}

/* ----------------------------- Transconductance Tests ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, TransconductanceSaturation) {
  MosfetLevel3Params params{.eta = 0.02}; // Reduced DIBL for stability
  const double VGS = 1.2, vds = 1.5, vbs = 0.0;
  const double GM = MosfetLevel3::transconductance(VGS, vds, vbs, params);

  EXPECT_GT(GM, 1e-6);
}

/** @test */
TEST(MosfetLevel3Test, OutputConductanceSaturation) {
  MosfetLevel3Params params{.lambda = 0.05, .eta = 0.02};
  const double VGS = 1.2, vds = 1.5, vbs = 0.0;
  const double GDS = MosfetLevel3::outputConductance(VGS, vds, vbs, params);

  // Non-zero due to CLM + DIBL
  EXPECT_GT(GDS, 0.0);
  EXPECT_LT(GDS, 1e-3);
}

/** @test */
TEST(MosfetLevel3Test, BulkTransconductance) {
  MosfetLevel3Params params{.gamma = 0.4, .eta = 0.02};
  const double VGS = 1.2, vds = 1.2, vbs = -0.3;
  const double GMB = MosfetLevel3::bulkTransconductance(VGS, vds, vbs, params);

  EXPECT_GT(GMB, 0.0);
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, TransconductanceNumericalDerivative) {
  MosfetLevel3Params params{.eta = 0.02};
  const double VGS = 1.2, vds = 1.2, vbs = 0.0;
  const double DV = 1e-8;

  const double GM = MosfetLevel3::transconductance(VGS, vds, vbs, params);

  const double I1 = MosfetLevel3::current(VGS - DV, vds, vbs, params);
  const double I2 = MosfetLevel3::current(VGS + DV, vds, vbs, params);
  const double GM_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GM, GM_NUMERICAL, std::abs(GM_NUMERICAL) * 0.01);
}

/** @test */
TEST(MosfetLevel3Test, OutputConductanceNumericalDerivative) {
  MosfetLevel3Params params{.eta = 0.02};
  const double VGS = 1.2, vds = 1.2, vbs = 0.0;
  const double DV = 1e-8;

  const double GDS = MosfetLevel3::outputConductance(VGS, vds, vbs, params);

  const double I1 = MosfetLevel3::current(VGS, vds - DV, vbs, params);
  const double I2 = MosfetLevel3::current(VGS, vds + DV, vbs, params);
  const double GDS_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GDS, GDS_NUMERICAL, std::abs(GDS_NUMERICAL) * 0.01);
}

/** @test */
TEST(MosfetLevel3Test, BulkTransconductanceNumericalDerivative) {
  MosfetLevel3Params params{.eta = 0.02};
  const double VGS = 1.2, vds = 1.2, vbs = -0.3;
  const double DV = 1e-8;

  const double GMB = MosfetLevel3::bulkTransconductance(VGS, vds, vbs, params);

  const double I1 = MosfetLevel3::current(VGS, vds, vbs - DV, params);
  const double I2 = MosfetLevel3::current(VGS, vds, vbs + DV, params);
  const double GMB_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GMB, GMB_NUMERICAL, std::abs(GMB_NUMERICAL) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(MosfetLevel3Test, StampIsLinearizationAtOperatingPoint) {
  const MosfetLevel3Params PARAMS;

  struct Bias {
    double vgs;
    double vds;
    double vbs;
    const char* region;
  };
  struct Step {
    double gate;
    double drain;
    double source;
    double bulk;
  };

  // The source sits off ground so every stamped entry takes part.
  const double V_SOURCE = 0.8;
  const double DV = 0.05;

  for (const Bias& bias : {Bias{0.3, 1.0, 0.0, "subthreshold"}, Bias{2.0, 0.2, -0.5, "linear"},
                           Bias{2.0, 3.0, -0.5, "saturation"}}) {
    const double ID = MosfetLevel3::current(bias.vgs, bias.vds, bias.vbs, PARAMS);
    const double GM = MosfetLevel3::transconductance(bias.vgs, bias.vds, bias.vbs, PARAMS);
    const double GDS = MosfetLevel3::outputConductance(bias.vgs, bias.vds, bias.vbs, PARAMS);
    const double GMB = MosfetLevel3::bulkTransconductance(bias.vgs, bias.vds, bias.vbs, PARAMS);

    // Terminal order: drain, gate, source, bulk.
    const auto STAMP = [&](MnaSystem& mna) {
      MosfetLevel3::stamp(mna, terminalNet(0), terminalNet(1), terminalNet(2), terminalNet(3),
                          bias.vgs, bias.vds, bias.vbs, PARAMS);
    };

    for (const Step& step :
         {Step{0.0, 0.0, 0.0, 0.0}, Step{DV, 0.0, 0.0, 0.0}, Step{0.0, DV, 0.0, 0.0},
          Step{0.0, 0.0, DV, 0.0}, Step{0.0, 0.0, 0.0, DV}}) {
      const auto PINNED =
          pinTerminals({V_SOURCE + bias.vds + step.drain, V_SOURCE + bias.vgs + step.gate,
                        V_SOURCE + step.source, V_SOURCE + bias.vbs + step.bulk},
                       STAMP);
      ASSERT_TRUE(PINNED.solved) << bias.region;

      const double D_VGS = step.gate - step.source;
      const double D_VDS = step.drain - step.source;
      const double D_VBS = step.bulk - step.source;
      const double I_DRAIN = ID + GM * D_VGS + GDS * D_VDS + GMB * D_VBS;
      expectCurrent(PINNED.into[0], I_DRAIN, "drain");
      expectCurrent(PINNED.into[1], 0.0, "gate");
      expectCurrent(PINNED.into[2], -I_DRAIN, "source");
      expectCurrent(PINNED.into[3], 0.0, "bulk");
    }
  }
}

/* ----------------------------- Region Boundaries ----------------------------- */

/** @test Drain current is continuous where the linear region meets saturation */
TEST(MosfetLevel3Test, ContinuousAtSaturationVoltage) {
  const MosfetLevel3Params PARAMS;
  constexpr double EPS = 1e-9;

  for (const double VGS : {1.0, 1.5, 2.0, 3.0}) {
    for (const double VBS : {0.0, -0.5}) {
      // The threshold moves with the drain voltage, so the boundary is the
      // drain voltage that equals its own saturation voltage.
      double vdsat = 1.0;
      for (int iter = 0; iter < 50; ++iter) {
        const double VTH = MosfetLevel3::thresholdVoltage(vdsat, VBS, PARAMS);
        vdsat = MosfetLevel3::drainSaturationVoltage(VGS, VTH, PARAMS);
      }
      ASSERT_GT(vdsat, 0.0) << "vgs = " << VGS;

      const double BELOW = MosfetLevel3::current(VGS, vdsat - EPS, VBS, PARAMS);
      const double ABOVE = MosfetLevel3::current(VGS, vdsat + EPS, VBS, PARAMS);
      EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE) << "vgs = " << VGS << ", vbs = " << VBS;
    }
  }
}

/** @test Above threshold the drain current never falls as the drain voltage rises */
TEST(MosfetLevel3Test, MonotonicInDrainVoltage) {
  const MosfetLevel3Params PARAMS;

  for (const double VGS : {1.0, 1.5, 2.0, 3.0}) {
    double previous = MosfetLevel3::current(VGS, 0.0, 0.0, PARAMS);
    for (int step = 1; step <= 500; ++step) {
      const double VDS = 0.01 * step;
      const double ID = MosfetLevel3::current(VGS, VDS, 0.0, PARAMS);
      ASSERT_GE(ID, previous) << "vgs = " << VGS << ", vds = " << VDS;
      previous = ID;
    }
  }
}

/** @test Output conductance is finite and not negative through saturation, the boundary included */
TEST(MosfetLevel3Test, OutputConductanceNotNegativeThroughSaturation) {
  const MosfetLevel3Params PARAMS;

  for (const double VGS : {1.0, 2.0, 3.0}) {
    // On the boundary itself the conductance is a difference taken across it.
    double vdsat = 1.0;
    for (int iter = 0; iter < 50; ++iter) {
      const double VTH = MosfetLevel3::thresholdVoltage(vdsat, 0.0, PARAMS);
      vdsat = MosfetLevel3::drainSaturationVoltage(VGS, VTH, PARAMS);
    }
    for (const double VDS : {vdsat - 5e-9, vdsat, vdsat + 5e-9}) {
      const double GDS = MosfetLevel3::outputConductance(VGS, VDS, 0.0, PARAMS);
      EXPECT_TRUE(std::isfinite(GDS)) << "vgs = " << VGS << ", at the boundary";
      EXPECT_GE(GDS, 0.0) << "vgs = " << VGS << ", at the boundary";
    }

    for (int step = 0; step <= 500; ++step) {
      const double VDS = 0.01 * step;
      const double GDS = MosfetLevel3::outputConductance(VGS, VDS, 0.0, PARAMS);
      ASSERT_TRUE(std::isfinite(GDS)) << "vgs = " << VGS << ", vds = " << VDS;
      ASSERT_GE(GDS, 0.0) << "vgs = " << VGS << ", vds = " << VDS;
    }
  }
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(MosfetLevel3Test, DIBLStrength) {
  MosfetLevel3Params weak{.Vth0 = 0.5, .eta = 0.01};
  MosfetLevel3Params strong{.Vth0 = 0.5, .eta = 0.15};

  const double VTH_WEAK = MosfetLevel3::thresholdVoltage(2.0, 0.0, weak);
  const double VTH_STRONG = MosfetLevel3::thresholdVoltage(2.0, 0.0, strong);

  // Stronger DIBL -> larger threshold reduction
  EXPECT_LT(VTH_STRONG, VTH_WEAK);
}

/** @test */
TEST(MosfetLevel3Test, ShortChannelEffect) {
  MosfetLevel3Params long_channel{.L = 1e-6, .delta_l = 0.0};
  MosfetLevel3Params short_channel{.L = 0.18e-6, .delta_l = 0.02e-6};

  const double VGS = 1.5, vds = 1.5, vbs = 0.0;

  const double ID_LONG = MosfetLevel3::current(VGS, vds, vbs, long_channel);
  const double ID_SHORT = MosfetLevel3::current(VGS, vds, vbs, short_channel);

  EXPECT_GT(ID_SHORT, ID_LONG);
}

/** @test */
TEST(MosfetLevel3Test, SaturationVoltageKappaParameter) {
  MosfetLevel3Params low_kappa{.kappa = 0.05};
  MosfetLevel3Params high_kappa{.kappa = 0.2};

  const double VGS = 2.0, vth = 0.5;

  const double VDSAT_LOW = MosfetLevel3::drainSaturationVoltage(VGS, vth, low_kappa);
  const double VDSAT_HIGH = MosfetLevel3::drainSaturationVoltage(VGS, vth, high_kappa);

  // Higher kappa -> stronger reduction -> lower Vdsat
  EXPECT_LT(VDSAT_HIGH, VDSAT_LOW);
}

/** @test */
TEST(MosfetLevel3Test, SubthresholdSlope) {
  MosfetLevel3Params params{.n_sub = 1.5};

  // Two voltages below threshold
  const double VGS1 = 0.3, vgs2 = 0.4;
  const double VDS = 1.0, vbs = 0.0;

  const double ID1 = MosfetLevel3::current(VGS1, VDS, vbs, params);
  const double ID2 = MosfetLevel3::current(vgs2, VDS, vbs, params);

  // Subthreshold current is exponential in Vgs
  EXPECT_GT(ID2, ID1);
  const double RATIO = ID2 / ID1;
  EXPECT_GT(RATIO, 2.0); // Should increase significantly for 0.1V step
}
