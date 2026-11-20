/**
 * @file DiodeShockley_uTest.cpp
 * @brief Unit tests for DiodeShockley model.
 */

#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

using sim::electronics::devices::nonlinear::DiodeShockley;
using sim::electronics::devices::nonlinear::DiodeShockleyParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

/* ----------------------------- Constants ----------------------------- */

static constexpr double EPSILON = 1e-9; // Tolerance for floating-point comparison

/* ----------------------------- Parameters ----------------------------- */

/** @test Default parameters are reasonable. */
TEST(DiodeShockleyTest, DefaultParameters) {
  DiodeShockleyParams params;

  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
}

/** @test Custom parameters. */
TEST(DiodeShockleyTest, CustomParameters) {
  DiodeShockleyParams params{.Is = 1e-12, .n = 1.5, .Vt = 0.025};

  EXPECT_DOUBLE_EQ(params.Is, 1e-12);
  EXPECT_DOUBLE_EQ(params.n, 1.5);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
}

/* ----------------------------- I-V Characteristic ----------------------------- */

/** @test Zero bias current (reverse saturation). */
TEST(DiodeShockleyTest, ZeroBiasCurrent) {
  DiodeShockleyParams params;
  double i = DiodeShockley::current(0.0, params);

  // I(0) = Is * (exp(0) - 1) = Is * (1 - 1) = 0
  EXPECT_NEAR(i, 0.0, EPSILON);
}

/** @test Reverse bias current approaches -Is. */
TEST(DiodeShockleyTest, ReverseBiasCurrent) {
  DiodeShockleyParams params;

  // At large reverse bias, I -> -Is
  double i = DiodeShockley::current(-1.0, params);
  EXPECT_NEAR(i, -params.Is, params.Is * 0.01); // Within 1% of -Is
}

/** @test Forward bias current (exponential region). */
TEST(DiodeShockleyTest, ForwardBiasCurrent) {
  DiodeShockleyParams params;

  // At 0.7V forward bias (typical silicon diode)
  double i = DiodeShockley::current(0.7, params);

  // I = Is * (exp(0.7 / 0.026) - 1) ~= Is * exp(26.9) ~= 5.5e11 * Is
  EXPECT_GT(i, 1e-3); // Forward current should be mA range
  EXPECT_LT(i, 1.0);  // But not unreasonably large
}

/** @test Current-voltage monotonicity. */
TEST(DiodeShockleyTest, MonotonicIV) {
  DiodeShockleyParams params;

  double i1 = DiodeShockley::current(0.5, params);
  double i2 = DiodeShockley::current(0.6, params);
  double i3 = DiodeShockley::current(0.7, params);

  // Current should increase with voltage
  EXPECT_LT(i1, i2);
  EXPECT_LT(i2, i3);
}

/** @test Ideality factor effect. */
TEST(DiodeShockleyTest, IdealityFactor) {
  DiodeShockleyParams ideal{.Is = 1e-14, .n = 1.0, .Vt = 0.026};
  DiodeShockleyParams nonIdeal{.Is = 1e-14, .n = 2.0, .Vt = 0.026};

  double iIdeal = DiodeShockley::current(0.7, ideal);
  double iNonIdeal = DiodeShockley::current(0.7, nonIdeal);

  // Higher ideality factor -> lower current at same voltage
  EXPECT_GT(iIdeal, iNonIdeal);
}

/* ----------------------------- Conductance (Jacobian) ----------------------------- */

/** @test Conductance at zero bias. */
TEST(DiodeShockleyTest, ZeroBiasConductance) {
  DiodeShockleyParams params;
  double g = DiodeShockley::conductance(0.0, params);

  // g(0) = Is / (n * Vt) * exp(0) = Is / (n * Vt)
  double expectedG = params.Is / (params.n * params.Vt);
  EXPECT_NEAR(g, expectedG, EPSILON);
}

/** @test Conductance increases with forward bias. */
TEST(DiodeShockleyTest, ForwardBiasConductance) {
  DiodeShockleyParams params;

  double g1 = DiodeShockley::conductance(0.5, params);
  double g2 = DiodeShockley::conductance(0.6, params);
  double g3 = DiodeShockley::conductance(0.7, params);

  // Conductance should increase exponentially with voltage
  EXPECT_LT(g1, g2);
  EXPECT_LT(g2, g3);
}

/** @test Conductance matches numerical derivative. */
TEST(DiodeShockleyTest, ConductanceNumericalDerivative) {
  DiodeShockleyParams params;
  const double V = 0.6;
  const double DV = 1e-6;

  // Analytical conductance
  double gAnalytical = DiodeShockley::conductance(V, params);

  // Numerical derivative: dI/dV ~= (I(v+dv) - I(v-dv)) / (2*dv)
  double i1 = DiodeShockley::current(V - DV, params);
  double i2 = DiodeShockley::current(V + DV, params);
  double gNumerical = (i2 - i1) / (2.0 * DV);

  // Should match within 1%
  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
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

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test Silicon diode turn-on voltage. */
TEST(DiodeShockleyTest, SiliconTurnOnVoltage) {
  DiodeShockleyParams params; // Default = silicon diode

  // Below 0.6V: relatively low current
  double iLow = DiodeShockley::current(0.5, params);
  EXPECT_LT(iLow, 10e-6); // Less than 10 uA

  // Above 0.7V: significant current
  double iHigh = DiodeShockley::current(0.7, params);
  EXPECT_GT(iHigh, 1e-3); // More than 1mA
}

/** @test Temperature effect (via Vt). */
TEST(DiodeShockleyTest, TemperatureEffect) {
  // Room temperature (300K): Vt ~= 26mV
  DiodeShockleyParams roomTemp{.Is = 1e-14, .n = 1.0, .Vt = 0.026};

  // Hot temperature (350K): Vt ~= 30mV
  DiodeShockleyParams hotTemp{.Is = 1e-14, .n = 1.0, .Vt = 0.030};

  double iRoom = DiodeShockley::current(0.7, roomTemp);
  double iHot = DiodeShockley::current(0.7, hotTemp);

  // Higher temperature -> lower current at same voltage
  EXPECT_GT(iRoom, iHot);
}

/** @test Exponential scaling with voltage. */
TEST(DiodeShockleyTest, ExponentialScaling) {
  DiodeShockleyParams params;

  // Current should scale exponentially: every 60mV decade in ideal diode
  double i1 = DiodeShockley::current(0.6, params);
  double i2 = DiodeShockley::current(0.66, params); // +60mV

  double ratio = i2 / i1;

  // Should be approximately 10x (one decade)
  EXPECT_NEAR(ratio, 10.0, 1.0); // Within 10% tolerance
}
