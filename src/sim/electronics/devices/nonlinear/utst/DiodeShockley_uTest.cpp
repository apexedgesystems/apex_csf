/**
 * @file DiodeShockley_uTest.cpp
 * @brief Unit tests for DiodeShockley model.
 *
 * Expected currents and conductances are reference values computed outside
 * the library from I = Is*(exp(V/(n*Vt)) - 1) and its derivative.
 */

#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <utility>

using sim::electronics::devices::nonlinear::DiodeShockley;
using sim::electronics::devices::nonlinear::DiodeShockleyParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

namespace {

/// Relative tolerance on a value against its reference.
constexpr double VALUE_TOL = 1e-12;

} // namespace

/* ----------------------------- I-V Characteristic ----------------------------- */

/** @test Current and conductance match the Shockley equation, forward, zero and reverse bias */
TEST(DiodeShockleyTest, CurrentAndConductanceMatchReference) {
  struct Row {
    double v;
    double current;
    double conductance;
  };

  const DiodeShockleyParams PARAMS;
  const std::array<Row, 4> ROWS = {{
      Row{0.7, 0.004926559627563881, 0.18948306259899542},
      Row{0.5, 2.2481068956995423e-06, 8.646565021921317e-05},
      Row{0.0, 0.0, 3.846153846153846e-13},
      Row{-1.0, -1e-14, 7.610141512715596e-30},
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(DiodeShockley::current(row.v, PARAMS), row.current,
                VALUE_TOL * std::abs(row.current))
        << "v = " << row.v;
    EXPECT_NEAR(DiodeShockley::conductance(row.v, PARAMS), row.conductance,
                VALUE_TOL * row.conductance)
        << "v = " << row.v;
  }

  // Every parameter moved off its default: the ideality factor scales the
  // thermal voltage.
  const DiodeShockleyParams OTHER{.Is = 1e-12, .n = 1.5, .Vt = 0.025};
  EXPECT_NEAR(DiodeShockley::current(0.6, OTHER), 8.88610952050784e-06,
              VALUE_TOL * 8.88610952050784e-06);
  EXPECT_NEAR(DiodeShockley::conductance(0.6, OTHER), 0.00023696294721354237,
              VALUE_TOL * 0.00023696294721354237);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(DiodeShockleyTest, StampIsLinearizationAtOperatingPoint) {
  const DiodeShockleyParams PARAMS;

  // Anode and cathode both sit off ground so every stamped entry takes part.
  const double V_CATHODE = 1.2;
  const double DV = 0.01;
  for (const double V : {0.7, 0.3, -2.0}) {
    const double I = DiodeShockley::current(V, PARAMS);
    const double G = DiodeShockley::conductance(V, PARAMS);
    const auto STAMP = [&](MnaSystem& mna) {
      DiodeShockley::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
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
TEST(DiodeShockleyTest, FiniteAtLargeBias) {
  const DiodeShockleyParams PARAMS;

  for (const double V : {20.0, 100.0, -20.0, -100.0}) {
    EXPECT_TRUE(std::isfinite(DiodeShockley::current(V, PARAMS))) << "v = " << V;
    EXPECT_TRUE(std::isfinite(DiodeShockley::conductance(V, PARAMS))) << "v = " << V;

    const auto PINNED = pinTerminals({V, 0.0}, [&](MnaSystem& mna) {
      DiodeShockley::stamp(mna, terminalNet(0), terminalNet(1), V, PARAMS);
    });
    ASSERT_TRUE(PINNED.solved) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[0])) << "v = " << V;
    EXPECT_TRUE(std::isfinite(PINNED.into[1])) << "v = " << V;
  }
}

/* ----------------------------- Circuit ----------------------------- */

/** @test Newton iteration on the stamp reaches the operating point of a current-driven diode */
TEST(DiodeShockleyTest, NewtonOnStampReachesDrivenOperatingPoint) {
  const DiodeShockleyParams PARAMS{.Is = 1e-14, .n = 1.0, .Vt = 0.026};
  const double I_DRIVE = 1e-3;
  const NetID ANODE = 1;
  const NetID CATHODE = 0;

  MnaSystem mna(2);
  double vDiode = 0.7;
  for (int iter = 0; iter < 20; ++iter) {
    mna.clear();
    DiodeShockley::stamp(mna, ANODE, CATHODE, vDiode, PARAMS);
    mna.addCurrent(ANODE, CATHODE, I_DRIVE);
    const auto RESULT = mna.solve();
    ASSERT_TRUE(RESULT.success) << "iteration " << iter;
    vDiode = RESULT.nodeVoltages[ANODE] - RESULT.nodeVoltages[CATHODE];
  }

  // Closed form of I_DRIVE = Is * (exp(V / (n * Vt)) - 1).
  const double V_EXPECTED = PARAMS.n * PARAMS.Vt * std::log(1.0 + I_DRIVE / PARAMS.Is);
  EXPECT_NEAR(vDiode, V_EXPECTED, 1e-6);
}
