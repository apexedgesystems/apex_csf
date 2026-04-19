/**
 * @file ZenerDiode_uTest.cpp
 * @brief Unit tests for ZenerDiode.
 */

#include "src/sim/electronics/devices/nonlinear/inc/ZenerDiode.hpp"

#include <gtest/gtest.h>

#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;
using sim::electronics::devices::nonlinear::ZenerDiode;
using sim::electronics::devices::nonlinear::ZenerDiodeParams;

/* ----------------------------- Default Construction ----------------------------- */

/** @test */
TEST(ZenerDiode, DefaultParameters) {
  ZenerDiodeParams params;
  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
  EXPECT_DOUBLE_EQ(params.Vz, 5.1);
  EXPECT_DOUBLE_EQ(params.Ibv, 1e-3);
  EXPECT_DOUBLE_EQ(params.Vbv, 0.1);
}

/** @test */
TEST(ZenerDiode, CustomParameters) {
  ZenerDiodeParams params{.Is = 1e-15, .n = 1.2, .Vt = 0.025, .Vz = 3.3, .Ibv = 5e-3, .Vbv = 0.05};
  EXPECT_DOUBLE_EQ(params.Is, 1e-15);
  EXPECT_DOUBLE_EQ(params.n, 1.2);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
  EXPECT_DOUBLE_EQ(params.Vz, 3.3);
  EXPECT_DOUBLE_EQ(params.Ibv, 5e-3);
  EXPECT_DOUBLE_EQ(params.Vbv, 0.05);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test */
TEST(ZenerDiode, CurrentForwardBias) {
  ZenerDiodeParams params;
  const double v = 0.7; // Forward bias (silicon turn-on)
  const double i = ZenerDiode::current(v, params);

  // Should be exponentially large (mA range)
  EXPECT_GT(i, 1e-3);
  EXPECT_LT(i, 1.0);

  // Check exponential relationship
  const double expected = params.Is * (std::exp(v / (params.n * params.Vt)) - 1.0);
  EXPECT_NEAR(i, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(ZenerDiode, CurrentReverseBias) {
  ZenerDiodeParams params;
  const double v = -2.0; // Reverse bias (before breakdown)
  const double i = ZenerDiode::current(v, params);

  // Should be small negative (leakage current ~Is)
  EXPECT_LT(i, 0.0);
  EXPECT_GT(i, -params.Is * 1.1); // Close to -Is
}

/** @test */
TEST(ZenerDiode, CurrentBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3};
  const double v = -5.5; // Past breakdown voltage
  const double i = ZenerDiode::current(v, params);

  // Should be large negative (regulation current)
  EXPECT_LT(i, -1e-4); // At least 100 uA
  EXPECT_GT(i, -1.0);  // But not unreasonably large
}

/** @test */
TEST(ZenerDiode, CurrentAtBreakdownKnee) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3};
  const double v = -5.1; // Exactly at breakdown voltage
  const double i = ZenerDiode::current(v, params);

  // Should be transition point (around -Ibv)
  EXPECT_LT(i, 0.0);
  EXPECT_GT(i, -params.Ibv * 10.0); // Within order of magnitude
}

