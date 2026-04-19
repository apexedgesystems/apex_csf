/**
 * @file SchottkyDiode_uTest.cpp
 * @brief Unit tests for SchottkyDiode.
 */

#include "src/sim/electronics/devices/nonlinear/inc/SchottkyDiode.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;
using sim::electronics::devices::nonlinear::SchottkyDiode;
using sim::electronics::devices::nonlinear::SchottkyDiodeParams;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(SchottkyDiode, DefaultParameters) {
  SchottkyDiodeParams params;
  EXPECT_DOUBLE_EQ(params.Is, 1e-12);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
  EXPECT_DOUBLE_EQ(params.Rs, 0.0);
}

/** @test */
TEST(SchottkyDiode, CustomParameters) {
  SchottkyDiodeParams params{.Is = 5e-12, .n = 1.05, .Vt = 0.025, .Rs = 0.5};
  EXPECT_DOUBLE_EQ(params.Is, 5e-12);
  EXPECT_DOUBLE_EQ(params.n, 1.05);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
  EXPECT_DOUBLE_EQ(params.Rs, 0.5);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(SchottkyDiode, CurrentNoSeriesResistance) {
  SchottkyDiodeParams params{.Rs = 0.0};
  const double v = 0.3;
  const double i = SchottkyDiode::current(v, params);

  // Should match Shockley equation when Rs=0
  const double expected = params.Is * (std::exp(v / (params.n * params.Vt)) - 1.0);
  EXPECT_NEAR(i, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(SchottkyDiode, CurrentWithSeriesResistance) {
  SchottkyDiodeParams params{.Is = 1e-12, .Rs = 0.5};
  const double v = 0.3;
  const double i = SchottkyDiode::current(v, params);

  // With series R, current should be lower than ideal
  const double iIdeal = params.Is * (std::exp(v / (params.n * params.Vt)) - 1.0);
  EXPECT_LT(i, iIdeal);
  EXPECT_GT(i, 0.0);
}

/** @test */
TEST(SchottkyDiode, CurrentForwardBias) {
  SchottkyDiodeParams params{.Is = 1e-9, .Rs = 0.5}; // Higher Is for mA at 0.3V
  const double v = 0.3;                              // Lower Vf than silicon PN
  const double i = SchottkyDiode::current(v, params);

  // Should be in mA range for Schottky at 0.3V
  EXPECT_GT(i, 1e-4);
  EXPECT_LT(i, 1.0);
}

/** @test */
TEST(SchottkyDiode, CurrentReverseBias) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double v = -2.0;
  const double i = SchottkyDiode::current(v, params);

  // Higher leakage than PN junction (larger Is)
  EXPECT_LT(i, 0.0);
  EXPECT_GT(i, -params.Is * 1.1);
}

/** @test */
TEST(SchottkyDiode, CurrentZeroVoltage) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double v = 0.0;
  const double i = SchottkyDiode::current(v, params);

  EXPECT_NEAR(i, 0.0, params.Is);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(SchottkyDiode, ConductanceNoSeriesResistance) {
  SchottkyDiodeParams params{.Rs = 0.0};
  const double v = 0.3;
  const double g = SchottkyDiode::conductance(v, params);

  // Should match ideal diode conductance when Rs=0
  const double expected =
      (params.Is / (params.n * params.Vt)) * std::exp(v / (params.n * params.Vt));
  EXPECT_NEAR(g, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(SchottkyDiode, ConductanceWithSeriesResistance) {
  SchottkyDiodeParams params{.Is = 1e-12, .Rs = 0.5};
  const double v = 0.3;
  const double g = SchottkyDiode::conductance(v, params);

  // With series R, conductance should be reduced: g = gj/(1 + gj*Rs)
  const double gIdeal = (params.Is / (params.n * params.Vt)) * std::exp(v / (params.n * params.Vt));
  EXPECT_LT(g, gIdeal);
  EXPECT_GT(g, 0.0);
}

/** @test */
TEST(SchottkyDiode, ConductanceForwardBias) {
  SchottkyDiodeParams params{.Is = 1e-9, .Rs = 0.5}; // Higher Is for mA at 0.3V
  const double v = 0.3;
  const double g = SchottkyDiode::conductance(v, params);

  EXPECT_GT(g, 1e-3); // Should be significant in forward bias
}

/** @test */
TEST(SchottkyDiode, ConductanceReverseBias) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double v = -2.0;
  const double g = SchottkyDiode::conductance(v, params);

  EXPECT_GT(g, 0.0);
  EXPECT_LT(g, 1e-9); // Very small in reverse bias
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(SchottkyDiode, ConductanceNumericalDerivativeNoRs) {
  SchottkyDiodeParams params{.Rs = 0.0};
  const double v = 0.3;
  const double dv = 1e-8;

  const double gAnalytical = SchottkyDiode::conductance(v, params);

  const double i1 = SchottkyDiode::current(v - dv, params);
  const double i2 = SchottkyDiode::current(v + dv, params);
  const double gNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
}

/** @test */
TEST(SchottkyDiode, ConductanceNumericalDerivativeWithRs) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double v = 0.3;
  const double dv = 1e-8;

  const double gAnalytical = SchottkyDiode::conductance(v, params);

  const double i1 = SchottkyDiode::current(v - dv, params);
  const double i2 = SchottkyDiode::current(v + dv, params);
  const double gNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(SchottkyDiode, StampForwardBias) {
  MnaSystem mna(3);
  SchottkyDiodeParams params{.Rs = 0.5};
  const NetID anode = 1;
  const NetID cathode = 2;
  const double v = 0.3;

  // Should stamp linearized conductance and current source
  SchottkyDiode::stamp(mna, anode, cathode, v, params);
}

/** @test */
TEST(SchottkyDiode, StampReverseBias) {
  MnaSystem mna(3);
  SchottkyDiodeParams params{.Rs = 0.5};
  const NetID anode = 1;
  const NetID cathode = 2;
  const double v = -2.0;

  // Should stamp small conductance (reverse bias)
  SchottkyDiode::stamp(mna, anode, cathode, v, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(SchottkyDiode, LowerForwardVoltageThanSilicon) {
  SchottkyDiodeParams schottky{.Is = 1e-9, .Rs = 0.0}; // Schottky: much higher Is
  SchottkyDiodeParams silicon{.Is = 1e-14, .Rs = 0.0}; // Silicon PN: lower Is

  // At same current, Schottky should have lower voltage
  const double targetCurrent = 1e-3; // 1mA

  // Find voltage for each diode to reach 1mA
  double vSchottky = 0.0;
  double vSilicon = 0.0;

  for (double v = 0.0; v < 1.0; v += 0.01) {
    if (vSchottky == 0.0 && SchottkyDiode::current(v, schottky) >= targetCurrent) {
      vSchottky = v;
    }
    if (vSilicon == 0.0 && SchottkyDiode::current(v, silicon) >= targetCurrent) {
      vSilicon = v;
    }
    if (vSchottky > 0.0 && vSilicon > 0.0) {
      break;
    }
  }

  // Schottky should reach 1mA at lower voltage
  EXPECT_LT(vSchottky, vSilicon);
  EXPECT_LT(vSchottky, 0.4); // Schottky typically ~0.3V
  EXPECT_GT(vSilicon, 0.6);  // Silicon typically ~0.7V
}

/** @test */
TEST(SchottkyDiode, HigherLeakageCurrentThanSilicon) {
  SchottkyDiodeParams schottky{.Is = 1e-12}; // Typical Schottky
  SchottkyDiodeParams silicon{.Is = 1e-14};  // Typical silicon PN

  const double v = -1.0; // Reverse bias

  const double iSchottky = SchottkyDiode::current(v, schottky);
  const double iSilicon = SchottkyDiode::current(v, silicon);

  // Schottky should have higher reverse leakage (more negative)
  EXPECT_LT(iSchottky, iSilicon);
  EXPECT_NEAR(std::abs(iSchottky), schottky.Is, schottky.Is * 0.01);
  EXPECT_NEAR(std::abs(iSilicon), silicon.Is, silicon.Is * 0.01);
}

/** @test */
TEST(SchottkyDiode, SeriesResistanceReducesCurrent) {
  SchottkyDiodeParams noRs{.Rs = 0.0};
  SchottkyDiodeParams withRs{.Rs = 1.0};
  const double v = 0.3;

  const double iNoRs = SchottkyDiode::current(v, noRs);
  const double iWithRs = SchottkyDiode::current(v, withRs);

  // Series resistance should reduce current
  EXPECT_GT(iNoRs, iWithRs);
}

/** @test */
TEST(SchottkyDiode, SeriesResistanceReducesConductance) {
  SchottkyDiodeParams noRs{.Rs = 0.0};
  SchottkyDiodeParams withRs{.Rs = 1.0};
  const double v = 0.3;

  const double gNoRs = SchottkyDiode::conductance(v, noRs);
  const double gWithRs = SchottkyDiode::conductance(v, withRs);

  // Series resistance should reduce conductance
  EXPECT_GT(gNoRs, gWithRs);
}
