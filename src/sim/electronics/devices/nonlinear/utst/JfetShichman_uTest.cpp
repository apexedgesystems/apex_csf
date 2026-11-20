/**
 * @file JfetShichman_uTest.cpp
 * @brief Unit tests for JfetShichman.
 */

#include "src/sim/electronics/devices/nonlinear/inc/JfetShichman.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::JfetShichman;
using sim::electronics::devices::nonlinear::JfetShichmanParams;
using sim::electronics::devices::nonlinear::MnaSystem;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(JfetShichmanTest, DefaultParameters) {
  JfetShichmanParams params;
  EXPECT_DOUBLE_EQ(params.Beta, 1e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -2.0);
  EXPECT_DOUBLE_EQ(params.lambda, 0.01);
}

/** @test */
TEST(JfetShichmanTest, CustomParameters) {
  JfetShichmanParams params{.Beta = 2e-3, .Vp = -3.0, .lambda = 0.02};
  EXPECT_DOUBLE_EQ(params.Beta, 2e-3);
  EXPECT_DOUBLE_EQ(params.Vp, -3.0);
  EXPECT_DOUBLE_EQ(params.lambda, 0.02);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(JfetShichmanTest, CurrentCutoff) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double VGS = -2.5; // Below Vp (cutoff)
  const double VDS = 5.0;
  const double ID = JfetShichman::current(VGS, VDS, params);

  // Should be zero (cutoff region)
  EXPECT_DOUBLE_EQ(ID, 0.0);
}

/** @test */
TEST(JfetShichmanTest, CurrentLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double VGS = -0.5; // Below Vp
  const double VDS = 0.5;  // Vds < Vgst = -0.5 - (-2.0) = 1.5
  const double ID = JfetShichman::current(VGS, VDS, params);

  // Linear region: Id = 2*Beta*[(Vgs - Vp)*Vds - 0.5*Vds^2]
  const double VGST = VGS - params.Vp; // 1.5
  const double EXPECTED = 2.0 * params.Beta * (VGST * VDS - 0.5 * VDS * VDS);
  EXPECT_NEAR(ID, EXPECTED, std::abs(EXPECTED) * 1e-10);
  EXPECT_GT(ID, 0.0); // Should conduct
}

