/**
 * @file MosfetLevel3_uTest.cpp
 * @brief Unit tests for MosfetLevel3.
 *
 * Expected currents and conductances are reference values computed outside
 * the library from the model equations, which each table restates.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel3.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetLevel3;
using sim::electronics::devices::nonlinear::MosfetLevel3Params;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

namespace {

/// Relative tolerance on a value against its reference.
constexpr double VALUE_TOL = 1e-12;

/// Relative tolerance on a conductance: the model differentiates numerically.
constexpr double CONDUCTANCE_TOL = 1e-4;

/// Geometry, short-channel and threshold parameters moved off their defaults.
constexpr MosfetLevel3Params OTHER{.Kp = 400e-6,
                                   .Vth0 = 0.4,
                                   .lambda = 0.1,
                                   .W = 0.5e-6,
                                   .L = 0.13e-6,
                                   .eta = 0.1,
                                   .kappa = 0.2,
                                   .delta_w = 0.05e-6,
                                   .delta_l = 0.02e-6};

/// Turn-on voltage: one subthreshold slope voltage above threshold.
double turnOnVoltage(double vds, double vbs, const MosfetLevel3Params& params) {
  return MosfetLevel3::thresholdVoltage(vds, vbs, params) + params.n_sub * params.Vt;
}

/// Drain voltage that equals its own saturation voltage. The threshold moves
/// with the drain voltage, so the boundary is a fixed point.
double saturationBoundary(double vgs, double vbs, const MosfetLevel3Params& params) {
  double vdsat = 1.0;
  for (int iter = 0; iter < 50; ++iter) {
    const double VTH = MosfetLevel3::thresholdVoltage(vdsat, vbs, params);
    vdsat = MosfetLevel3::drainSaturationVoltage(vgs, VTH, params);
  }
  return vdsat;
}

} // namespace

/* ----------------------------- Dimensions, Threshold, Saturation Voltage
 * ----------------------------- */

/** @test Effective dimensions are the drawn ones less the deltas, floored at 10 nm */
TEST(MosfetLevel3Test, EffectiveDimensionsSubtractDeltas) {
  const MosfetLevel3Params PLAIN{.W = 1e-6, .L = 0.18e-6};
  const auto [W_PLAIN, L_PLAIN] = MosfetLevel3::effectiveDimensions(PLAIN.W, PLAIN.L, PLAIN);
  EXPECT_DOUBLE_EQ(W_PLAIN, 1e-6);
  EXPECT_DOUBLE_EQ(L_PLAIN, 0.18e-6);

  const MosfetLevel3Params REDUCED{.W = 1e-6, .L = 0.18e-6, .delta_w = 0.05e-6, .delta_l = 0.02e-6};
  const auto [W_REDUCED, L_REDUCED] =
      MosfetLevel3::effectiveDimensions(REDUCED.W, REDUCED.L, REDUCED);
  EXPECT_DOUBLE_EQ(W_REDUCED, 0.95e-6);
  EXPECT_DOUBLE_EQ(L_REDUCED, 0.16e-6);

  const MosfetLevel3Params CONSUMED{.W = 1e-6, .L = 0.18e-6, .delta_w = 2e-6, .delta_l = 1e-6};
  const auto [W_FLOOR, L_FLOOR] =
      MosfetLevel3::effectiveDimensions(CONSUMED.W, CONSUMED.L, CONSUMED);
  EXPECT_DOUBLE_EQ(W_FLOOR, 0.01e-6);
  EXPECT_DOUBLE_EQ(L_FLOOR, 0.01e-6);
}

/** @test Threshold is Vth0 plus the body term less eta*Vds */
TEST(MosfetLevel3Test, ThresholdVoltageFollowsBodyEffectAndDibl) {
  const MosfetLevel3Params PARAMS;

  EXPECT_DOUBLE_EQ(MosfetLevel3::thresholdVoltage(0.0, 0.0, PARAMS), 0.5);
  EXPECT_NEAR(MosfetLevel3::thresholdVoltage(2.0, 0.0, PARAMS), 0.4, VALUE_TOL);
  EXPECT_NEAR(MosfetLevel3::thresholdVoltage(2.0, -1.0, PARAMS), 0.5961257579303474, VALUE_TOL);

  // A forward-biased bulk adds no body term.
  EXPECT_NEAR(MosfetLevel3::thresholdVoltage(2.0, 0.5, PARAMS), 0.4, VALUE_TOL);
}

