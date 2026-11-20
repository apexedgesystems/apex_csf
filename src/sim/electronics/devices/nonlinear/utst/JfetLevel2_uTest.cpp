/**
 * @file JfetLevel2_uTest.cpp
 * @brief Unit tests for JfetLevel2.
 *
 * Expected currents and conductances are reference values computed outside
 * the library from the model equations, which each table restates.
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

namespace {

/// Relative tolerance on a current against its reference value.
constexpr double CURRENT_TOL = 1e-12;

/// Relative tolerance on a conductance: the model differentiates numerically.
constexpr double CONDUCTANCE_TOL = 1e-4;

/// Turn-on voltage: one subthreshold slope voltage above pinch-off.
double turnOnVoltage(const JfetLevel2Params& params) {
  return params.Vp + params.n_sub * params.Vt;
}

} // namespace

/* ----------------------------- Drain Current ----------------------------- */

/** @test Drain current matches the reference in every region */
TEST(JfetLevel2Test, DrainCurrentMatchesReference) {
  struct Row {
    double vgs;
    double vds;
    double id;
  };

  // With Vgst = Vgs - Vp and Von = Vp + n_sub*Vt:
  //   Vgs >= Von: 2*Beta*(Vgst*Vds - Vds^2/2)*(1 + lambda*Vds) for Vds < Vgst,
  //               Beta*Vgst^2*(1 + lambda*Vds) from there on;
  //   Vgs <  Von: that current at Vgs = Von, times exp((Vgs - Von)/(n_sub*Vt)).
  const JfetLevel2Params PARAMS;
  const std::array<Row, 10> ROWS = {{
      Row{-0.5, 5.0, 0.0023625},               // saturation
      Row{-0.5, 0.5, 0.00125625},              // linear region
      Row{0.0, 1.0, 0.00303},                  // linear region
      Row{0.0, 3.0, 0.00412},                  // saturation
      Row{-1.95, 5.0, 2.625e-06},              // just above turn-on
      Row{-2.0, 5.0, 5.875218615228519e-07},   // at pinch-off
      Row{-2.1, 5.0, 4.523227552590983e-08},   // below pinch-off
      Row{-2.1, 1.0, 4.350914122016088e-08},   // same gate, lower drain: saturated
      Row{-2.1, 0.01, 1.9261152219626463e-08}, // below the turn-on saturation voltage
      Row{-2.5, 5.0, 1.589081084517508e-12},   // deep below pinch-off
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(JfetLevel2::drainCurrent(row.vgs, row.vds, PARAMS), row.id, CURRENT_TOL * row.id)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
  }

  // Every parameter of the drain current moved off its default.
  const JfetLevel2Params OTHER{.Beta = 2e-3, .Vp = -2.5, .lambda = 0.02, .n_sub = 2.0};
  EXPECT_NEAR(JfetLevel2::drainCurrent(-1.0, 4.0, OTHER), 0.00486, CURRENT_TOL * 0.00486);
  EXPECT_NEAR(JfetLevel2::drainCurrent(-2.6, 4.0, OTHER), 3.14039487943108e-07,
              CURRENT_TOL * 3.14039487943108e-07);
}

/** @test Transconductance and output conductance are the derivatives of the drain current */
TEST(JfetLevel2Test, ConductancesMatchReference) {
  struct Row {
    double vgs;
    double vds;
    double gm;
    double gds;
  };

  const JfetLevel2Params PARAMS;
  const std::array<Row, 5> ROWS = {{
      Row{-0.5, 5.0, 0.00315, 2.25e-05},                              // saturation
      Row{-0.5, 0.5, 0.001005, 0.0020225},                            // linear region
      Row{-2.1, 5.0, 1.1598019365617905e-06, 4.3078357643723643e-10}, // below pinch-off
      Row{-2.1, 0.01, 4.938756979391401e-07, 1.6430555757016979e-06}, // below pinch-off
      Row{-2.5, 5.0, 4.074566883378226e-11, 1.513410556683341e-14},   // deep below
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(JfetLevel2::transconductance(row.vgs, row.vds, PARAMS), row.gm,
                CONDUCTANCE_TOL * row.gm)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
    EXPECT_NEAR(JfetLevel2::outputConductance(row.vgs, row.vds, PARAMS), row.gds,
                CONDUCTANCE_TOL * row.gds)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
  }
}

/* ----------------------------- Gate Junction ----------------------------- */

/** @test Gate current and conductance follow Is*(exp(Vgs/(n*Vt)) - 1) */
TEST(JfetLevel2Test, GateJunctionFollowsShockley) {
  // An ideality factor off its default shows the junction uses n*Vt.
  const JfetLevel2Params PARAMS{.Is = 1e-14, .n = 1.5, .Vt = 0.026};

  EXPECT_NEAR(JfetLevel2::gateCurrent(0.6, PARAMS), 4.802346335708585e-08,
              CURRENT_TOL * 4.802346335708585e-08);
  EXPECT_NEAR(JfetLevel2::gateConductance(0.6, PARAMS), 1.2313711117201498e-06,
              CURRENT_TOL * 1.2313711117201498e-06);

  // Reverse bias, the normal operating condition: the leakage is -Is.
  EXPECT_NEAR(JfetLevel2::gateCurrent(-1.0, PARAMS), -9.999999999926846e-15,
              CURRENT_TOL * 9.999999999926846e-15);
  EXPECT_EQ(JfetLevel2::gateCurrent(0.0, PARAMS), 0.0);

  // The default junction: Is = 1e-14 A, n = 1.
  EXPECT_NEAR(JfetLevel2::gateCurrent(0.6, JfetLevel2Params{}), 0.00010523988178435307,
              CURRENT_TOL * 0.00010523988178435307);
}

/* ----------------------------- Region Boundaries ----------------------------- */

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

/** @test Drain current is continuous in the gate voltage at pinch-off and at turn-on */
TEST(JfetLevel2Test, ContinuousAtPinchOffAndTurnOn) {
  const JfetLevel2Params PARAMS;
  constexpr double EPS = 1e-9;

  for (const double VGS : {PARAMS.Vp, turnOnVoltage(PARAMS)}) {
    for (const double VDS : {0.01, 0.5, 5.0}) {
      const double BELOW = JfetLevel2::drainCurrent(VGS - EPS, VDS, PARAMS);
      const double ABOVE = JfetLevel2::drainCurrent(VGS + EPS, VDS, PARAMS);
      EXPECT_GT(ABOVE, 0.0) << "vgs = " << VGS << ", vds = " << VDS;
      EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE) << "vgs = " << VGS << ", vds = " << VDS;
    }
  }
}

/** @test Drain current never falls as the gate voltage rises through pinch-off */
TEST(JfetLevel2Test, MonotonicInGateVoltage) {
  const JfetLevel2Params PARAMS;

  for (const double VDS : {0.01, 0.5, 5.0}) {
    double previous = JfetLevel2::drainCurrent(PARAMS.Vp - 0.5, VDS, PARAMS);
    for (int step = 1; step <= 1000; ++step) {
      const double VGS = PARAMS.Vp - 0.5 + 0.001 * step;
      const double ID = JfetLevel2::drainCurrent(VGS, VDS, PARAMS);
      ASSERT_GE(ID, previous) << "vgs = " << VGS << ", vds = " << VDS;
      previous = ID;
    }
  }
}

/** @test Drain current never falls as the drain voltage rises, below pinch-off and above */
TEST(JfetLevel2Test, MonotonicInDrainVoltage) {
  const JfetLevel2Params PARAMS;

  for (const double VGS : {-2.2, -2.0, -1.5, -1.0, -0.5, 0.0}) {
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

/** @test Transconductance is finite and not negative at pinch-off and at turn-on */
TEST(JfetLevel2Test, TransconductanceNotNegativeThroughTurnOn) {
  const JfetLevel2Params PARAMS;

  for (const double BOUNDARY : {PARAMS.Vp, turnOnVoltage(PARAMS)}) {
    // On a boundary the transconductance is a difference taken across it.
    for (const double VGS : {BOUNDARY - 5e-9, BOUNDARY, BOUNDARY + 5e-9}) {
      for (const double VDS : {0.01, 5.0}) {
        const double GM = JfetLevel2::transconductance(VGS, VDS, PARAMS);
        EXPECT_TRUE(std::isfinite(GM)) << "vgs = " << VGS << ", vds = " << VDS;
        EXPECT_GE(GM, 0.0) << "vgs = " << VGS << ", vds = " << VDS;
      }
    }
  }
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

  for (const Bias& bias : {Bias{-3.0, 5.0, "cutoff"}, Bias{-2.05, 5.0, "subthreshold"},
                           Bias{-0.5, 0.5, "linear"}, Bias{-0.5, 5.0, "saturation"}}) {
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