/** @test */
TEST(JfetShichmanTest, CurrentSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double VGS = -0.5; // Below Vp
  const double VDS = 5.0;  // Vds > Vgst = 1.5 (saturation)
  const double ID = JfetShichman::current(VGS, VDS, params);

  // Saturation region: Id = Beta*(Vgs - Vp)^2
  const double VGST = VGS - params.Vp; // 1.5
  const double EXPECTED = params.Beta * VGST * VGST;
  EXPECT_NEAR(ID, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(JfetShichmanTest, CurrentChannelModulation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 5.0;
  const double ID = JfetShichman::current(VGS, VDS, params);

  // Saturation with channel modulation: Id = Beta*Vgst^2 * (1 + lambda*Vds)
  const double VGST = VGS - params.Vp;
  const double EXPECTED = params.Beta * VGST * VGST * (1.0 + params.lambda * VDS);
  EXPECT_NEAR(ID, EXPECTED, std::abs(EXPECTED) * 1e-10);
  EXPECT_GT(ID, params.Beta * VGST * VGST); // Should be higher than no modulation
}

/** @test */
TEST(JfetShichmanTest, CurrentAtPinchoff) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double VGS = -2.0; // Exactly at Vp
  const double VDS = 5.0;
  const double ID = JfetShichman::current(VGS, VDS, params);

  // At pinch-off, current should be very small (Vgst = 0)
  EXPECT_NEAR(ID, 0.0, 1e-10);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(JfetShichmanTest, TransconductanceCutoff) {
  JfetShichmanParams params{.Vp = -2.0};
  const double VGS = -2.5; // Below Vp (cutoff)
  const double VDS = 5.0;
  const double GM = JfetShichman::transconductance(VGS, VDS, params);

  EXPECT_DOUBLE_EQ(GM, 0.0);
}

/** @test */
TEST(JfetShichmanTest, TransconductanceLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double VGS = -0.5;
  const double VDS = 0.5;
  const double GM = JfetShichman::transconductance(VGS, VDS, params);

  // Linear: gm = 2*Beta*Vds
  const double EXPECTED = 2.0 * params.Beta * VDS;
  EXPECT_NEAR(GM, EXPECTED, std::abs(EXPECTED) * 1e-10);
  EXPECT_GT(GM, 0.0);
}

/** @test */
TEST(JfetShichmanTest, TransconductanceSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double VGS = -0.5;
  const double VDS = 5.0;
  const double GM = JfetShichman::transconductance(VGS, VDS, params);

  // Saturation: gm = 2*Beta*Vgst
  const double VGST = VGS - params.Vp;
  const double EXPECTED = 2.0 * params.Beta * VGST;
  EXPECT_NEAR(GM, EXPECTED, std::abs(EXPECTED) * 1e-10);
  EXPECT_GT(GM, 0.0);
}

/** @test */
TEST(JfetShichmanTest, OutputConductanceCutoff) {
  JfetShichmanParams params{.Vp = -2.0};
  const double VGS = -2.5; // Below Vp (cutoff)
  const double VDS = 5.0;
  const double GDS = JfetShichman::outputConductance(VGS, VDS, params);

  EXPECT_DOUBLE_EQ(GDS, 0.0);
}

/** @test */
TEST(JfetShichmanTest, OutputConductanceLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 0.5;
  const double GDS = JfetShichman::outputConductance(VGS, VDS, params);

  // Linear region has higher output conductance
  EXPECT_GT(GDS, 0.0);
}

/** @test */
TEST(JfetShichmanTest, OutputConductanceSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 5.0;
  const double GDS = JfetShichman::outputConductance(VGS, VDS, params);

  // Saturation: gds = lambda*Beta*Vgst^2 (channel modulation only)
  const double VGST = VGS - params.Vp;
  const double EXPECTED = params.lambda * params.Beta * VGST * VGST;
  EXPECT_NEAR(GDS, EXPECTED, std::abs(EXPECTED) * 1e-10);
  EXPECT_GT(GDS, 0.0);
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(JfetShichmanTest, TransconductanceNumericalDerivativeSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 5.0;
  const double DVGS = 1e-8;

  const double GM_ANALYTICAL = JfetShichman::transconductance(VGS, VDS, params);

  const double ID1 = JfetShichman::current(VGS - DVGS, VDS, params);
  const double ID2 = JfetShichman::current(VGS + DVGS, VDS, params);
  const double GM_NUMERICAL = (ID2 - ID1) / (2.0 * DVGS);

  EXPECT_NEAR(GM_ANALYTICAL, GM_NUMERICAL, std::abs(GM_NUMERICAL) * 0.01);
}

/** @test */
TEST(JfetShichmanTest, TransconductanceNumericalDerivativeLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 0.5;
  const double DVGS = 1e-8;

  const double GM_ANALYTICAL = JfetShichman::transconductance(VGS, VDS, params);

  const double ID1 = JfetShichman::current(VGS - DVGS, VDS, params);
  const double ID2 = JfetShichman::current(VGS + DVGS, VDS, params);
  const double GM_NUMERICAL = (ID2 - ID1) / (2.0 * DVGS);

  EXPECT_NEAR(GM_ANALYTICAL, GM_NUMERICAL, std::abs(GM_NUMERICAL) * 0.01);
}

/** @test */
TEST(JfetShichmanTest, OutputConductanceNumericalDerivativeSaturation) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 5.0;
  const double DVDS = 1e-8;

  const double GDS_ANALYTICAL = JfetShichman::outputConductance(VGS, VDS, params);

  const double ID1 = JfetShichman::current(VGS, VDS - DVDS, params);
  const double ID2 = JfetShichman::current(VGS, VDS + DVDS, params);
  const double GDS_NUMERICAL = (ID2 - ID1) / (2.0 * DVDS);

  EXPECT_NEAR(GDS_ANALYTICAL, GDS_NUMERICAL, std::abs(GDS_NUMERICAL) * 0.01);
}

/** @test */
TEST(JfetShichmanTest, OutputConductanceNumericalDerivativeLinear) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.01};
  const double VGS = -0.5;
  const double VDS = 0.5;
  const double DVDS = 1e-8;

  const double GDS_ANALYTICAL = JfetShichman::outputConductance(VGS, VDS, params);

  const double ID1 = JfetShichman::current(VGS, VDS - DVDS, params);
  const double ID2 = JfetShichman::current(VGS, VDS + DVDS, params);
  const double GDS_NUMERICAL = (ID2 - ID1) / (2.0 * DVDS);

  EXPECT_NEAR(GDS_ANALYTICAL, GDS_NUMERICAL, std::abs(GDS_NUMERICAL) * 0.01);
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test */
TEST(JfetShichmanTest, RegionCutoff) {
  JfetShichmanParams params{.Vp = -2.0};
  EXPECT_EQ(JfetShichman::region(-3.0, 5.0, params), 0); // Below Vp (cutoff)
  EXPECT_EQ(JfetShichman::region(-2.5, 5.0, params), 0); // Below Vp (cutoff)
  EXPECT_EQ(JfetShichman::region(-2.0, 5.0, params), 0); // At Vp (cutoff)
}

/** @test */
TEST(JfetShichmanTest, RegionLinear) {
  JfetShichmanParams params{.Vp = -2.0};
  const double VGS = -0.5; // VGST = VGS - Vp = 1.5; the Vds values below are all < it

  EXPECT_EQ(JfetShichman::region(VGS, 0.5, params), 1); // Vds < Vgst
  EXPECT_EQ(JfetShichman::region(VGS, 1.0, params), 1); // Vds < Vgst
  EXPECT_EQ(JfetShichman::region(VGS, 1.4, params), 1); // Vds < Vgst
}

/** @test */
TEST(JfetShichmanTest, RegionSaturation) {
  JfetShichmanParams params{.Vp = -2.0};
  const double VGS = -0.5;
  const double VGST = VGS - params.Vp; // 1.5

  EXPECT_EQ(JfetShichman::region(VGS, VGST, params), 2); // Vds = Vgst (boundary)
  EXPECT_EQ(JfetShichman::region(VGS, 2.0, params), 2);  // Vds > Vgst
  EXPECT_EQ(JfetShichman::region(VGS, 5.0, params), 2);  // Vds > Vgst
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(JfetShichmanTest, StampIsLinearizationAtOperatingPoint) {
  const JfetShichmanParams PARAMS;

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
    const double ID = JfetShichman::current(bias.vgs, bias.vds, PARAMS);
    const double GM = JfetShichman::transconductance(bias.vgs, bias.vds, PARAMS);
    const double GDS = JfetShichman::outputConductance(bias.vgs, bias.vds, PARAMS);

    // Terminal order: drain, gate, source.
    const auto STAMP = [&](MnaSystem& mna) {
      JfetShichman::stamp(mna, terminalNet(0), terminalNet(1), terminalNet(2), bias.vgs, bias.vds,
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
      expectCurrent(PINNED.into[1], 0.0, "gate");
      expectCurrent(PINNED.into[2], -I_DRAIN, "source");
    }
  }
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(JfetShichmanTest, SaturationConstantCurrent) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double VGS = -0.5;

  // In saturation (no channel modulation), current should be constant vs Vds
  const double ID1 = JfetShichman::current(VGS, 3.0, params);
  const double ID2 = JfetShichman::current(VGS, 5.0, params);
  const double ID3 = JfetShichman::current(VGS, 10.0, params);

  // All should be nearly equal (saturation)
  EXPECT_NEAR(ID1, ID2, 1e-10);
  EXPECT_NEAR(ID2, ID3, 1e-10);
}

/** @test */
TEST(JfetShichmanTest, LinearOhmicBehavior) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  const double VGS = -0.5;

  // In linear region, current should increase with Vds
  const double ID1 = JfetShichman::current(VGS, 0.1, params);
  const double ID2 = JfetShichman::current(VGS, 0.5, params);
  const double ID3 = JfetShichman::current(VGS, 1.0, params);

  EXPECT_GT(ID2, ID1); // Current increases with Vds
  EXPECT_GT(ID3, ID2);
}

/** @test */
TEST(JfetShichmanTest, TransconductanceIncreasesWithVgs) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double VDS = 5.0; // Saturation

  // More negative Vgs (closer to Vp) should reduce transconductance
  const double GM1 = JfetShichman::transconductance(-0.2, VDS, params);
  const double GM2 = JfetShichman::transconductance(-0.5, VDS, params);
  const double GM3 = JfetShichman::transconductance(-1.0, VDS, params);

  EXPECT_GT(GM1, GM2); // Less negative Vgs -> higher gm
  EXPECT_GT(GM2, GM3);
}

/** @test */
TEST(JfetShichmanTest, PinchoffCutoff) {
  JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
  const double VDS = 5.0;

  // Vgs at or below Vp should cut off
  const double ID_CUTOFF = JfetShichman::current(-2.5, VDS, params);  // Below Vp
  const double ID_CONDUCT = JfetShichman::current(-0.5, VDS, params); // Above Vp

  EXPECT_DOUBLE_EQ(ID_CUTOFF, 0.0);
  EXPECT_GT(ID_CONDUCT, 1e-6);
}

/** @test */
TEST(JfetShichmanTest, ChannelModulationEffect) {
  JfetShichmanParams noMod{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.0};
  JfetShichmanParams withMod{.Beta = 1e-3, .Vp = -2.0, .lambda = 0.02};
  const double VGS = -0.5;
  const double VDS = 5.0;

  const double ID_NO_MOD = JfetShichman::current(VGS, VDS, noMod);
  const double ID_WITH_MOD = JfetShichman::current(VGS, VDS, withMod);

  // Channel modulation should increase current
  EXPECT_GT(ID_WITH_MOD, ID_NO_MOD);
}
