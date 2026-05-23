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
TEST(ZenerDiodeTest, DefaultParameters) {
  ZenerDiodeParams params;
  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.n, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
  EXPECT_DOUBLE_EQ(params.Vz, 5.1);
  EXPECT_DOUBLE_EQ(params.Ibv, 1e-3);
  EXPECT_DOUBLE_EQ(params.Vbv, 0.1);
}

/** @test */
TEST(ZenerDiodeTest, CustomParameters) {
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
TEST(ZenerDiodeTest, CurrentForwardBias) {
  ZenerDiodeParams params;
  const double V = 0.7; // Forward bias (silicon turn-on)
  const double I = ZenerDiode::current(V, params);

  // Should be exponentially large (mA range)
  EXPECT_GT(I, 1e-3);
  EXPECT_LT(I, 1.0);

  // Check exponential relationship
  const double EXPECTED = params.Is * (std::exp(V / (params.n * params.Vt)) - 1.0);
  EXPECT_NEAR(I, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(ZenerDiodeTest, CurrentReverseBias) {
  ZenerDiodeParams params;
  const double V = -2.0; // Reverse bias (before breakdown)
  const double I = ZenerDiode::current(V, params);

  // Should be small negative (leakage current ~Is)
  EXPECT_LT(I, 0.0);
  EXPECT_GT(I, -params.Is * 1.1); // Close to -Is
}

/** @test */
TEST(ZenerDiodeTest, CurrentBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3};
  const double V = -5.5; // Past breakdown voltage
  const double I = ZenerDiode::current(V, params);

  // Should be large negative (regulation current)
  EXPECT_LT(I, -1e-4); // At least 100 uA
  EXPECT_GT(I, -1.0);  // But not unreasonably large
}

/** @test */
TEST(ZenerDiodeTest, CurrentAtBreakdownKnee) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3};
  const double V = -5.1; // Exactly at breakdown voltage
  const double I = ZenerDiode::current(V, params);

  // Should be transition point (around -Ibv)
  EXPECT_LT(I, 0.0);
  EXPECT_GT(I, -params.Ibv * 10.0); // Within order of magnitude
}

/** @test */
TEST(ZenerDiodeTest, CurrentZeroVoltage) {
  ZenerDiodeParams params;
  const double V = 0.0;
  const double I = ZenerDiode::current(V, params);

  // Should be zero (no voltage, no current)
  EXPECT_NEAR(I, 0.0, params.Is);
}

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test */
TEST(ZenerDiodeTest, ConductanceForwardBias) {
  ZenerDiodeParams params;
  const double V = 0.7;
  const double G = ZenerDiode::conductance(V, params);

  // Should be large (exponential)
  EXPECT_GT(G, 1e-3); // At least 1 mS

  // Check analytical expression
  const double N_VT = params.n * params.Vt;
  const double EXPECTED = (params.Is / N_VT) * std::exp(V / N_VT);
  EXPECT_NEAR(G, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(ZenerDiodeTest, ConductanceReverseBias) {
  ZenerDiodeParams params;
  const double V = -2.0;
  const double G = ZenerDiode::conductance(V, params);

  // Should be very small (reverse bias before breakdown)
  EXPECT_GT(G, 0.0);
  EXPECT_LT(G, 1e-9); // Less than 1 nS
}

/** @test */
TEST(ZenerDiodeTest, ConductanceBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};
  const double V = -5.5; // Past breakdown
  const double G = ZenerDiode::conductance(V, params);

  // Should be moderate (breakdown knee)
  EXPECT_GT(G, 1e-6); // At least 1 uS
  EXPECT_LT(G, 1.0);  // But not huge

  // Check analytical expression
  const double V_EXCESS = V - (-params.Vz);
  const double EXPECTED = (params.Ibv / params.Vbv) * std::exp(-V_EXCESS / params.Vbv);
  EXPECT_NEAR(G, EXPECTED, std::abs(EXPECTED) * 1e-10);
}

/** @test */
TEST(ZenerDiodeTest, ConductanceAlwaysPositive) {
  ZenerDiodeParams params;

  // Test across all regions
  for (double v = -10.0; v <= 1.0; v += 0.5) {
    const double G = ZenerDiode::conductance(v, params);
    EXPECT_GT(G, 0.0) << "Conductance must be positive at V=" << v;
  }
}

/* ----------------------------- Numerical Jacobian ----------------------------- */

/** @test */
TEST(ZenerDiodeTest, ConductanceNumericalDerivativeForward) {
  ZenerDiodeParams params;
  const double V = 0.7;
  const double DV = 1e-8;

  const double G_ANALYTICAL = ZenerDiode::conductance(V, params);

  const double I1 = ZenerDiode::current(V - DV, params);
  const double I2 = ZenerDiode::current(V + DV, params);
  const double G_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(G_ANALYTICAL, G_NUMERICAL, std::abs(G_NUMERICAL) * 0.01);
}

/** @test */
TEST(ZenerDiodeTest, ConductanceNumericalDerivativeReverse) {
  ZenerDiodeParams params;
  const double V = -2.0;
  const double DV = 1e-8;

  const double G_ANALYTICAL = ZenerDiode::conductance(V, params);

  const double I1 = ZenerDiode::current(V - DV, params);
  const double I2 = ZenerDiode::current(V + DV, params);
  const double G_NUMERICAL = (I2 - I1) / (2.0 * DV);

  // Use absolute tolerance for near-zero conductances
  const double TOLERANCE = std::max(std::abs(G_NUMERICAL) * 0.01, 1e-20);
  EXPECT_NEAR(G_ANALYTICAL, G_NUMERICAL, TOLERANCE);
}

/** @test */
TEST(ZenerDiodeTest, ConductanceNumericalDerivativeBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};
  const double V = -5.5;
  const double DV = 1e-8;

  const double G_ANALYTICAL = ZenerDiode::conductance(V, params);

  const double I1 = ZenerDiode::current(V - DV, params);
  const double I2 = ZenerDiode::current(V + DV, params);
  const double G_NUMERICAL = (I2 - I1) / (2.0 * DV);

  EXPECT_NEAR(G_ANALYTICAL, G_NUMERICAL, std::abs(G_NUMERICAL) * 0.01);
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test */
TEST(ZenerDiodeTest, RegionBreakdown) {
  ZenerDiodeParams params{.Vz = 5.1};
  EXPECT_EQ(ZenerDiode::region(-6.0, params), 0); // Breakdown
  EXPECT_EQ(ZenerDiode::region(-5.5, params), 0); // Breakdown
  EXPECT_EQ(ZenerDiode::region(-5.2, params), 0); // Breakdown
}

/** @test */
TEST(ZenerDiodeTest, RegionReverse) {
  ZenerDiodeParams params{.Vz = 5.1};
  EXPECT_EQ(ZenerDiode::region(-5.0, params), 1); // Reverse
  EXPECT_EQ(ZenerDiode::region(-3.0, params), 1); // Reverse
  EXPECT_EQ(ZenerDiode::region(-0.5, params), 1); // Reverse
}

/** @test */
TEST(ZenerDiodeTest, RegionForward) {
  ZenerDiodeParams params{.Vz = 5.1};
  EXPECT_EQ(ZenerDiode::region(0.0, params), 2); // Forward (V >= 0)
  EXPECT_EQ(ZenerDiode::region(0.3, params), 2); // Forward
  EXPECT_EQ(ZenerDiode::region(0.7, params), 2); // Forward
  EXPECT_EQ(ZenerDiode::region(1.0, params), 2); // Forward
}

/* ----------------------------- Stamping ----------------------------- */

/** @test */
TEST(ZenerDiodeTest, StampForwardBias) {
  MnaSystem mna(3);
  ZenerDiodeParams params;
  const NetID ANODE = 1;
  const NetID CATHODE = 2;
  const double V = 0.7;

  // Should stamp linearized conductance and current source
  ZenerDiode::stamp(mna, ANODE, CATHODE, V, params);
}

/** @test */
TEST(ZenerDiodeTest, StampBreakdown) {
  MnaSystem mna(3);
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3};
  const NetID ANODE = 1;
  const NetID CATHODE = 2;
  const double V = -5.5;

  // Should stamp breakdown region conductance
  ZenerDiode::stamp(mna, ANODE, CATHODE, V, params);
}

