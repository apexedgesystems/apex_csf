/**
 * @file JfetShichman_uTest.cpp
 * @brief Unit tests for JfetShichman.
 *
 * Expected currents and conductances are reference values computed outside
 * the library from the model equations, which each table restates.
 */

#include "src/sim/electronics/devices/nonlinear/inc/JfetShichman.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::JfetShichman;
using sim::electronics::devices::nonlinear::JfetShichmanParams;
using sim::electronics::devices::nonlinear::MnaSystem;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

namespace {

/// Relative tolerance on a value against its reference.
constexpr double VALUE_TOL = 1e-12;

} // namespace

/* ----------------------------- Drain Current ----------------------------- */

/** @test Drain current matches the reference in cutoff, the linear region and saturation */
TEST(JfetShichmanTest, CurrentMatchesReference) {
  struct Row {
    double vgs;
    double vds;
    double id;
  };

  // With Vgst = Vgs - Vp: zero for Vgs <= Vp;
  // 2*Beta*(Vgst*Vds - Vds^2/2)*(1 + lambda*Vds) for Vds < Vgst;
  // Beta*Vgst^2*(1 + lambda*Vds) from there on.
  const JfetShichmanParams PARAMS;
  const std::array<Row, 6> ROWS = {{
      Row{-0.5, 5.0, 0.0023625},  // saturation
      Row{-0.5, 0.5, 0.00125625}, // linear region
      Row{0.0, 1.0, 0.00303},     // linear region
      Row{0.0, 3.0, 0.00412},     // saturation
      Row{-2.0, 5.0, 0.0},        // at pinch-off
      Row{-2.5, 5.0, 0.0},        // below pinch-off
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(JfetShichman::current(row.vgs, row.vds, PARAMS), row.id, VALUE_TOL * row.id)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
  }

  // Every parameter moved off its default.
  const JfetShichmanParams OTHER{.Beta = 2e-3, .Vp = -2.5, .lambda = 0.02};
  EXPECT_NEAR(JfetShichman::current(-1.0, 4.0, OTHER), 0.00486, VALUE_TOL * 0.00486);
}

/** @test Transconductance and output conductance are the derivatives of the drain current */
TEST(JfetShichmanTest, ConductancesMatchReference) {
  struct Row {
    double vgs;
    double vds;
    double gm;
    double gds;
  };

  const JfetShichmanParams PARAMS;
  const std::array<Row, 5> ROWS = {{
      Row{-0.5, 5.0, 0.00315, 2.25e-05},   // saturation
      Row{-0.5, 0.5, 0.001005, 0.0020225}, // linear region
      Row{0.0, 1.0, 0.00202, 0.00205},     // linear region
      Row{0.0, 3.0, 0.00412, 4e-05},       // saturation
      Row{-2.5, 5.0, 0.0, 0.0},            // below pinch-off
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(JfetShichman::transconductance(row.vgs, row.vds, PARAMS), row.gm,
                VALUE_TOL * row.gm)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
    EXPECT_NEAR(JfetShichman::outputConductance(row.vgs, row.vds, PARAMS), row.gds,
                VALUE_TOL * row.gds)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
  }
}

/* ----------------------------- Region Boundaries ----------------------------- */

/** @test Drain current is continuous at the saturation voltage and at pinch-off */
TEST(JfetShichmanTest, ContinuousAtRegionBoundaries) {
  const JfetShichmanParams PARAMS;
  constexpr double EPS = 1e-9;

  for (const double VGS : {-1.5, -1.0, -0.5, 0.0}) {
    const double VDSAT = VGS - PARAMS.Vp;
    const double BELOW = JfetShichman::current(VGS, VDSAT - EPS, PARAMS);
    const double ABOVE = JfetShichman::current(VGS, VDSAT + EPS, PARAMS);
    EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE) << "vgs = " << VGS;
  }

  // The square law leaves pinch-off from zero: 1 nV above it the current is
  // Beta*(1e-9)^2 in saturation and no more in the linear region.
  for (const double VDS : {0.01, 5.0}) {
    EXPECT_EQ(JfetShichman::current(PARAMS.Vp - EPS, VDS, PARAMS), 0.0);
    EXPECT_LT(JfetShichman::current(PARAMS.Vp + EPS, VDS, PARAMS), 1e-18) << "vds = " << VDS;
  }
}

/** @test Region code is 0 at or below pinch-off, 1 for Vds < Vgst, 2 from Vds = Vgst on */
TEST(JfetShichmanTest, RegionClassifiesBias) {
  const JfetShichmanParams PARAMS;

  EXPECT_EQ(JfetShichman::region(-3.0, 5.0, PARAMS), 0);
  EXPECT_EQ(JfetShichman::region(-2.0, 5.0, PARAMS), 0); // at pinch-off

  // Vgs = -0.5 V puts the boundary at Vds = 1.5 V.
  EXPECT_EQ(JfetShichman::region(-0.5, 0.5, PARAMS), 1);
  EXPECT_EQ(JfetShichman::region(-0.5, 1.4, PARAMS), 1);
  EXPECT_EQ(JfetShichman::region(-0.5, 1.5, PARAMS), 2); // on the boundary
  EXPECT_EQ(JfetShichman::region(-0.5, 5.0, PARAMS), 2);
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
