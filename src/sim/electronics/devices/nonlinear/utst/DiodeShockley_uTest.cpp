/**
 * @file DiodeShockley_uTest.cpp
 * @brief Unit tests for DiodeShockley model.
 */

#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::nonlinear::DiodeShockley;
using sim::electronics::devices::nonlinear::DiodeShockleyParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Constants ----------------------------- */

static constexpr double EPSILON = 1e-9; // Tolerance for floating-point comparison

/* ----------------------------- Parameters ----------------------------- */

/** @test Default parameters are reasonable. */
TEST(DiodeShockley, DefaultParameters) {
  DiodeShockleyParams params;

  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
}

/** @test Custom parameters. */
TEST(DiodeShockley, CustomParameters) {
  DiodeShockleyParams params{.Is = 1e-12, .n = 1.5, .Vt = 0.025};

  EXPECT_DOUBLE_EQ(params.Is, 1e-12);
  EXPECT_DOUBLE_EQ(params.n, 1.5);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
}

/* ----------------------------- I-V Characteristic ----------------------------- */

/** @test Zero bias current (reverse saturation). */
TEST(DiodeShockley, ZeroBiasCurrent) {
  DiodeShockleyParams params;
  double i = DiodeShockley::current(0.0, params);

  // I(0) = Is * (exp(0) - 1) = Is * (1 - 1) = 0
  EXPECT_NEAR(i, 0.0, EPSILON);
}

/** @test Reverse bias current approaches -Is. */
TEST(DiodeShockley, ReverseBiasCurrent) {
  DiodeShockleyParams params;

  // At large reverse bias, I -> -Is
  double i = DiodeShockley::current(-1.0, params);
  EXPECT_NEAR(i, -params.Is, params.Is * 0.01); // Within 1% of -Is
}

/** @test Forward bias current (exponential region). */
TEST(DiodeShockley, ForwardBiasCurrent) {
  DiodeShockleyParams params;

  // At 0.7V forward bias (typical silicon diode)
  double i = DiodeShockley::current(0.7, params);

  // I = Is * (exp(0.7 / 0.026) - 1) ~= Is * exp(26.9) ~= 5.5e11 * Is
  EXPECT_GT(i, 1e-3); // Forward current should be mA range
  EXPECT_LT(i, 1.0);  // But not unreasonably large
}

/** @test Current-voltage monotonicity. */
TEST(DiodeShockley, MonotonicIV) {
  DiodeShockleyParams params;

  double i1 = DiodeShockley::current(0.5, params);
  double i2 = DiodeShockley::current(0.6, params);
  double i3 = DiodeShockley::current(0.7, params);

  // Current should increase with voltage
  EXPECT_LT(i1, i2);
  EXPECT_LT(i2, i3);
}

/** @test Ideality factor effect. */
TEST(DiodeShockley, IdealityFactor) {
  DiodeShockleyParams ideal{.Is = 1e-14, .n = 1.0, .Vt = 0.026};
  DiodeShockleyParams nonIdeal{.Is = 1e-14, .n = 2.0, .Vt = 0.026};

  double iIdeal = DiodeShockley::current(0.7, ideal);
  double iNonIdeal = DiodeShockley::current(0.7, nonIdeal);

  // Higher ideality factor -> lower current at same voltage
  EXPECT_GT(iIdeal, iNonIdeal);
}

/* ----------------------------- Conductance (Jacobian) ----------------------------- */

/** @test Conductance at zero bias. */
TEST(DiodeShockley, ZeroBiasConductance) {
  DiodeShockleyParams params;
  double g = DiodeShockley::conductance(0.0, params);

  // g(0) = Is / (n * Vt) * exp(0) = Is / (n * Vt)
  double expectedG = params.Is / (params.n * params.Vt);
  EXPECT_NEAR(g, expectedG, EPSILON);
}

/** @test Conductance increases with forward bias. */
TEST(DiodeShockley, ForwardBiasConductance) {
  DiodeShockleyParams params;

  double g1 = DiodeShockley::conductance(0.5, params);
  double g2 = DiodeShockley::conductance(0.6, params);
  double g3 = DiodeShockley::conductance(0.7, params);

  // Conductance should increase exponentially with voltage
  EXPECT_LT(g1, g2);
  EXPECT_LT(g2, g3);
}

/** @test Conductance matches numerical derivative. */
TEST(DiodeShockley, ConductanceNumericalDerivative) {
  DiodeShockleyParams params;
  const double v = 0.6;
  const double dv = 1e-6;

  // Analytical conductance
  double gAnalytical = DiodeShockley::conductance(v, params);

  // Numerical derivative: dI/dV ~= (I(v+dv) - I(v-dv)) / (2*dv)
  double i1 = DiodeShockley::current(v - dv, params);
  double i2 = DiodeShockley::current(v + dv, params);
  double gNumerical = (i2 - i1) / (2.0 * dv);

  // Should match within 1%
  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp into MNA system. */
TEST(DiodeShockley, Stamp) {
  MnaSystem mna(3);
  DiodeShockleyParams params;
  const NetID ANODE = 1;
  const NetID CATHODE = 0;
  const double vDiode = 0.6;

  DiodeShockley::stamp(mna, ANODE, CATHODE, vDiode, params);

  // Should not throw and should modify MNA system
  // (Detailed MNA matrix checks would require exposing internal state)
}

/** @test Stamp with reverse bias. */
TEST(DiodeShockley, StampReverseBias) {
  MnaSystem mna(3);
  DiodeShockleyParams params;
  const NetID ANODE = 1;
  const NetID CATHODE = 0;
  const double vDiode = -0.5; // Reverse bias

  DiodeShockley::stamp(mna, ANODE, CATHODE, vDiode, params);

  // Should handle negative voltages without issue
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test Silicon diode turn-on voltage. */
TEST(DiodeShockley, SiliconTurnOnVoltage) {
  DiodeShockleyParams params; // Default = silicon diode

  // Below 0.6V: relatively low current
  double iLow = DiodeShockley::current(0.5, params);
  EXPECT_LT(iLow, 10e-6); // Less than 10 uA

  // Above 0.7V: significant current
  double iHigh = DiodeShockley::current(0.7, params);
  EXPECT_GT(iHigh, 1e-3); // More than 1mA
}

/** @test Temperature effect (via Vt). */
TEST(DiodeShockley, TemperatureEffect) {
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
TEST(DiodeShockley, ExponentialScaling) {
  DiodeShockleyParams params;

  // Current should scale exponentially: every 60mV decade in ideal diode
  double i1 = DiodeShockley::current(0.6, params);
  double i2 = DiodeShockley::current(0.66, params); // +60mV

  double ratio = i2 / i1;

  // Should be approximately 10x (one decade)
  EXPECT_NEAR(ratio, 10.0, 1.0); // Within 10% tolerance
}