/** @test */
TEST(ZenerDiodeTest, StampGroundedCathode) {
  MnaSystem mna(3);
  ZenerDiodeParams params;
  const NetID ANODE = 1;
  const NetID CATHODE = 0; // Ground
  const double V = 0.7;

  // Should handle grounded cathode correctly
  ZenerDiode::stamp(mna, ANODE, CATHODE, V, params);
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test */
TEST(ZenerDiodeTest, BreakdownVoltageRegulation) {
  ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};

  // Test that breakdown voltage is around -Vz
  const double V1 = -5.0; // Just before breakdown
  const double V2 = -5.2; // Just after breakdown

  const double I1 = ZenerDiode::current(V1, params);
  const double I2 = ZenerDiode::current(V2, params);

  // Current should increase dramatically across breakdown
  EXPECT_GT(std::abs(I2), std::abs(I1) * 10.0);
}

/** @test */
TEST(ZenerDiodeTest, ForwardSameAsDiode) {
  ZenerDiodeParams zenerParams;
  const double V = 0.6;

  const double I_ZENER = ZenerDiode::current(V, zenerParams);
  const double I_DIODE = zenerParams.Is * (std::exp(V / (zenerParams.n * zenerParams.Vt)) - 1.0);

  // Forward bias should match standard diode equation
  EXPECT_NEAR(I_ZENER, I_DIODE, std::abs(I_DIODE) * 1e-10);
}

/** @test */
TEST(ZenerDiodeTest, BreakdownSharpness) {
  ZenerDiodeParams sharpParams{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.05};  // Sharp
  ZenerDiodeParams gradualParams{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.2}; // Gradual

  const double V = -5.5; // Past breakdown

  const double I_SHARP = ZenerDiode::current(V, sharpParams);
  const double I_GRADUAL = ZenerDiode::current(V, gradualParams);

  // Sharp breakdown should have larger current at same voltage
  EXPECT_GT(std::abs(I_SHARP), std::abs(I_GRADUAL));
}

/** @test */
TEST(ZenerDiodeTest, ZenerVoltageParameter) {
  ZenerDiodeParams params3V{.Vz = 3.3, .Ibv = 1e-3, .Vbv = 0.1};
  ZenerDiodeParams params5V{.Vz = 5.1, .Ibv = 1e-3, .Vbv = 0.1};

  // 3.3V Zener should break down at -3.3V
  const double I3_V_BEFORE = ZenerDiode::current(-3.2, params3V);
  const double I3_V_AFTER = ZenerDiode::current(-3.4, params3V);
  EXPECT_GT(std::abs(I3_V_AFTER), std::abs(I3_V_BEFORE) * 10.0);

  // 5.1V Zener should NOT break down at -3.4V
  const double I5_V = ZenerDiode::current(-3.4, params5V);
  EXPECT_LT(std::abs(I5_V), 1e-10); // Still in reverse region
}