/** @test */
TEST(ZenerDiode, CurrentZeroVoltage) {
  ZenerDiodeParams params;
  const double v = 0.0;
  const double i = ZenerDiode::current(v, params);

  // Should be zero (no voltage, no current)
  EXPECT_NEAR(i, 0.0, params.Is);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(ZenerDiode, ConductanceForwardBias) {
  ZenerDiodeParams params;
  const double v = 0.7;
  const double g = ZenerDiode::conductance(v, params);

  // Should be large (exponential)
  EXPECT_GT(g, 1e-3); // At least 1 mS

  // Check analytical expression
  const double nVt = params.n * params.Vt;
  const double expected = (params.Is / nVt) * std::exp(v / nVt);
  EXPECT_NEAR(g, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(ZenerDiode, ConductanceReverseBias) {
  ZenerDiodeParams params;
  const double v = -2.0;
  const double g = ZenerDiode::conductance(v, params);

  // Should be very small (reverse bias before breakdown)
  EXPECT_GT(g, 0.0);
  EXPECT_LT(g, 1e-9); // Less than 1 nS
}

/** @test */
TEST(ZenerDiode, ConductanceBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};
  const double v = -5.5; // Past breakdown
  const double g = ZenerDiode::conductance(v, params);

  // Should be moderate (breakdown knee)
  EXPECT_GT(g, 1e-6); // At least 1 uS
  EXPECT_LT(g, 1.0);  // But not huge

  // Check analytical expression
  const double vExcess = v - (-params.Vz);
  const double expected = (params.Ibv / params.Vbv) * std::exp(-vExcess / params.Vbv);
  EXPECT_NEAR(g, expected, std::abs(expected) * 1e-10);
}

/** @test */
TEST(ZenerDiode, ConductanceAlwaysPositive) {
  ZenerDiodeParams params;

  // Test across all regions
  for (double v = -10.0; v <= 1.0; v += 0.5) {
    const double g = ZenerDiode::conductance(v, params);
    EXPECT_GT(g, 0.0) << "Conductance must be positive at V=" << v;
  }
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(ZenerDiode, ConductanceNumericalDerivativeForward) {
  ZenerDiodeParams params;
  const double v = 0.7;
  const double dv = 1e-8;

  const double gAnalytical = ZenerDiode::conductance(v, params);

  const double i1 = ZenerDiode::current(v - dv, params);
  const double i2 = ZenerDiode::current(v + dv, params);
  const double gNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
}

/** @test */
TEST(ZenerDiode, ConductanceNumericalDerivativeReverse) {
  ZenerDiodeParams params;
  const double v = -2.0;
  const double dv = 1e-8;

  const double gAnalytical = ZenerDiode::conductance(v, params);

  const double i1 = ZenerDiode::current(v - dv, params);
  const double i2 = ZenerDiode::current(v + dv, params);
  const double gNumerical = (i2 - i1) / (2.0 * dv);

  // Use absolute tolerance for near-zero conductances
  const double tolerance = std::max(std::abs(gNumerical) * 0.01, 1e-20);
  EXPECT_NEAR(gAnalytical, gNumerical, tolerance);
}

/** @test */
TEST(ZenerDiode, ConductanceNumericalDerivativeBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};
  const double v = -5.5;
  const double dv = 1e-8;

  const double gAnalytical = ZenerDiode::conductance(v, params);

  const double i1 = ZenerDiode::current(v - dv, params);
  const double i2 = ZenerDiode::current(v + dv, params);
  const double gNumerical = (i2 - i1) / (2.0 * dv);

  EXPECT_NEAR(gAnalytical, gNumerical, std::abs(gNumerical) * 0.01);
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test */
TEST(ZenerDiode, RegionBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1};
  EXPECT_EQ(ZenerDiode::region(-6.0, params), 0); // Breakdown
  EXPECT_EQ(ZenerDiode::region(-5.5, params), 0); // Breakdown
  EXPECT_EQ(ZenerDiode::region(-5.2, params), 0); // Breakdown
}

/** @test */
TEST(ZenerDiode, RegionReverse) {
  ZenerDiodeParams params{.Vz = 5.1};
  EXPECT_EQ(ZenerDiode::region(-5.0, params), 1); // Reverse
  EXPECT_EQ(ZenerDiode::region(-3.0, params), 1); // Reverse
  EXPECT_EQ(ZenerDiode::region(-0.5, params), 1); // Reverse
}

/** @test */
TEST(ZenerDiode, RegionForward) {
  ZenerDiodeParams params{.Vz = 5.1};
  EXPECT_EQ(ZenerDiode::region(0.0, params), 2); // Forward (V >= 0)
  EXPECT_EQ(ZenerDiode::region(0.3, params), 2); // Forward
  EXPECT_EQ(ZenerDiode::region(0.7, params), 2); // Forward
  EXPECT_EQ(ZenerDiode::region(1.0, params), 2); // Forward
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(ZenerDiode, StampForwardBias) {
  MnaSystem mna(3);
  ZenerDiodeParams params;
  const NetID anode = 1;
  const NetID cathode = 2;
  const double v = 0.7;

  // Should stamp linearized conductance and current source
  ZenerDiode::stamp(mna, anode, cathode, v, params);
}

/** @test */
TEST(ZenerDiode, StampBreakdown) {
  MnaSystem mna(3);
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3};
  const NetID anode = 1;
  const NetID cathode = 2;
  const double v = -5.5;

  // Should stamp breakdown region conductance
  ZenerDiode::stamp(mna, anode, cathode, v, params);
}

/** @test */
TEST(ZenerDiode, StampGroundedCathode) {
  MnaSystem mna(3);
  ZenerDiodeParams params;
  const NetID anode = 1;
  const NetID cathode = 0; // Ground
  const double v = 0.7;

  // Should handle grounded cathode correctly
  ZenerDiode::stamp(mna, anode, cathode, v, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(ZenerDiode, BreakdownVoltageRegulation) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};

  // Test that breakdown voltage is around -Vz
  const double v1 = -5.0; // Just before breakdown
  const double v2 = -5.2; // Just after breakdown

  const double i1 = ZenerDiode::current(v1, params);
  const double i2 = ZenerDiode::current(v2, params);

  // Current should increase dramatically across breakdown
  EXPECT_GT(std::abs(i2), std::abs(i1) * 10.0);
}

/** @test */
TEST(ZenerDiode, ForwardSameAsDiode) {
  ZenerDiodeParams zenerParams;
  const double v = 0.6;

  const double iZener = ZenerDiode::current(v, zenerParams);
  const double iDiode = zenerParams.Is * (std::exp(v / (zenerParams.n * zenerParams.Vt)) - 1.0);

  // Forward bias should match standard diode equation
  EXPECT_NEAR(iZener, iDiode, std::abs(iDiode) * 1e-10);
}

/** @test */
TEST(ZenerDiode, BreakdownSharpness) {
  ZenerDiodeParams sharpParams{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.05};  // Sharp
  ZenerDiodeParams gradualParams{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.2}; // Gradual

  const double v = -5.5; // Past breakdown

  const double iSharp = ZenerDiode::current(v, sharpParams);
  const double iGradual = ZenerDiode::current(v, gradualParams);

  // Sharp breakdown should have larger current at same voltage
  EXPECT_GT(std::abs(iSharp), std::abs(iGradual));
}

/** @test */
TEST(ZenerDiode, ZenerVoltageParameter) {
  ZenerDiodeParams params3V{.Vz = 3.3, .Ibv = 1e-3, .Vbv = 0.1};
  ZenerDiodeParams params5V{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};

  // 3.3V Zener should break down at -3.3V
  const double i3V_before = ZenerDiode::current(-3.2, params3V);
  const double i3V_after = ZenerDiode::current(-3.4, params3V);
  EXPECT_GT(std::abs(i3V_after), std::abs(i3V_before) * 10.0);

  // 5.1V Zener should NOT break down at -3.4V
  const double i5V = ZenerDiode::current(-3.4, params5V);
  EXPECT_LT(std::abs(i5V), 1e-10); // Still in reverse region
}