/** @test Saturation voltage is Vgst*(1 - kappa) above threshold and 0 below */
TEST(MosfetLevel3Test, DrainSaturationVoltageMatchesReference) {
  const MosfetLevel3Params PARAMS;

  EXPECT_NEAR(MosfetLevel3::drainSaturationVoltage(1.5, 0.5, PARAMS), 0.9, VALUE_TOL);
  EXPECT_DOUBLE_EQ(MosfetLevel3::drainSaturationVoltage(0.0, 0.5, PARAMS), 0.0);
}

/* ----------------------------- Drain Current ----------------------------- */

/** @test Drain current matches the reference in every region */
TEST(MosfetLevel3Test, CurrentMatchesReference) {
  struct Row {
    double vgs;
    double vds;
    double vbs;
    double id;
  };

  // With Vth = Vth0 + body term - eta*Vds, Vgst = Vgs - Vth,
  // beta = Kp*(Weff/Leff)/(1 + theta*Vgst), Vdsat = Vgst*(1 - kappa) and
  // Von = Vth + n_sub*Vt:
  //   Vgs >= Von: beta*(Vgst*V - V^2/2)*(1 + lambda*Vds), V = min(Vds, Vdsat);
  //   Vgs <  Von: that current at Vgs = Von, times exp((Vgs - Von)/(n_sub*Vt)).
  const MosfetLevel3Params PARAMS;
  const std::array<Row, 8> ROWS = {{
      Row{1.5, 0.3, 0.0, 0.0002432737600443336},  // linear region
      Row{1.5, 2.0, 0.0, 0.0006000409836065573},  // saturation
      Row{1.5, 2.0, -0.5, 0.0004952477788762659}, // saturation, reverse body bias
      Row{0.45, 1.0, 0.0, 3.206360625496811e-07}, // at threshold
      Row{0.3, 1.0, 0.0, 6.849343938287535e-09},  // weak inversion
      Row{0.3, 3.0, 0.0, 9.743906561350015e-08},  // same gate, higher drain: lower barrier
      Row{0.3, 0.01, 0.0, 8.283261142577451e-10}, // below the turn-on saturation voltage
      Row{0.0, 1.0, 0.0, 3.1255193423404087e-12}, // deep weak inversion
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(MosfetLevel3::current(row.vgs, row.vds, row.vbs, PARAMS), row.id,
                VALUE_TOL * row.id)
        << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
  }

  EXPECT_NEAR(MosfetLevel3::current(1.2, 1.5, -0.3, OTHER), 0.0005952651710183392,
              VALUE_TOL * 0.0005952651710183392);
  EXPECT_NEAR(MosfetLevel3::current(0.1, 1.5, -0.3, OTHER), 1.7967241245545972e-09,
              VALUE_TOL * 1.7967241245545972e-09);
}

/** @test gm, gds and gmb are the derivatives of the drain current */
TEST(MosfetLevel3Test, ConductancesMatchReference) {
  struct Row {
    double vgs;
    double vds;
    double vbs;
    double gm;
    double gds;
    double gmb;
  };

  // The bulk transconductance is compared under reverse body bias only: the
  // body effect switches off at Vbs = 0, so the slope there is one-sided.
  const MosfetLevel3Params PARAMS;
  const std::array<Row, 5> ROWS = {{
      Row{1.5, 0.3, 0.0, 0.00024079682570612353, 0.0006943156332867839, 0.0},
      Row{1.5, 2.0, 0.0, 0.0009926162321956464, 7.690540177371674e-05, 0.0},
      Row{1.5, 2.0, -0.5, 0.0009175074483198666, 6.838663509218723e-05, 0.0001749618054654353},
      Row{0.3, 1.0, 0.0, 1.756242035458342e-07, 9.10736941244826e-09, 0.0},
      Row{0.3, 0.01, 0.0, 2.123913113481398e-08, 7.175469720428084e-08, 0.0},
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(MosfetLevel3::transconductance(row.vgs, row.vds, row.vbs, PARAMS), row.gm,
                CONDUCTANCE_TOL * row.gm)
        << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
    EXPECT_NEAR(MosfetLevel3::outputConductance(row.vgs, row.vds, row.vbs, PARAMS), row.gds,
                CONDUCTANCE_TOL * row.gds)
        << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
    if (row.vbs < 0.0) {
      EXPECT_NEAR(MosfetLevel3::bulkTransconductance(row.vgs, row.vds, row.vbs, PARAMS), row.gmb,
                  CONDUCTANCE_TOL * row.gmb)
          << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
    }
  }

  EXPECT_NEAR(MosfetLevel3::transconductance(0.1, 1.5, -0.3, OTHER), 4.6069849347553775e-08,
              CONDUCTANCE_TOL * 4.6069849347553775e-08);
  EXPECT_NEAR(MosfetLevel3::outputConductance(0.1, 1.5, -0.3, OTHER), 4.76322181515143e-09,
              CONDUCTANCE_TOL * 4.76322181515143e-09);
  EXPECT_NEAR(MosfetLevel3::bulkTransconductance(0.1, 1.5, -0.3, OTHER), 9.71237702660614e-09,
              CONDUCTANCE_TOL * 9.71237702660614e-09);
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

  for (const Bias& bias : {Bias{0.3, 1.0, 0.0, "weak inversion"}, Bias{2.0, 0.2, -0.5, "linear"},
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
      const double VDSAT = saturationBoundary(VGS, VBS, PARAMS);
      ASSERT_GT(VDSAT, 0.0) << "vgs = " << VGS;

      const double BELOW = MosfetLevel3::current(VGS, VDSAT - EPS, VBS, PARAMS);
      const double ABOVE = MosfetLevel3::current(VGS, VDSAT + EPS, VBS, PARAMS);
      EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE) << "vgs = " << VGS << ", vbs = " << VBS;
    }
  }
}

/** @test Drain current is continuous in the gate voltage at threshold and at turn-on */
TEST(MosfetLevel3Test, ContinuousAtThresholdAndTurnOn) {
  const MosfetLevel3Params PARAMS;
  constexpr double EPS = 1e-9;

  for (const double VBS : {0.0, -0.5}) {
    for (const double VDS : {0.01, 0.5, 3.0}) {
      for (const double VGS :
           {MosfetLevel3::thresholdVoltage(VDS, VBS, PARAMS), turnOnVoltage(VDS, VBS, PARAMS)}) {
        const double BELOW = MosfetLevel3::current(VGS - EPS, VDS, VBS, PARAMS);
        const double ABOVE = MosfetLevel3::current(VGS + EPS, VDS, VBS, PARAMS);
        EXPECT_GT(ABOVE, 0.0) << "vgs = " << VGS << ", vds = " << VDS << ", vbs = " << VBS;
        EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE)
            << "vgs = " << VGS << ", vds = " << VDS << ", vbs = " << VBS;
      }
    }
  }
}

