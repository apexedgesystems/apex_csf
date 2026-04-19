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
TEST(DiodeSpice, DefaultParameters) {
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
TEST(DiodeSpice, CustomParameters) {
  DiodeSpiceParams params{.Is = 1e-15, .n = 1.2, .Vt = 0.025, .Rs = 2.0, .Cj0 = 10e-12};
  EXPECT_DOUBLE_EQ(params.Is, 1e-15);
  EXPECT_DOUBLE_EQ(params.n, 1.2);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
  EXPECT_DOUBLE_EQ(params.Rs, 2.0);
  EXPECT_DOUBLE_EQ(params.Cj0, 10e-12);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(DiodeSpice, CurrentNoSeriesResistance) {
  DiodeSpiceParams params{.Rs = 0.0};
  const double v = 0.7;
  const double i = DiodeSpice::current(v, params);

  // Should match DiodeShockley when Rs=0
  const double expected = params.Is * (std::exp(v / (params.n * params.Vt)) - 1.0);
  EXPECT_NEAR(i, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(DiodeSpice, CurrentWithSeriesResistance) {
  DiodeSpiceParams params{.Is = 1e-14, .Rs = 1.0};
  const double v = 0.7;
  const double i = DiodeSpice::current(v, params);

  // With series R, current should be lower than ideal
  const double iIdeal = params.Is * (std::exp(v / (params.n * params.Vt)) - 1.0);
  EXPECT_LT(i, iIdeal);
  EXPECT_GT(i, 0.0);
}

/** @test */
TEST(DiodeSpice, CurrentForwardBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double v = 0.7;
  const double i = DiodeSpice::current(v, params);

  // Should be in mA range for silicon at 0.7V
  EXPECT_GT(i, 1e-4);
  EXPECT_LT(i, 1.0);
}

/** @test */
TEST(DiodeSpice, CurrentReverseBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double v = -2.0;
  const double i = DiodeSpice::current(v, params);

  // Should be small negative (leakage current)
  EXPECT_LT(i, 0.0);
  EXPECT_GT(i, -params.Is * 1.1);
}

/** @test */
TEST(DiodeSpice, CurrentZeroVoltage) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double v = 0.0;
  const double i = DiodeSpice::current(v, params);

  EXPECT_NEAR(i, 0.0, params.Is);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(DiodeSpice, ConductanceNoSeriesResistance) {
  DiodeSpiceParams params{.Rs = 0.0};
  const double v = 0.7;
  const double g = DiodeSpice::conductance(v, params);

  // Should match ideal diode conductance when Rs=0
  const double expected =
      (params.Is / (params.n * params.Vt)) * std::exp(v / (params.n * params.Vt));
  EXPECT_NEAR(g, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(DiodeSpice, ConductanceWithSeriesResistance) {
  DiodeSpiceParams params{.Is = 1e-14, .Rs = 1.0};
  const double v = 0.7;
  const double g = DiodeSpice::conductance(v, params);

  // With series R, conductance should be reduced: g = gj/(1 + gj*Rs)
  const double gIdeal = (params.Is / (params.n * params.Vt)) * std::exp(v / (params.n * params.Vt));
  EXPECT_LT(g, gIdeal);
  EXPECT_GT(g, 0.0);
}

/** @test */
TEST(DiodeSpice, ConductanceForwardBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double v = 0.7;
  const double g = DiodeSpice::conductance(v, params);

  EXPECT_GT(g, 1e-3); // Should be significant in forward bias
}

/** @test */
TEST(DiodeSpice, ConductanceReverseBias) {
  DiodeSpiceParams params{.Rs = 0.5};
  const double v = -2.0;
  const double g = DiodeSpice::conductance(v, params);

  EXPECT_GT(g, 0.0);
  EXPECT_LT(g, 1e-9); // Very small in reverse bias
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(DiodeSpice, ConductanceNumericalDerivativeNoRs) {
  DiodeSpiceParams params{.Rs = 0.0};
  const double v = 0.7;
  const double dv = 1e-8;

  const double gAnalytical = DiodeSpice::conductance(v, params);

  const double i1 = DiodeSpice::current(v - dv, params);
  const double i2 = DiodeSpice::current(v + dv, params);
  const double gNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
}

/** @test */
TEST(DiodeSpice, ConductanceNumericalDerivativeWithRs) {
  DiodeSpiceParams params{.Rs = 1.0};
  const double v = 0.7;
  const double dv = 1e-8;

  const double gAnalytical = DiodeSpice::conductance(v, params);

  const double i1 = DiodeSpice::current(v - dv, params);
  const double i2 = DiodeSpice::current(v + dv, params);
  const double gNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(DiodeSpice, StampForwardBias) {
  MnaSystem mna(3);
  DiodeSpiceParams params{.Rs = 0.5};
  const NetID anode = 1;
  const NetID cathode = 2;
  const double v = 0.7;

  // Should stamp linearized conductance and current source
  DiodeSpice::stamp(mna, anode, cathode, v, params);
}

/** @test */
TEST(DiodeSpice, StampReverseBias) {
  MnaSystem mna(3);
  DiodeSpiceParams params{.Rs = 0.5};
  const NetID anode = 1;
  const NetID cathode = 2;
  const double v = -2.0;

  // Should stamp small conductance (reverse bias)
  DiodeSpice::stamp(mna, anode, cathode, v, params);
}

/* ----------------------------- Junction Capacitance ----------------------------- */

/** @test */
TEST(DiodeSpice, JunctionCapacitanceZeroBias) {
  DiodeSpiceParams params{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};
  const double v = 0.0;
  const double c = DiodeSpice::junctionCapacitance(v, params);

  // At zero bias, C = Cj0
  EXPECT_DOUBLE_EQ(c, params.Cj0);
}

/** @test */
TEST(DiodeSpice, JunctionCapacitanceReverseBias) {
  DiodeSpiceParams params{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};
  const double v = -1.0;
  const double c = DiodeSpice::junctionCapacitance(v, params);

  // Reverse bias: C = Cj0 / (1 - V/Vj)^M -> should be less than Cj0
  EXPECT_LT(c, params.Cj0);
  EXPECT_GT(c, 0.0);
}

/** @test */
TEST(DiodeSpice, JunctionCapacitanceForwardBias) {
  DiodeSpiceParams params{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};
  const double v = 0.5;
  const double c = DiodeSpice::junctionCapacitance(v, params);

  // Forward bias: C increases (capacitance higher)
  EXPECT_GT(c, params.Cj0);
}

/** @test */
TEST(DiodeSpice, JunctionCapacitanceGradingCoefficient) {
  DiodeSpiceParams abrupt{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.33}; // Abrupt junction
  DiodeSpiceParams linear{.Cj0 = 10e-12, .Vj = 0.7, .M = 0.5};  // Linear junction
  const double v = -1.0;

  const double cAbrupt = DiodeSpice::junctionCapacitance(v, abrupt);
  const double cLinear = DiodeSpice::junctionCapacitance(v, linear);

  // Different grading coefficients give different capacitances
  EXPECT_NE(cAbrupt, cLinear);
}

/** @test */
TEST(DiodeSpice, JunctionCapacitanceNoCj0) {
  DiodeSpiceParams params{.Cj0 = 0.0};
  const double v = 0.5;
  const double c = DiodeSpice::junctionCapacitance(v, params);

  // No junction capacitance specified
  EXPECT_DOUBLE_EQ(c, 0.0);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(DiodeSpice, SeriesResistanceReducesCurrent) {
  DiodeSpiceParams noRs{.Rs = 0.0};
  DiodeSpiceParams withRs{.Rs = 2.0};
  const double v = 0.7;

  const double iNoRs = DiodeSpice::current(v, noRs);
  const double iWithRs = DiodeSpice::current(v, withRs);

  // Series resistance should reduce current
  EXPECT_GT(iNoRs, iWithRs);
}

/** @test */
TEST(DiodeSpice, SeriesResistanceReducesConductance) {
  DiodeSpiceParams noRs{.Rs = 0.0};
  DiodeSpiceParams withRs{.Rs = 2.0};
  const double v = 0.7;

  const double gNoRs = DiodeSpice::conductance(v, noRs);
  const double gWithRs = DiodeSpice::conductance(v, withRs);

  // Series resistance should reduce conductance
  EXPECT_GT(gNoRs, gWithRs);
}

/** @test */
TEST(DiodeSpice, HighSeriesResistanceLimitsCurrent) {
  DiodeSpiceParams params{.Is = 1e-14, .Rs = 100.0}; // Very high Rs
  const double v = 5.0;                              // High forward voltage
  const double i = DiodeSpice::current(v, params);

  // With high Rs, current is limited even at high voltage
  EXPECT_LT(i, 0.1); // Less than 100mA
}
