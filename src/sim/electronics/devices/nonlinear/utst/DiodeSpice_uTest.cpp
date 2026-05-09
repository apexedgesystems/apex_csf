/**
 * @file DiodeSpice_uTest.cpp
 * @brief Unit tests for DiodeSpice.
 */

#include "src/sim/electronics/devices/nonlinear/inc/DiodeSpice.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::DiodeSpice;
using sim::electronics::devices::nonlinear::DiodeSpiceParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(DiodeSpiceTest, DefaultParameters) {
  DiodeSpiceParams params;
  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
  EXPECT_DOUBLE_EQ(params.Rs, 0.0);
  EXPECT_DOUBLE_EQ(params.Cj0, 0.0);
  EXPECT_DOUBLE_EQ(params.Vj, 0.7);
  EXPECT_DOUBLE_EQ(params.M, 0.5);
}

/** @test */
TEST(DiodeSpiceTest, CustomParameters) {
  DiodeSpiceParams params{.Is = 1e-15, .n = 1.2, .Vt = 0.025, .Rs = 2.0, .Cj0 = 10e-12};
  EXPECT_DOUBLE_EQ(params.Is, 1e-15);
  EXPECT_DOUBLE_EQ(params.n, 1.2);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
  EXPECT_DOUBLE_EQ(params.Rs, 2.0);
  EXPECT_DOUBLE_EQ(params.Cj0, 10e-12);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(DiodeSpiceTest, CurrentNoSeriesResistance) {
  DiodeSpiceParams params{.Rs = 0.0};
  const double V = 0.7;
  const double I = DiodeSpice::current(V, params);

  // Should match DiodeShockley when Rs=0
  const double EXPECTED = params.Is * (std::exp(V / (params.n * params.Vt)) - 1.0);
  EXPECT_NEAR(I, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(DiodeSpiceTest, CurrentWithSeriesResistance) {
  DiodeSpiceParams params{.Is = 1e-14, .Rs = 1.0};
  const double V = 0.7;
  const double I = DiodeSpice::current(V, params);

  // With series R, current should be lower than ideal
  const double I_IDEAL = params.Is * (std::exp(V / (params.n * params.Vt)) - 1.0);
  EXPECT_LT(I, I_IDEAL);
  EXPECT_GT(I, 0.0);
}

/** @test */
TEST(DiodeSpiceTest, CurrentForwardBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double V = 0.7;
  const double I = DiodeSpice::current(V, params);

  // Should be in mA range for silicon at 0.7V
  EXPECT_GT(I, 1e-4);
  EXPECT_LT(I, 1.0);
}

/** @test */
TEST(DiodeSpiceTest, CurrentReverseBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double V = -2.0;
  const double I = DiodeSpice::current(V, params);

  // Should be small negative (leakage current)
  EXPECT_LT(I, 0.0);
  EXPECT_GT(I, -params.Is * 1.1);
}

/** @test */
TEST(DiodeSpiceTest, CurrentZeroVoltage) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double V = 0.0;
  const double I = DiodeSpice::current(V, params);

  EXPECT_NEAR(I, 0.0, params.Is);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(DiodeSpiceTest, ConductanceNoSeriesResistance) {
  DiodeSpiceParams params{.Rs = 0.0};
  const double V = 0.7;
  const double G = DiodeSpice::conductance(V, params);

  // Should match ideal diode conductance when Rs=0
  const double EXPECTED =
      (params.Is / (params.n * params.Vt)) * std::exp(V / (params.n * params.Vt));
  EXPECT_NEAR(G, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(DiodeSpiceTest, ConductanceWithSeriesResistance) {
  DiodeSpiceParams params{.Is = 1e-14, .Rs = 1.0};
  const double V = 0.7;
  const double G = DiodeSpice::conductance(V, params);

  // With series R, conductance should be reduced: g = gj/(1 + gj*Rs)
  const double G_IDEAL = (params.Is / (params.n * params.Vt)) * std::exp(V / (params.n * params.Vt));
  EXPECT_LT(G, G_IDEAL);
  EXPECT_GT(G, 0.0);
}

/** @test */
TEST(DiodeSpiceTest, ConductanceForwardBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double V = 0.7;
  const double G = DiodeSpice::conductance(V, params);

  EXPECT_GT(G, 1e-3); // Should be significant in forward bias
}

/** @test */
TEST(DiodeSpiceTest, ConductanceReverseBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double V = -2.0;
  const double G = DiodeSpice::conductance(V, params);

  EXPECT_GT(G, 0.0);
  EXPECT_LT(G, 1e-9); // Very small in reverse bias
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(DiodeSpiceTest, ConductanceNumericalDerivativeNoRs) {
  DiodeSpiceParams params{.Rs = 0.0};
  const double V = 0.7;
  const double DV = 1e-8;

  const double G_ANALYTICAL = DiodeSpice::conductance(V, params);

  const double I1 = DiodeSpice::current(V - DV, params);
  const double I2 = DiodeSpice::current(V + DV, params);
  const double G_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(G_ANALYTICAL, G_NUMERICAL, std::abs(G_NUMERICAL) * 0.01);
}

/** @test */
TEST(DiodeSpiceTest, ConductanceNumericalDerivativeWithRs) {
  DiodeSpiceParams params{.Rs = 1.0};
  const double V = 0.7;
  const double DV = 1e-8;

  const double G_ANALYTICAL = DiodeSpice::conductance(V, params);

  const double I1 = DiodeSpice::current(V - DV, params);
  const double I2 = DiodeSpice::current(V + DV, params);
  const double G_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(G_ANALYTICAL, G_NUMERICAL, std::abs(G_NUMERICAL) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(DiodeSpiceTest, StampForwardBias) {
  MnaSystem mna(3);
  DiodeSpiceParams params{.Rs = 0.5};
  const NetID ANODE = 1;
  const NetID CATHODE = 2;
  const double V = 0.7;

  // Should stamp linearized conductance and current source
  DiodeSpice::stamp(mna, ANODE, CATHODE, V, params);
}

/** @test */
TEST(DiodeSpiceTest, StampReverseBias) {
  MnaSystem mna(3);
  DiodeSpiceParams params{.Rs = 0.5};
  const NetID ANODE = 1;
  const NetID CATHODE = 2;
  const double V = -2.0;

  // Should stamp small conductance (reverse bias)
  DiodeSpice::stamp(mna, ANODE, CATHODE, V, params);
}

/* ----------------------------- Junction Capacitance ----------------------------- */

/** @test */
TEST(DiodeSpiceTest, JunctionCapacitanceZeroBias) {
  DiodeSpiceParams params{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};
  const double V = 0.0;
  const double C = DiodeSpice::junctionCapacitance(V, params);

  // At zero bias, C = Cj0
  EXPECT_DOUBLE_EQ(C, params.Cj0);
}

/** @test */
TEST(DiodeSpiceTest, JunctionCapacitanceReverseBias) {
  DiodeSpiceParams params{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};
  const double V = -1.0;
  const double C = DiodeSpice::junctionCapacitance(V, params);

  // Reverse bias: C = Cj0 / (1 - V/Vj)^M -> should be less than Cj0
  EXPECT_LT(C, params.Cj0);
  EXPECT_GT(C, 0.0);
}

/** @test */
TEST(DiodeSpiceTest, JunctionCapacitanceForwardBias) {
  DiodeSpiceParams params{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};
  const double V = 0.5;
  const double C = DiodeSpice::junctionCapacitance(V, params);

  // Forward bias: C increases (capacitance higher)
  EXPECT_GT(C, params.Cj0);
}

/** @test */
TEST(DiodeSpiceTest, JunctionCapacitanceGradingCoefficient) {
  DiodeSpiceParams abrupt{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.33}; // Abrupt junction
  DiodeSpiceParams linear{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};  // Linear junction
  const double V = -1.0;

  const double C_ABRUPT = DiodeSpice::junctionCapacitance(V, abrupt);
  const double C_LINEAR = DiodeSpice::junctionCapacitance(V, linear);

  // Different grading coefficients give different capacitances
  EXPECT_NE(C_ABRUPT, C_LINEAR);
}

/** @test */
TEST(DiodeSpiceTest, JunctionCapacitanceNoCj0) {
  DiodeSpiceParams params{.Cj0 = 0.0};
  const double V = 0.5;
  const double C = DiodeSpice::junctionCapacitance(V, params);

  // No junction capacitance specified
  EXPECT_DOUBLE_EQ(C, 0.0);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(DiodeSpiceTest, SeriesResistanceReducesCurrent) {
  DiodeSpiceParams noRs{.Rs = 0.0};
  DiodeSpiceParams withRs{.Rs = 2.0};
  const double V = 0.7;

  const double I_NO_RS = DiodeSpice::current(V, noRs);
  const double I_WITH_RS = DiodeSpice::current(V, withRs);

  // Series resistance should reduce current
  EXPECT_GT(I_NO_RS, I_WITH_RS);
}

/** @test */
TEST(DiodeSpiceTest, SeriesResistanceReducesConductance) {
  DiodeSpiceParams noRs{.Rs = 0.0};
  DiodeSpiceParams withRs{.Rs = 2.0};
  const double V = 0.7;

  const double G_NO_RS = DiodeSpice::conductance(V, noRs);
  const double G_WITH_RS = DiodeSpice::conductance(V, withRs);

  // Series resistance should reduce conductance
  EXPECT_GT(G_NO_RS, G_WITH_RS);
}

/** @test */
TEST(DiodeSpiceTest, HighSeriesResistanceLimitsCurrent) {
  DiodeSpiceParams params{.Is = 1e-14, .Rs = 100.0}; // Very high Rs
  const double V = 5.0;                              // High forward voltage
  const double I = DiodeSpice::current(V, params);

  // With high Rs, current is limited even at high voltage
  EXPECT_LT(I, 0.1); // Less than 100mA
}
