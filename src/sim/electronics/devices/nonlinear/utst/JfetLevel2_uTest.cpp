/**
 * @file JfetLevel2_uTest.cpp
 * @brief Unit tests for JfetLevel2.
 */

#include "src/sim/electronics/devices/nonlinear/inc/JfetLevel2.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::JfetLevel2;
using sim::electronics::devices::nonlinear::JfetLevel2Params;
using sim::electronics::devices::nonlinear::MnaSystem;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(JfetLevel2Test, DefaultParameters) {
  JfetLevel2Params params;
  EXPECT_DOUBLE_EQ(params.Beta, 1e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -2.0);
  EXPECT_DOUBLE_EQ(params.lambda, 0.01);
  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
}

/** @test */
TEST(JfetLevel2Test, CustomParameters) {
  JfetLevel2Params params{.Beta = 2e-3, .Vp = -2.5, .lambda = 0.02, .Is = 1e-13};
  EXPECT_DOUBLE_EQ(params.Beta, 2e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -2.5);
  EXPECT_DOUBLE_EQ(params.lambda, 0.02);
  EXPECT_DOUBLE_EQ(params.Is, 1e-13);
}

/* ----------------------------- Drain Current Tests ----------------------------- */

/** @test */
TEST(JfetLevel2Test, DrainCurrentCutoff) {
  JfetLevel2Params params{.Vp = -2.0};
  const double VGS = -2.5; // Below pinch-off
  const double VDS = 5.0;
  const double ID = JfetLevel2::drainCurrent(VGS, VDS, params);

  // Subthreshold (non-zero but very small)
  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-9);
}

/** @test */
TEST(JfetLevel2Test, GateCurrentReverseBias) {
  JfetLevel2Params params{.Is = 1e-14};
  const double VGS = -1.0; // Reverse bias (normal for N-channel)
  const double IG = JfetLevel2::gateCurrent(VGS, params);

  // Reverse leakage (small negative)
  EXPECT_LT(IG, 0.0);
  EXPECT_GT(IG, -params.Is * 2.0);
}

/** @test */
TEST(JfetLevel2Test, GateCurrentZeroBias) {
  JfetLevel2Params params;
  const double VGS = 0.0;
  const double IG = JfetLevel2::gateCurrent(VGS, params);

  EXPECT_NEAR(IG, 0.0, params.Is);
}

/** @test */
TEST(JfetLevel2Test, GateCurrentForwardBias) {
  JfetLevel2Params params{.Is = 1e-14};
  const double VGS = 0.6; // Forward bias (unusual, but possible)
  const double IG = JfetLevel2::gateCurrent(VGS, params);

  // Forward current (exponential)
  EXPECT_GT(IG, 1e-6);
}

/* ----------------------------- Transconductance Tests ----------------------------- */

/** @test */
TEST(JfetLevel2Test, TransconductanceCutoff) {
  JfetLevel2Params params{.Vp = -2.0};
  const double VGS = -2.5;
  const double VDS = 5.0;
  const double GM = JfetLevel2::transconductance(VGS, VDS, params);

  // Small in cutoff (subthreshold)
  EXPECT_GT(GM, 0.0);
  EXPECT_LT(GM, 1e-6);
}

/** @test */
TEST(JfetLevel2Test, TransconductanceSaturation) {
  JfetLevel2Params params;
  const double VGS = -0.5;
  const double VDS = 5.0;
  const double GM = JfetLevel2::transconductance(VGS, VDS, params);

  EXPECT_GT(GM, 1e-4);
}

/** @test */
TEST(JfetLevel2Test, OutputConductanceSaturation) {
  JfetLevel2Params params{.lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 5.0;
  const double GDS = JfetLevel2::outputConductance(VGS, VDS, params);

  // Non-zero due to channel-length modulation
  EXPECT_GT(GDS, 0.0);
  EXPECT_LT(GDS, 1e-4);
}

/** @test */
TEST(JfetLevel2Test, GateConductance) {
  JfetLevel2Params params{.Is = 1e-14};
  const double VGS = -1.0;
  const double GG = JfetLevel2::gateConductance(VGS, params);

  EXPECT_GT(GG, 0.0);
  EXPECT_LT(GG, 1e-9); // Very small for reverse bias
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(JfetLevel2Test, TransconductanceNumericalDerivative) {
  JfetLevel2Params params;
  const double VGS = -0.5, vds = 5.0;
  const double DV = 1e-8;

  const double GM = JfetLevel2::transconductance(VGS, vds, params);

  const double I1 = JfetLevel2::drainCurrent(VGS - DV, vds, params);
  const double I2 = JfetLevel2::drainCurrent(VGS + DV, vds, params);
  const double GM_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GM, GM_NUMERICAL, std::abs(GM_NUMERICAL) * 0.01);
}

/** @test */
TEST(JfetLevel2Test, OutputConductanceNumericalDerivative) {
  JfetLevel2Params params;
  const double VGS = -0.5, vds = 5.0;
  const double DV = 1e-8;

  const double GDS = JfetLevel2::outputConductance(VGS, vds, params);

  const double I1 = JfetLevel2::drainCurrent(VGS, vds - DV, params);
  const double I2 = JfetLevel2::drainCurrent(VGS, vds + DV, params);
  const double GDS_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GDS, GDS_NUMERICAL, std::abs(GDS_NUMERICAL) * 0.01);
}

