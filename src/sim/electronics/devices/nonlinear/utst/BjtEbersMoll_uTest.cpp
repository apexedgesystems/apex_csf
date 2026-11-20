/**
 * @file BjtEbersMoll_uTest.cpp
 * @brief Unit tests for BjtEbersMoll model.
 *
 * Notes:
 *  - Reference currents come from the transport form of the Ebers-Moll
 *    equations evaluated outside this library, not from the model's own
 *    functions.
 */

#include "src/sim/electronics/devices/nonlinear/inc/BjtEbersMoll.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using sim::electronics::devices::nonlinear::BjtEbersMoll;
using sim::electronics::devices::nonlinear::BjtEbersMollParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Constants ----------------------------- */

/// One bias point with the junction voltages the tests share.
struct BjtBias {
  double vbe;
  double vbc;
  const char* region;
};

static constexpr BjtBias FORWARD_ACTIVE{0.7, -5.0, "forward active"};
static constexpr BjtBias SATURATION{0.7, 0.5, "saturation"};
static constexpr BjtBias REVERSE_ACTIVE{-5.0, 0.7, "reverse active"};
static constexpr BjtBias CUTOFF{-1.0, -2.0, "cutoff"};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default parameters describe the documented small-signal NPN device */
TEST(BjtEbersMollDefaultTest, DefaultParameters) {
  const BjtEbersMollParams PARAMS;

  EXPECT_DOUBLE_EQ(PARAMS.Is, 1e-14);
  EXPECT_DOUBLE_EQ(PARAMS.Bf, 100.0);
  EXPECT_DOUBLE_EQ(PARAMS.Br, 1.0);
  EXPECT_DOUBLE_EQ(PARAMS.Vt, 0.026);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test Collector and base currents match the transport-form reference in all four regions */
TEST(BjtEbersMollTest, CurrentsMatchTransportFormReference) {
  // Reference values: Is = 1e-14 A, Vt = 0.026 V,
  //   Ic = Is*(exp(Vbe/Vt) - exp(Vbc/Vt)) - (Is/Br)*(exp(Vbc/Vt) - 1)
  //   Ib = (Is/Bf)*(exp(Vbe/Vt) - 1) + (Is/Br)*(exp(Vbc/Vt) - 1)
  // evaluated in double precision outside this library.
  struct Row {
    BjtBias bias;
    double bf;
    double br;
    double ic;
    double ib;
  };
  const std::array<Row, 4> ROWS = {{
      Row{FORWARD_ACTIVE, 100.0, 1.0, 0.00492655962758388, 4.92655962656388e-05},
      Row{SATURATION, 100.0, 1.0, 0.0049220634137724815, 5.151370317133835e-05},
      Row{REVERSE_ACTIVE, 100.0, 5.0, -0.005911871553086657, 0.0009853119255126763},
      Row{CUTOFF, 100.0, 1.0, 1e-14, -1.01e-14},
  }};
  constexpr double REL_TOL = 1e-12;

  for (const Row& row : ROWS) {
    const BjtEbersMollParams PARAMS{.Is = 1e-14, .Bf = row.bf, .Br = row.br, .Vt = 0.026};
    const double IC = BjtEbersMoll::collectorCurrent(row.bias.vbe, row.bias.vbc, PARAMS);
    const double IB = BjtEbersMoll::baseCurrent(row.bias.vbe, row.bias.vbc, PARAMS);
    const double IE = BjtEbersMoll::emitterCurrent(row.bias.vbe, row.bias.vbc, PARAMS);

    EXPECT_NEAR(IC, row.ic, REL_TOL * std::abs(row.ic)) << row.bias.region;
    EXPECT_NEAR(IB, row.ib, REL_TOL * std::abs(row.ib)) << row.bias.region;
    EXPECT_NEAR(IE, -(row.ic + row.ib), REL_TOL * std::abs(row.ic)) << row.bias.region;
  }
}

/** @test Forward-active gain Ic/Ib is Bf and reverse-active gain Ie/Ib is Br */
TEST(BjtEbersMollTest, ActiveRegionGainsAreBfAndBr) {
  // Leakage of the reverse-biased junction moves each ratio by about 1e-10.
  constexpr double REL_TOL = 1e-8;

  for (const double BF : {50.0, 200.0}) {
    for (const double BR : {1.0, 5.0}) {
      const BjtEbersMollParams PARAMS{.Is = 1e-14, .Bf = BF, .Br = BR, .Vt = 0.026};

      const double IC_FWD =
          BjtEbersMoll::collectorCurrent(FORWARD_ACTIVE.vbe, FORWARD_ACTIVE.vbc, PARAMS);
      const double IB_FWD =
          BjtEbersMoll::baseCurrent(FORWARD_ACTIVE.vbe, FORWARD_ACTIVE.vbc, PARAMS);
      EXPECT_NEAR(IC_FWD / IB_FWD, BF, REL_TOL * BF);

      // Reverse active: the emitter takes the collector's role.
      const double IE_REV =
          BjtEbersMoll::emitterCurrent(REVERSE_ACTIVE.vbe, REVERSE_ACTIVE.vbc, PARAMS);
      const double IB_REV =
          BjtEbersMoll::baseCurrent(REVERSE_ACTIVE.vbe, REVERSE_ACTIVE.vbc, PARAMS);
      EXPECT_NEAR(IE_REV / IB_REV, BR, REL_TOL * BR);
    }
  }
}

/** @test A higher thermal voltage lowers the collector current at a fixed Vbe */
TEST(BjtEbersMollTest, TemperatureEffect) {
  const BjtEbersMollParams ROOM{.Is = 1e-14, .Bf = 100.0, .Br = 1.0, .Vt = 0.026};
  const BjtEbersMollParams HOT{.Is = 1e-14, .Bf = 100.0, .Br = 1.0, .Vt = 0.030};

  const double IC_ROOM = BjtEbersMoll::collectorCurrent(0.7, -5.0, ROOM);
  const double IC_HOT = BjtEbersMoll::collectorCurrent(0.7, -5.0, HOT);

  EXPECT_GT(IC_ROOM, IC_HOT);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test Each conductance is the derivative of its current, in every conducting region */
TEST(BjtEbersMollTest, ConductancesMatchCurrentDerivatives) {
  const BjtEbersMollParams PARAMS{.Is = 1e-14, .Bf = 100.0, .Br = 5.0, .Vt = 0.026};

  // Central difference: truncation is about g * (DV / Vt)^2 / 6 = 2.5e-10 * g,
  // roundoff about 1e-16 * I / DV = 5e-13 S at these currents.
  constexpr double DV = 1e-6;
  constexpr double REL_TOL = 1e-7;
  constexpr double ABS_TOL = 1e-10;

  for (const BjtBias& bias : {FORWARD_ACTIVE, SATURATION, REVERSE_ACTIVE}) {
    const double DIC_DVBE = (BjtEbersMoll::collectorCurrent(bias.vbe + DV, bias.vbc, PARAMS) -
                             BjtEbersMoll::collectorCurrent(bias.vbe - DV, bias.vbc, PARAMS)) /
                            (2.0 * DV);
    const double DIC_DVBC = (BjtEbersMoll::collectorCurrent(bias.vbe, bias.vbc + DV, PARAMS) -
                             BjtEbersMoll::collectorCurrent(bias.vbe, bias.vbc - DV, PARAMS)) /
                            (2.0 * DV);
    const double DIB_DVBE = (BjtEbersMoll::baseCurrent(bias.vbe + DV, bias.vbc, PARAMS) -
                             BjtEbersMoll::baseCurrent(bias.vbe - DV, bias.vbc, PARAMS)) /
                            (2.0 * DV);
    const double DIB_DVBC = (BjtEbersMoll::baseCurrent(bias.vbe, bias.vbc + DV, PARAMS) -
                             BjtEbersMoll::baseCurrent(bias.vbe, bias.vbc - DV, PARAMS)) /
                            (2.0 * DV);

    EXPECT_NEAR(BjtEbersMoll::transconductance(bias.vbe, bias.vbc, PARAMS), DIC_DVBE,
                REL_TOL * std::abs(DIC_DVBE) + ABS_TOL)
        << bias.region;
    EXPECT_NEAR(BjtEbersMoll::outputConductance(bias.vbe, bias.vbc, PARAMS), DIC_DVBC,
                REL_TOL * std::abs(DIC_DVBC) + ABS_TOL)
        << bias.region;
    EXPECT_NEAR(BjtEbersMoll::baseConductanceBE(bias.vbe, bias.vbc, PARAMS), DIB_DVBE,
                REL_TOL * std::abs(DIB_DVBE) + ABS_TOL)
        << bias.region;
    EXPECT_NEAR(BjtEbersMoll::baseConductanceBC(bias.vbe, bias.vbc, PARAMS), DIB_DVBC,
                REL_TOL * std::abs(DIB_DVBC) + ABS_TOL)
        << bias.region;
  }
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test Region code follows which junctions are forward biased */
TEST(BjtEbersMollTest, RegionFollowsJunctionBias) {
  const BjtEbersMollParams PARAMS;

  EXPECT_EQ(BjtEbersMoll::region(-1.0, -5.0, PARAMS), 0); // both reverse
  EXPECT_EQ(BjtEbersMoll::region(0.0, 0.0, PARAMS), 0);   // both at zero
  EXPECT_EQ(BjtEbersMoll::region(0.3, -1.0, PARAMS), 0);  // below turn-on
  EXPECT_EQ(BjtEbersMoll::region(0.7, -5.0, PARAMS), 1);  // BE forward, BC reverse
  EXPECT_EQ(BjtEbersMoll::region(-5.0, 0.7, PARAMS), 2);  // BE reverse, BC forward
  EXPECT_EQ(BjtEbersMoll::region(0.7, 0.6, PARAMS), 3);   // both forward
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(BjtEbersMollTest, StampIsLinearizationAtOperatingPoint) {
  const BjtEbersMollParams PARAMS{.Is = 1e-14, .Bf = 100.0, .Br = 5.0, .Vt = 0.026};

  struct Step {
    double collector;
    double base;
    double emitter;
  };

  // The emitter sits off ground so every stamped entry takes part.
  const double V_EMITTER = 6.0;
  const double DV = 0.01;

  for (const BjtBias& bias : {FORWARD_ACTIVE, SATURATION, REVERSE_ACTIVE, CUTOFF}) {
    const double IC = BjtEbersMoll::collectorCurrent(bias.vbe, bias.vbc, PARAMS);
    const double IB = BjtEbersMoll::baseCurrent(bias.vbe, bias.vbc, PARAMS);
    const double GM = BjtEbersMoll::transconductance(bias.vbe, bias.vbc, PARAMS);
    const double GO = BjtEbersMoll::outputConductance(bias.vbe, bias.vbc, PARAMS);
    const double GBE = BjtEbersMoll::baseConductanceBE(bias.vbe, bias.vbc, PARAMS);
    const double GBC = BjtEbersMoll::baseConductanceBC(bias.vbe, bias.vbc, PARAMS);

    // Terminal order: collector, base, emitter.
    const auto STAMP = [&](MnaSystem& mna) {
      BjtEbersMoll::stamp(mna, terminalNet(0), terminalNet(1), terminalNet(2), bias.vbe, bias.vbc,
                          PARAMS);
    };

    for (const Step& step :
         {Step{0.0, 0.0, 0.0}, Step{DV, 0.0, 0.0}, Step{0.0, DV, 0.0}, Step{0.0, 0.0, DV}}) {
      const double V_BASE = V_EMITTER + bias.vbe;
      const double V_COLLECTOR = V_BASE - bias.vbc;
      const auto PINNED = pinTerminals(
          {V_COLLECTOR + step.collector, V_BASE + step.base, V_EMITTER + step.emitter}, STAMP);
      ASSERT_TRUE(PINNED.solved) << bias.region;

      const double D_VBE = step.base - step.emitter;
      const double D_VBC = step.base - step.collector;
      const double I_COLLECTOR = IC + GM * D_VBE + GO * D_VBC;
      const double I_BASE = IB + GBE * D_VBE + GBC * D_VBC;
      expectCurrent(PINNED.into[0], I_COLLECTOR, "collector");
      expectCurrent(PINNED.into[1], I_BASE, "base");
      expectCurrent(PINNED.into[2], -(I_COLLECTOR + I_BASE), "emitter");
    }
  }
}