/** @test Drain current never falls as the gate voltage rises through threshold */
TEST(MosfetLevel3Test, MonotonicInGateVoltage) {
  const MosfetLevel3Params PARAMS;

  for (const double VDS : {0.01, 0.5, 3.0}) {
    const double VTH = MosfetLevel3::thresholdVoltage(VDS, 0.0, PARAMS);
    double previous = MosfetLevel3::current(VTH - 0.5, VDS, 0.0, PARAMS);
    for (int step = 1; step <= 1000; ++step) {
      const double VGS = VTH - 0.5 + 0.001 * step;
      const double ID = MosfetLevel3::current(VGS, VDS, 0.0, PARAMS);
      ASSERT_GE(ID, previous) << "vgs = " << VGS << ", vds = " << VDS;
      previous = ID;
    }
  }
}

/** @test Drain current never falls as the drain voltage rises, below threshold and above */
TEST(MosfetLevel3Test, MonotonicInDrainVoltage) {
  const MosfetLevel3Params PARAMS;

  for (const double VGS : {0.2, 0.4, 1.0, 1.5, 2.0, 3.0}) {
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
    const double VDSAT = saturationBoundary(VGS, 0.0, PARAMS);
    for (const double VDS : {VDSAT - 5e-9, VDSAT, VDSAT + 5e-9}) {
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

/** @test Transconductance is finite and not negative at threshold and at turn-on */
TEST(MosfetLevel3Test, TransconductanceNotNegativeThroughTurnOn) {
  const MosfetLevel3Params PARAMS;

  for (const double VDS : {0.01, 3.0}) {
    for (const double BOUNDARY :
         {MosfetLevel3::thresholdVoltage(VDS, 0.0, PARAMS), turnOnVoltage(VDS, 0.0, PARAMS)}) {
      // On a boundary the transconductance is a difference taken across it.
      for (const double VGS : {BOUNDARY - 5e-9, BOUNDARY, BOUNDARY + 5e-9}) {
        const double GM = MosfetLevel3::transconductance(VGS, VDS, 0.0, PARAMS);
        EXPECT_TRUE(std::isfinite(GM)) << "vgs = " << VGS << ", vds = " << VDS;
        EXPECT_GE(GM, 0.0) << "vgs = " << VGS << ", vds = " << VDS;
      }
    }
  }
}
