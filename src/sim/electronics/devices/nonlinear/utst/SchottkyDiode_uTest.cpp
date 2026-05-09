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
TEST(SchottkyDiodeTest, DefaultParameters) {
  SchottkyDiodeParams params;
  EXPECT_DOUBLE_EQ(params.Is, 1e-12);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
  EXPECT_DOUBLE_EQ(params.Rs, 0.0);
}

/** @test */
TEST(SchottkyDiodeTest, CustomParameters) {
  SchottkyDiodeParams params{.Is = 5e-12, .n = 1.05, .Vt = 0.025, .Rs = 0.5};
  EXPECT_DOUBLE_EQ(params.Is, 5e-12);
  EXPECT_DOUBLE_EQ(params.n, 1.05);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
  EXPECT_DOUBLE_EQ(params.Rs, 0.5);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(SchottkyDiodeTest, CurrentNoSeriesResistance) {
  SchottkyDiodeParams params{.Rs = 0.0};
  const double V = 0.3;
  const double I = SchottkyDiode::current(V, params);

  // Should match Shockley equation when Rs=0
  const double EXPECTED = params.Is * (std::exp(V / (params.n * params.Vt)) - 1.0);
  EXPECT_NEAR(I, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(SchottkyDiodeTest, CurrentWithSeriesResistance) {
  SchottkyDiodeParams params{.Is = 1e-12, .Rs = 0.5};
  const double V = 0.3;
  const double I = SchottkyDiode::current(V, params);

  // With series R, current should be lower than ideal
  const double I_IDEAL = params.Is * (std::exp(V / (params.n * params.Vt)) - 1.0);
  EXPECT_LT(I, I_IDEAL);
  EXPECT_GT(I, 0.0);
}

/** @test */
TEST(SchottkyDiodeTest, CurrentForwardBias) {
  SchottkyDiodeParams params{.Is = 1e-9, .Rs = 0.5}; // Higher Is for mA at 0.3V
  const double V = 0.3;                              // Lower Vf than silicon PN
  const double I = SchottkyDiode::current(V, params);

  // Should be in mA range for Schottky at 0.3V
  EXPECT_GT(I, 1e-4);
  EXPECT_LT(I, 1.0);
}

/** @test */
TEST(SchottkyDiodeTest, CurrentReverseBias) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double V = -2.0;
  const double I = SchottkyDiode::current(V, params);

  // Higher leakage than PN junction (larger Is)
  EXPECT_LT(I, 0.0);
  EXPECT_GT(I, -params.Is * 1.1);
}

/** @test */
TEST(SchottkyDiodeTest, CurrentZeroVoltage) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double V = 0.0;
  const double I = SchottkyDiode::current(V, params);

  EXPECT_NEAR(I, 0.0, params.Is);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(SchottkyDiodeTest, ConductanceNoSeriesResistance) {
  SchottkyDiodeParams params{.Rs = 0.0};
  const double V = 0.3;
  const double G = SchottkyDiode::conductance(V, params);

  // Should match ideal diode conductance when Rs=0
  const double EXPECTED =
      (params.Is / (params.n * params.Vt)) * std::exp(V / (params.n * params.Vt));
  EXPECT_NEAR(G, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(SchottkyDiodeTest, ConductanceWithSeriesResistance) {
  SchottkyDiodeParams params{.Is = 1e-12, .Rs = 0.5};
  const double V = 0.3;
  const double G = SchottkyDiode::conductance(V, params);

  // With series R, conductance should be reduced: g = gj/(1 + gj*Rs)
  const double G_IDEAL = (params.Is / (params.n * params.Vt)) * std::exp(V / (params.n * params.Vt));
  EXPECT_LT(G, G_IDEAL);
  EXPECT_GT(G, 0.0);
}

/** @test */
TEST(SchottkyDiodeTest, ConductanceForwardBias) {
  SchottkyDiodeParams params{.Is = 1e-9, .Rs = 0.5}; // Higher Is for mA at 0.3V
  const double V = 0.3;
  const double G = SchottkyDiode::conductance(V, params);

  EXPECT_GT(G, 1e-3); // Should be significant in forward bias
}

/** @test */
TEST(SchottkyDiodeTest, ConductanceReverseBias) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double V = -2.0;
  const double G = SchottkyDiode::conductance(V, params);

  EXPECT_GT(G, 0.0);
  EXPECT_LT(G, 1e-9); // Very small in reverse bias
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(SchottkyDiodeTest, ConductanceNumericalDerivativeNoRs) {
  SchottkyDiodeParams params{.Rs = 0.0};
  const double V = 0.3;
  const double DV = 1e-8;

  const double G_ANALYTICAL = SchottkyDiode::conductance(V, params);

  const double I1 = SchottkyDiode::current(V - DV, params);
  const double I2 = SchottkyDiode::current(V + DV, params);
  const double G_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(G_ANALYTICAL, G_NUMERICAL, std::abs(G_NUMERICAL) * 0.01);
}

/** @test */
TEST(SchottkyDiodeTest, ConductanceNumericalDerivativeWithRs) {
  SchottkyDiodeParams params{.Rs = 0.5};
  const double V = 0.3;
  const double DV = 1e-8;

  const double G_ANALYTICAL = SchottkyDiode::conductance(V, params);

  const double I1 = SchottkyDiode::current(V - DV, params);
  const double I2 = SchottkyDiode::current(V + DV, params);
  const double G_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(G_ANALYTICAL, G_NUMERICAL, std::abs(G_NUMERICAL) * 0.01);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(SchottkyDiodeTest, StampForwardBias) {
  MnaSystem mna(3);
  SchottkyDiodeParams params{.Rs = 0.5};
  const NetID ANODE = 1;
  const NetID CATHODE = 2;
  const double V = 0.3;

  // Should stamp linearized conductance and current source
  SchottkyDiode::stamp(mna, ANODE, CATHODE, V, params);
}

/** @test */
TEST(SchottkyDiodeTest, StampReverseBias) {
  MnaSystem mna(3);
  SchottkyDiodeParams params{.Rs = 0.5};
  const NetID ANODE = 1;
  const NetID CATHODE = 2;
  const double V = -2.0;

  // Should stamp small conductance (reverse bias)
  SchottkyDiode::stamp(mna, ANODE, CATHODE, V, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(SchottkyDiodeTest, LowerForwardVoltageThanSilicon) {
  SchottkyDiodeParams schottky{.Is = 1e-9, .Rs = 0.0}; // Schottky: much higher Is
  SchottkyDiodeParams silicon{.Is = 1e-14, .Rs = 0.0}; // Silicon PN: lower Is

  // At same current, Schottky should have lower voltage
  const double TARGET_CURRENT = 1e-3; // 1mA

  // Find voltage for each diode to reach 1mA
  double vSchottky = 0.0;
  double vSilicon = 0.0;

  for (double v = 0.0; v < 1.0; v += 0.01) {
    if (vSchottky == 0.0 && SchottkyDiode::current(v, schottky) >= TARGET_CURRENT) {
      vSchottky = v;
    }
    if (vSilicon == 0.0 && SchottkyDiode::current(v, silicon) >= TARGET_CURRENT) {
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
TEST(SchottkyDiodeTest, HigherLeakageCurrentThanSilicon) {
  SchottkyDiodeParams schottky{.Is = 1e-12}; // Typical Schottky
  SchottkyDiodeParams silicon{.Is = 1e-14};  // Typical silicon PN

  const double V = -1.0; // Reverse bias

  const double I_SCHOTTKY = SchottkyDiode::current(V, schottky);
  const double I_SILICON = SchottkyDiode::current(V, silicon);

  // Schottky should have higher reverse leakage (more negative)
  EXPECT_LT(I_SCHOTTKY, I_SILICON);
  EXPECT_NEAR(std::abs(I_SCHOTTKY), schottky.Is, schottky.Is * 0.01);
  EXPECT_NEAR(std::abs(I_SILICON), silicon.Is, silicon.Is * 0.01);
}

/** @test */
TEST(SchottkyDiodeTest, SeriesResistanceReducesCurrent) {
  SchottkyDiodeParams noRs{.Rs = 0.0};
  SchottkyDiodeParams withRs{.Rs = 1.0};
  const double V = 0.3;

  const double I_NO_RS = SchottkyDiode::current(V, noRs);
  const double I_WITH_RS = SchottkyDiode::current(V, withRs);

  // Series resistance should reduce current
  EXPECT_GT(I_NO_RS, I_WITH_RS);
}

/** @test */
TEST(SchottkyDiodeTest, SeriesResistanceReducesConductance) {
  SchottkyDiodeParams noRs{.Rs = 0.0};
  SchottkyDiodeParams withRs{.Rs = 1.0};
  const double V = 0.3;

  const double G_NO_RS = SchottkyDiode::conductance(V, noRs);
  const double G_WITH_RS = SchottkyDiode::conductance(V, withRs);

  // Series resistance should reduce conductance
  EXPECT_GT(G_NO_RS, G_WITH_RS);
}
