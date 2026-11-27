/**
 * @file SchottkyDiode_uTest.cpp
 * @brief Unit tests for SchottkyDiode.
 *
 * Notes:
 *  - Reference currents come from bisection on the terminal equation
 *    I = Is * (exp((V - I*Rs) / (n*Vt)) - 1), evaluated outside this library.
 */

#include "src/sim/electronics/devices/nonlinear/inc/SchottkyDiode.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <utility>

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::SchottkyDiode;
using sim::electronics::devices::nonlinear::SchottkyDiodeParams;
using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default parameters describe a silicon Schottky junction without series resistance */
TEST(SchottkyDiodeDefaultTest, DefaultParameters) {
  const SchottkyDiodeParams PARAMS;

  EXPECT_DOUBLE_EQ(PARAMS.Is, 1e-12);
  EXPECT_DOUBLE_EQ(PARAMS.n, 1.0);
  EXPECT_DOUBLE_EQ(PARAMS.Vt, 0.026);
  EXPECT_DOUBLE_EQ(PARAMS.Rs, 0.0);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test Current matches the reference solution of the terminal equation, with and without Rs */
TEST(SchottkyDiodeTest, CurrentMatchesSeriesResistanceReference) {
  struct Row {
    double v;
    double rs;
    double current;
  };
  const std::array<Row, 5> ROWS = {{
      Row{0.3, 0.0, 1.0258549121319682e-07},
      Row{0.3, 0.5, 1.0258528883138052e-07},
      Row{1.0, 0.5, 0.5905733008799159},
      Row{5.0, 10.0, 0.430351542543679},
      Row{-1.0, 0.5, -1e-12},
  }};

  for (const Row& row : ROWS) {
    const SchottkyDiodeParams PARAMS{.Rs = row.rs};
    EXPECT_NEAR(SchottkyDiode::current(row.v, PARAMS), row.current, 1e-9 * std::abs(row.current))
        << "v = " << row.v << ", Rs = " << row.rs;
  }
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test Conductance is the derivative of the current, with and without Rs */
TEST(SchottkyDiodeTest, ConductanceIsDerivativeOfCurrent) {
  // Central difference: truncation about g * (DV / Vt)^2 / 6 = 2.5e-10 * g.
  constexpr double DV = 1e-6;

  for (const double RS : {0.0, 0.5, 10.0}) {
    const SchottkyDiodeParams PARAMS{.Rs = RS};
    for (const double V : {0.3, 0.6, 0.8, 2.0, 5.0}) {
      const double NUMERICAL =
          (SchottkyDiode::current(V + DV, PARAMS) - SchottkyDiode::current(V - DV, PARAMS)) /
          (2.0 * DV);
      EXPECT_NEAR(SchottkyDiode::conductance(V, PARAMS), NUMERICAL, 1e-6 * NUMERICAL)
          << "v = " << V << ", Rs = " << RS;
    }
  }
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(SchottkyDiodeTest, StampIsLinearizationAtOperatingPoint) {
  const SchottkyDiodeParams PARAMS{.Rs = 0.5};

  // Anode and cathode both sit off ground so every stamped entry takes part.
  const double V_CATHODE = 1.2;
  const double DV = 0.01;
  for (const double V : {1.0, 0.3, -1.0}) {
    const double I = SchottkyDiode::current(V, PARAMS);
    const double G = SchottkyDiode::conductance(V, PARAMS);
    const auto STAMP = [&](MnaSystem& mna) {
      SchottkyDiode::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
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
TEST(SchottkyDiodeTest, FiniteAtLargeBias) {
  const SchottkyDiodeParams PARAMS{.Rs = 0.5};

  for (const double V : {20.0, 100.0, -20.0, -100.0}) {
    EXPECT_TRUE(std::isfinite(SchottkyDiode::current(V, PARAMS))) << "v = " << V;
    EXPECT_TRUE(std::isfinite(SchottkyDiode::conductance(V, PARAMS))) << "v = " << V;

    const auto PINNED = pinTerminals({V, 0.0}, [&](MnaSystem& mna) {
      SchottkyDiode::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
    });
    ASSERT_TRUE(PINNED.solved) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[0])) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[1])) << "v = " << V;
  }
}