/** @test */
TEST(JfetLevel2Test, GateConductanceNumericalDerivative) {
  JfetLevel2Params params;
  const double VGS = 0.5; // Forward bias for measurable conductance
  const double DV = 1e-8;

  const double GG = JfetLevel2::gateConductance(VGS, params);

  const double I1 = JfetLevel2::gateCurrent(VGS - DV, params);
  const double I2 = JfetLevel2::gateCurrent(VGS + DV, params);
  const double GG_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(GG, GG_NUMERICAL, std::max(std::abs(GG_NUMERICAL) * 0.01, 1e-20));
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(JfetLevel2Test, StampIsLinearizationAtOperatingPoint) {
  const JfetLevel2Params PARAMS;

  struct Bias {
    double vgs;
    double vds;
    const char* region;
  };
  struct Step {
    double gate;
    double drain;
    double source;
  };

  // The source sits off ground so every stamped entry takes part.
  const double V_SOURCE = 0.8;
  const double DV = 0.05;

  for (const Bias& bias :
       {Bias{-3.0, 5.0, "cutoff"}, Bias{-0.5, 0.5, "linear"}, Bias{-0.5, 5.0, "saturation"}}) {
    const double ID = JfetLevel2::drainCurrent(bias.vgs, bias.vds, PARAMS);
    const double GM = JfetLevel2::transconductance(bias.vgs, bias.vds, PARAMS);
    const double GDS = JfetLevel2::outputConductance(bias.vgs, bias.vds, PARAMS);
    const double IG = JfetLevel2::gateCurrent(bias.vgs, PARAMS);
    const double GG = JfetLevel2::gateConductance(bias.vgs, PARAMS);

    // Terminal order: drain, gate, source.
    const auto STAMP = [&](MnaSystem& mna) {
      JfetLevel2::stamp(mna, terminalNet(0), terminalNet(1), terminalNet(2), bias.vgs, bias.vds,
                        PARAMS);
    };

    for (const Step& step :
         {Step{0.0, 0.0, 0.0}, Step{DV, 0.0, 0.0}, Step{0.0, DV, 0.0}, Step{0.0, 0.0, DV}}) {
      const auto PINNED = pinTerminals({V_SOURCE + bias.vds + step.drain,
                                        V_SOURCE + bias.vgs + step.gate, V_SOURCE + step.source},
                                       STAMP);
      ASSERT_TRUE(PINNED.solved) << bias.region;

      const double D_VGS = step.gate - step.source;
      const double D_VDS = step.drain - step.source;
      const double I_DRAIN = ID + GM * D_VGS + GDS * D_VDS;
      expectCurrent(PINNED.into[0], I_DRAIN, "drain");
      const double I_GATE = IG + GG * D_VGS;
      expectCurrent(PINNED.into[1], I_GATE, "gate");
      expectCurrent(PINNED.into[2], -I_DRAIN - I_GATE, "source");
    }
  }
}

/** @test Gate junction current, conductance and the stamp stay finite at a large gate bias */
TEST(JfetLevel2Test, FiniteAtLargeGateBias) {
  const JfetLevel2Params PARAMS;

  for (const double VGS : {20.0, 100.0, -100.0}) {
    EXPECT_TRUE(std::isfinite(JfetLevel2::gateCurrent(VGS, PARAMS))) << "vgs = " << VGS;
    EXPECT_TRUE(std::isfinite(JfetLevel2::gateConductance(VGS, PARAMS))) << "vgs = " << VGS;

    // Terminal order: drain, gate, source.
    const auto PINNED = pinTerminals({5.0, VGS, 0.0}, [&](MnaSystem& mna) {
      JfetLevel2::stamp(mna, terminalNet(0), terminalNet(1), terminalNet(2), VGS, 5.0, PARAMS);
    });
    ASSERT_TRUE(PINNED.solved) << "vgs = " << VGS;
    for (const double CURRENT : PINNED.into) {
      EXPECT_TRUE(std::isfinite(CURRENT)) << "vgs = " << VGS;
    }
  }
}

/* ----------------------------- Region Boundaries ----------------------------- */

/** @test Above pinch-off the drain current is the square law in both regions */
TEST(JfetLevel2Test, DrainCurrentAbovePinchOffMatchesReference) {
  struct Row {
    double vgs;
    double vds;
    double id;
  };

  // Saturation: Beta*Vgst^2*(1 + lambda*Vds). Linear region:
  // 2*Beta*(Vgst*Vds - Vds^2/2)*(1 + lambda*Vds). Vgst = Vgs - Vp.
  const JfetLevel2Params PARAMS;
  const std::array<Row, 5> ROWS = {{
      Row{-0.5, 5.0, 0.0023625},
      Row{-0.5, 0.5, 0.00125625},
      Row{0.0, 1.0, 0.00303},
      Row{0.0, 3.0, 0.00412},
      Row{-1.95, 5.0, 2.625e-06},
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(JfetLevel2::drainCurrent(row.vgs, row.vds, PARAMS), row.id, 1e-12 * row.id)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
  }

  const JfetLevel2Params OTHER{.Beta = 2e-3, .Vp = -2.5, .lambda = 0.02};
  EXPECT_NEAR(JfetLevel2::drainCurrent(-1.0, 4.0, OTHER), 0.00486, 1e-12 * 0.00486);
}

/** @test Drain current is continuous where the linear region meets saturation */
TEST(JfetLevel2Test, ContinuousAtSaturationVoltage) {
  const JfetLevel2Params PARAMS;
  constexpr double EPS = 1e-9;

  for (const double VGS : {-1.5, -1.0, -0.5, 0.0}) {
    const double VDSAT = VGS - PARAMS.Vp;
    const double BELOW = JfetLevel2::drainCurrent(VGS, VDSAT - EPS, PARAMS);
    const double ABOVE = JfetLevel2::drainCurrent(VGS, VDSAT + EPS, PARAMS);
    EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE) << "vgs = " << VGS;
  }
}

/** @test Above pinch-off the drain current never falls as the drain voltage rises */
TEST(JfetLevel2Test, MonotonicInDrainVoltage) {
  const JfetLevel2Params PARAMS;

  for (const double VGS : {-1.5, -1.0, -0.5, 0.0}) {
    double previous = JfetLevel2::drainCurrent(VGS, 0.0, PARAMS);
    for (int step = 1; step <= 500; ++step) {
      const double VDS = 0.01 * step;
      const double ID = JfetLevel2::drainCurrent(VGS, VDS, PARAMS);
      ASSERT_GE(ID, previous) << "vgs = " << VGS << ", vds = " << VDS;
      previous = ID;
    }
  }
}

/** @test Output conductance is finite and not negative on the saturation boundary */
TEST(JfetLevel2Test, OutputConductanceNotNegativeAtSaturationVoltage) {
  const JfetLevel2Params PARAMS;

  for (const double VGS : {-1.5, -0.5, 0.0}) {
    // On the boundary itself the conductance is a difference taken across it.
    const double VDSAT = VGS - PARAMS.Vp;
    for (const double VDS : {VDSAT - 5e-9, VDSAT, VDSAT + 5e-9}) {
      const double GDS = JfetLevel2::outputConductance(VGS, VDS, PARAMS);
      EXPECT_TRUE(std::isfinite(GDS)) << "vgs = " << VGS;
      EXPECT_GE(GDS, 0.0) << "vgs = " << VGS;
    }
  }
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(JfetLevel2Test, ChannelLengthModulation) {
  JfetLevel2Params params{.lambda = 0.02};
  const double VGS = -0.5;

  const double ID_LOW = JfetLevel2::drainCurrent(VGS, 3.0, params);
  const double ID_HIGH = JfetLevel2::drainCurrent(VGS, 6.0, params);

  // Higher Vds increases current in saturation (CLM)
  EXPECT_GT(ID_HIGH, ID_LOW);
}

/** @test */
TEST(JfetLevel2Test, SubthresholdConduction) {
  JfetLevel2Params params{.Vp = -2.0, .n_sub = 1.5};
  const double VGS = -2.2; // Slightly below pinch-off
  const double VDS = 3.0;

  const double ID = JfetLevel2::drainCurrent(VGS, VDS, params);

  // Subthreshold current (exponential in Vgs)
  EXPECT_GT(ID, 0.0);
  EXPECT_LT(ID, 1e-7); // Subthreshold range (nA to sub-nA)
}

/** @test */
TEST(JfetLevel2Test, GateLeakageIncreasesWithForwardBias) {
  JfetLevel2Params params;

  const double IG_REVERSE = JfetLevel2::gateCurrent(-1.0, params);
  const double IG_LESS_REVERSE = JfetLevel2::gateCurrent(-0.5, params);

  // Less reverse bias -> larger (less negative) leakage
  EXPECT_GT(IG_LESS_REVERSE, IG_REVERSE);
}

/** @test */
TEST(JfetLevel2Test, TransconductanceIncreasesWithVgs) {
  JfetLevel2Params params;
  const double VDS = 5.0;

  const double GM_LOW = JfetLevel2::transconductance(-1.0, VDS, params);
  const double GM_HIGH = JfetLevel2::transconductance(-0.3, VDS, params);

  // Higher Vgs (less negative) -> higher gm
  EXPECT_GT(GM_HIGH, GM_LOW);
}
