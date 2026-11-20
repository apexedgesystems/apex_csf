/**
 * @file ZenerDiode_uTest.cpp
 * @brief Unit tests for ZenerDiode.
 *
 * Notes:
 *  - Reference currents and the regulator's operating points come from the
 *    model equation and bisection on the circuit equation, evaluated outside
 *    this library.
 */

#include "src/sim/electronics/devices/nonlinear/inc/ZenerDiode.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <utility>

using sim::electronics::devices::nonlinear::Junction;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;
using sim::electronics::devices::nonlinear::ZenerDiode;
using sim::electronics::devices::nonlinear::ZenerDiodeParams;
using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default parameters describe a 5.1 V Zener with a 1 mA knee */
TEST(ZenerDiodeDefaultTest, DefaultParameters) {
  const ZenerDiodeParams PARAMS;

  EXPECT_DOUBLE_EQ(PARAMS.Is, 1e-14);
  EXPECT_DOUBLE_EQ(PARAMS.n, 1.0);
  EXPECT_DOUBLE_EQ(PARAMS.Vt, 0.026);
  EXPECT_DOUBLE_EQ(PARAMS.Vz, 5.1);
  EXPECT_DOUBLE_EQ(PARAMS.Ibv, 1e-3);
  EXPECT_DOUBLE_EQ(PARAMS.Vbv, 0.1);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test Current matches the reference from forward bias through the knee into breakdown */
TEST(ZenerDiodeTest, CurrentMatchesReference) {
  // I = Is*(exp(V/(n*Vt)) - 1) - Ibv*exp(-(V + Vz)/Vbv) with the defaults.
  struct Row {
    double v;
    double current;
  };
  const std::array<Row, 7> ROWS = {{
      Row{0.7, 0.004926559627563881},
      Row{0.0, -7.095474162284754e-26},
      Row{-2.0, -1.00344247710847e-14},
      Row{-5.0, -0.0003678794411814436},
      Row{-5.1, -0.00100000000001},
      Row{-5.5, -0.054598150033154436},
      Row{-6.0, -8.103083927575423},
  }};
  const ZenerDiodeParams PARAMS;

  for (const Row& row : ROWS) {
    EXPECT_NEAR(ZenerDiode::current(row.v, PARAMS), row.current, 1e-11 * std::abs(row.current))
        << "v = " << row.v;
  }
}

/** @test Current and conductance are continuous across the breakdown voltage */
TEST(ZenerDiodeTest, ContinuousAcrossBreakdownVoltage) {
  const ZenerDiodeParams PARAMS;
  const double EPS = 1e-9;
  const double BELOW = -PARAMS.Vz - EPS;
  const double ABOVE = -PARAMS.Vz + EPS;

  // Across 2 nV the curve moves by its slope, 10 mS, times 2 nV.
  EXPECT_NEAR(ZenerDiode::current(BELOW, PARAMS), ZenerDiode::current(ABOVE, PARAMS),
              1e-6 * PARAMS.Ibv);
  EXPECT_NEAR(ZenerDiode::conductance(BELOW, PARAMS), ZenerDiode::conductance(ABOVE, PARAMS),
              1e-6 * PARAMS.Ibv / PARAMS.Vbv);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test Conductance is the derivative of the current in every region */
TEST(ZenerDiodeTest, ConductanceIsDerivativeOfCurrent) {
  const ZenerDiodeParams PARAMS;

  // Central difference; the steepest stretch is the junction, with truncation
  // about g * (DV / Vt)^2 / 6 = 2.5e-10 * g.
  constexpr double DV = 1e-6;

  for (const double V : {0.7, 0.5, -4.8, -5.0, -5.1, -5.2, -5.5, -6.0}) {
    const double NUMERICAL =
        (ZenerDiode::current(V + DV, PARAMS) - ZenerDiode::current(V - DV, PARAMS)) / (2.0 * DV);
    EXPECT_NEAR(ZenerDiode::conductance(V, PARAMS), NUMERICAL, 1e-6 * NUMERICAL) << "v = " << V;
  }
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test Region code is breakdown below -Vz, reverse up to zero, forward from zero */
TEST(ZenerDiodeTest, RegionFollowsBias) {
  const ZenerDiodeParams PARAMS;

  EXPECT_EQ(ZenerDiode::region(-6.0, PARAMS), 0);
  EXPECT_EQ(ZenerDiode::region(-5.2, PARAMS), 0);
  EXPECT_EQ(ZenerDiode::region(-5.0, PARAMS), 1);
  EXPECT_EQ(ZenerDiode::region(-0.5, PARAMS), 1);
  EXPECT_EQ(ZenerDiode::region(0.0, PARAMS), 2);
  EXPECT_EQ(ZenerDiode::region(0.7, PARAMS), 2);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(ZenerDiodeTest, StampIsLinearizationAtOperatingPoint) {
  const ZenerDiodeParams PARAMS;

  // Anode and cathode both sit off ground so every stamped entry takes part.
  const double V_CATHODE = 6.5;
  const double DV = 0.01;
  for (const double V : {0.7, -2.0, -5.1, -5.5}) {
    const double I = ZenerDiode::current(V, PARAMS);
    const double G = ZenerDiode::conductance(V, PARAMS);
    const auto STAMP = [&](MnaSystem& mna) {
      ZenerDiode::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
    };

    // At the operating point, then one step on each terminal.
    for (const auto& [dAnode, dCathode] :
         {std::pair{0.0, 0.0}, std::pair{DV, 0.0}, std::pair{0.0, DV}}) {
      const auto PINNED = pinTerminals({V_CATHODE + V + dAnode, V_CATHODE + dCathode}, STAMP);
      ASSERT_TRUE(PINNED.solved) << "v = " << V;

      const double I_LINEAR = I + G * (dAnode - dCathode);
      expectCurrent(PINNED.into[0], I_LINEAR, "anode");
      expectCurrent(PINNED.into[1], -I_LINEAR, "cathode");
    }
  }
}

/** @test Current, conductance and stamp stay finite far outside any operating range */
TEST(ZenerDiodeTest, FiniteAtLargeBias) {
  const ZenerDiodeParams PARAMS;

  for (const double V : {20.0, 100.0, -20.0, -100.0}) {
    EXPECT_TRUE(std::isfinite(ZenerDiode::current(V, PARAMS))) << "v = " << V;
    EXPECT_TRUE(std::isfinite(ZenerDiode::conductance(V, PARAMS))) << "v = " << V;

    const auto PINNED = pinTerminals({V, 0.0}, [&](MnaSystem& mna) {
      ZenerDiode::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
    });
    ASSERT_TRUE(PINNED.solved) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[0])) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[1])) << "v = " << V;
  }
}

