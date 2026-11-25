/**
 * @file DiodeSpice_uTest.cpp
 * @brief Unit tests for DiodeSpice.
 *
 * Notes:
 *  - Reference currents come from bisection on the terminal equation
 *    I = Is * (exp((V - I*Rs) / (n*Vt)) - 1), evaluated outside this library.
 */

#include "src/sim/electronics/devices/nonlinear/inc/DiodeSpice.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <utility>

using sim::electronics::devices::nonlinear::DiodeSpice;
using sim::electronics::devices::nonlinear::DiodeSpiceParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default parameters describe an ideal silicon junction without parasitics */
TEST(DiodeSpiceDefaultTest, DefaultParameters) {
  const DiodeSpiceParams PARAMS;

  EXPECT_DOUBLE_EQ(PARAMS.Is, 1e-14);
  EXPECT_DOUBLE_EQ(PARAMS.n, 1.0);
  EXPECT_DOUBLE_EQ(PARAMS.Vt, 0.026);
  EXPECT_DOUBLE_EQ(PARAMS.Rs, 0.0);
  EXPECT_DOUBLE_EQ(PARAMS.Cj0, 0.0);
  EXPECT_DOUBLE_EQ(PARAMS.Vj, 0.7);
  EXPECT_DOUBLE_EQ(PARAMS.M, 0.5);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test Current matches the reference solution of the terminal equation, with and without Rs */
TEST(DiodeSpiceTest, CurrentMatchesSeriesResistanceReference) {
  struct Row {
    double v;
    double rs;
    double current;
  };
  const std::array<Row, 12> ROWS = {{
      Row{0.7, 0.0, 0.004926559627563881},
      Row{0.3, 1.0, 1.0258548716554892e-09},
      Row{0.8, 1.0, 0.04341796044631156},
      Row{1.0, 1.0, 0.20328130382728687},
      Row{2.0, 1.0, 1.1580439847433612},
      Row{5.0, 1.0, 4.1250152137432075},
      Row{0.8, 100.0, 0.0013387543417064693},
      Row{1.0, 100.0, 0.0031188646807203246},
      Row{2.0, 100.0, 0.012752713181679257},
      Row{5.0, 100.0, 0.04244010226595234},
      Row{-2.0, 1.0, -1e-14},
      Row{-2.0, 100.0, -1e-14},
  }};

  for (const Row& row : ROWS) {
    const DiodeSpiceParams PARAMS{.Rs = row.rs};
    EXPECT_NEAR(DiodeSpice::current(row.v, PARAMS), row.current, 1e-9 * std::abs(row.current))
        << "v = " << row.v << ", Rs = " << row.rs;
  }
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test Conductance is the derivative of the current, with and without Rs */
TEST(DiodeSpiceTest, ConductanceIsDerivativeOfCurrent) {
  // Central difference: truncation about g * (DV / Vt)^2 / 6 = 2.5e-10 * g.
  constexpr double DV = 1e-6;

  for (const double RS : {0.0, 1.0, 100.0}) {
    const DiodeSpiceParams PARAMS{.Rs = RS};
    for (const double V : {0.3, 0.6, 0.8, 2.0, 5.0}) {
      const double NUMERICAL =
          (DiodeSpice::current(V + DV, PARAMS) - DiodeSpice::current(V - DV, PARAMS)) / (2.0 * DV);
      EXPECT_NEAR(DiodeSpice::conductance(V, PARAMS), NUMERICAL, 1e-6 * NUMERICAL)
          << "v = " << V << ", Rs = " << RS;
    }
  }
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(DiodeSpiceTest, StampIsLinearizationAtOperatingPoint) {
  const DiodeSpiceParams PARAMS{.Rs = 0.5};

  // Anode and cathode both sit off ground so every stamped entry takes part.
  const double V_CATHODE = 1.2;
  const double DV = 0.01;
  for (const double V : {2.0, 0.7, -2.0}) {
    const double I = DiodeSpice::current(V, PARAMS);
    const double G = DiodeSpice::conductance(V, PARAMS);
    const auto STAMP = [&](MnaSystem& mna) {
      DiodeSpice::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
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
TEST(DiodeSpiceTest, FiniteAtLargeBias) {
  const DiodeSpiceParams PARAMS{.Rs = 0.5};

  for (const double V : {20.0, 100.0, -20.0, -100.0}) {
    EXPECT_TRUE(std::isfinite(DiodeSpice::current(V, PARAMS))) << "v = " << V;
    EXPECT_TRUE(std::isfinite(DiodeSpice::conductance(V, PARAMS))) << "v = " << V;

    const auto PINNED = pinTerminals({V, 0.0}, [&](MnaSystem& mna) {
      DiodeSpice::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
    });
    ASSERT_TRUE(PINNED.solved) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[0])) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[1])) << "v = " << V;
  }
}

/* ----------------------------- Junction Capacitance ----------------------------- */

/** @test Junction capacitance follows Cj0 / (1 - V/Vj)^M below the built-in potential */
TEST(DiodeSpiceTest, JunctionCapacitanceMatchesReference) {
  // Reference values of Cj0 / (1 - V/Vj)^M, evaluated outside this library.
  const DiodeSpiceParams ABRUPT{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};
  EXPECT_DOUBLE_EQ(DiodeSpice::junctionCapacitance(0.0, ABRUPT), 10e-12);
  EXPECT_NEAR(DiodeSpice::junctionCapacitance(-5.0, ABRUPT), 3.504383220252312e-12, 1e-24);
  EXPECT_NEAR(DiodeSpice::junctionCapacitance(0.35, ABRUPT), 1.4142135623730948e-11, 1e-23);

  const DiodeSpiceParams GRADED{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.33};
  EXPECT_NEAR(DiodeSpice::junctionCapacitance(-5.0, GRADED), 5.0054560314781495e-12, 1e-24);

  // No zero-bias capacitance: no junction capacitance at any bias.
  const DiodeSpiceParams NONE;
  EXPECT_DOUBLE_EQ(DiodeSpice::junctionCapacitance(-5.0, NONE), 0.0);
}