/* ----------------------------- Shunt Regulator ----------------------------- */

/** @test A shunt regulator solved by Newton on the stamp settles at the reference output voltage */
TEST(ZenerDiodeTest, ShuntRegulatorSettlesAtReferenceVoltage) {
  // Source, series resistor to the output net, Zener from ground (anode) to
  // the output (cathode), optional load to ground. Reference: bisection on
  // (Vs - Vn)/R = Vn*Gload - I(-Vn).
  struct Row {
    double vSource;
    double rSeries;
    double gLoad;
    double vOut;
    double iZener;
  };
  const std::array<Row, 4> ROWS = {{
      Row{12.0, 1e3, 0.0, 5.290354610157406, 0.006709645389842637},
      Row{9.0, 470.0, 1e-3, 5.20537210117077, 0.002868304279316962},
      Row{5.0, 1e3, 0.0, 4.884313160333862, 0.0001156868396661362},
      Row{24.0, 1e3, 0.0, 5.3923572397411075, 0.018607642760259013},
  }};
  const ZenerDiodeParams PARAMS;
  const NetID SOURCE = 1;
  const NetID OUT = 2;

  // The breakdown current is an exponential in (Vout - Vz) / Vbv, so the
  // junction step limiter applies to it with Ibv and Vbv in the roles of Is
  // and n*Vt.
  const double V_CRIT = Junction::criticalVoltage(PARAMS.Ibv, PARAMS.Vbv);

  for (const Row& row : ROWS) {
    double vOut = 0.0;
    int iterations = 0;
    for (; iterations < 50; ++iterations) {
      MnaSystem mna(3);
      mna.addVoltageSource(SOURCE, 0, row.vSource);
      mna.addConductance(SOURCE, OUT, 1.0 / row.rSeries);
      mna.addConductance(OUT, 0, row.gLoad);
      ZenerDiode::stamp(mna, /*anodeNet=*/0, /*cathodeNet=*/OUT, -vOut, PARAMS);
      const auto RESULT = mna.solve();
      ASSERT_TRUE(RESULT.success) << "Vs = " << row.vSource;

      const double V_SOLVED = RESULT.nodeVoltages[OUT];
      if (std::abs(V_SOLVED - vOut) < 1e-10) {
        vOut = V_SOLVED;
        break;
      }
      vOut = PARAMS.Vz +
             Junction::limitVoltage(V_SOLVED - PARAMS.Vz, vOut - PARAMS.Vz, PARAMS.Vbv, V_CRIT);
    }

    EXPECT_LT(iterations, 50) << "Vs = " << row.vSource;
    EXPECT_NEAR(vOut, row.vOut, 1e-9) << "Vs = " << row.vSource;
    EXPECT_NEAR(-ZenerDiode::current(-vOut, PARAMS), row.iZener, 1e-8 * row.iZener)
        << "Vs = " << row.vSource;
  }
}
