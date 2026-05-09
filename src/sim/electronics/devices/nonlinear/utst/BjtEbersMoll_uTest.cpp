/**
 * @file BjtEbersMoll_uTest.cpp
 * @brief Unit tests for BjtEbersMoll model.
 */

#include "src/sim/electronics/devices/nonlinear/inc/BjtEbersMoll.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::nonlinear::BjtEbersMoll;
using sim::electronics::devices::nonlinear::BjtEbersMollParams;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Constants ----------------------------- */

static constexpr double EPSILON = 1e-12; // Tolerance for floating-point comparison

/* ----------------------------- Parameters ----------------------------- */

/** @test Default parameters are reasonable. */
TEST(BjtEbersMollTest, DefaultParameters) {
  BjtEbersMollParams params;

  EXPECT_DOUBLE_EQ(params.Is, 1e-14);
  EXPECT_DOUBLE_EQ(params.Bf, 100.0);
  EXPECT_DOUBLE_EQ(params.Br, 1.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.026);
}

/** @test Custom parameters. */
TEST(BjtEbersMollTest, CustomParameters) {
  BjtEbersMollParams params{.Is = 1e-15, .Bf = 200.0, .Br = 2.0, .Vt = 0.025};

  EXPECT_DOUBLE_EQ(params.Is, 1e-15);
  EXPECT_DOUBLE_EQ(params.Bf, 200.0);
  EXPECT_DOUBLE_EQ(params.Br, 2.0);
  EXPECT_DOUBLE_EQ(params.Vt, 0.025);
}

/* ----------------------------- Cutoff Region ----------------------------- */

/** @test Cutoff region (both junctions reverse-biased). */
TEST(BjtEbersMollTest, CutoffRegion) {
  BjtEbersMollParams params;

  // Vbe = -1V, Vbc = -5V (both reverse-biased)
  double ic = BjtEbersMoll::collectorCurrent(-1.0, -5.0, params);
  double ib = BjtEbersMoll::baseCurrent(-1.0, -5.0, params);

  // Currents should be very small (near zero in deep cutoff)
  // Ic ~= -Is + Is/Br ~= 0 when Br=1
  // Ib ~= -Is/Bf - Is/Br
  EXPECT_LT(std::abs(ic), params.Is * 2); // Within 2*Is of zero
  EXPECT_LT(std::abs(ib), params.Is * 2); // Within 2*Is of zero
}

/** @test Cutoff region current magnitude. */
TEST(BjtEbersMollTest, CutoffCurrentMagnitude) {
  BjtEbersMollParams params;

  double ic = BjtEbersMoll::collectorCurrent(0.0, 0.0, params);
  double ib = BjtEbersMoll::baseCurrent(0.0, 0.0, params);

  // At zero bias, currents are zero (exp(0) - 1 = 0)
  EXPECT_NEAR(ic, 0.0, EPSILON);
  EXPECT_NEAR(ib, 0.0, EPSILON);
}

/* ----------------------------- Forward Active Region ----------------------------- */

/** @test Forward active region (BE forward, BC reverse). */
TEST(BjtEbersMollTest, ForwardActiveRegion) {
  BjtEbersMollParams params;

  // Vbe = 0.7V (forward), Vbc = -5.0V (reverse)
  double ic = BjtEbersMoll::collectorCurrent(0.7, -5.0, params);
  double ib = BjtEbersMoll::baseCurrent(0.7, -5.0, params);

  // Collector current should be positive and significant
  EXPECT_GT(ic, 1e-3); // More than 1 mA

  // Base current should be positive but much smaller (Ib = Ic/Bf)
  EXPECT_GT(ib, 0.0);
  EXPECT_LT(ib, ic); // Ib < Ic
}

/** @test Forward active current gain (beta = Ic/Ib). */
TEST(BjtEbersMollTest, ForwardActiveCurrentGain) {
  BjtEbersMollParams params{.Is = 1e-14, .Bf = 100.0};

  // Forward active: Vbe = 0.7V, Vbc = -5.0V
  double ic = BjtEbersMoll::collectorCurrent(0.7, -5.0, params);
  double ib = BjtEbersMoll::baseCurrent(0.7, -5.0, params);

  double beta = ic / ib;

  // Current gain should be approximately Bf
  EXPECT_NEAR(beta, params.Bf, params.Bf * 0.1); // Within 10%
}

/* ----------------------------- Saturation Region ----------------------------- */

/** @test Saturation region (both junctions forward-biased). */
TEST(BjtEbersMollTest, SaturationRegion) {
  BjtEbersMollParams params;

  // Vbe = 0.7V (forward), Vbc = 0.5V (forward, but less than Vbe)
  double ic = BjtEbersMoll::collectorCurrent(0.7, 0.5, params);
  double ib = BjtEbersMoll::baseCurrent(0.7, 0.5, params);

  // Collector current reduced compared to active region
  EXPECT_GT(ic, 0.0);

  // Base current increased (both junctions contribute)
  EXPECT_GT(ib, 0.0);
}

/** @test Saturation reduces collector current. */
TEST(BjtEbersMollTest, SaturationReducesIc) {
  BjtEbersMollParams params;

  // Forward active: Vbe = 0.7V, Vbc = -5.0V
  double icActive = BjtEbersMoll::collectorCurrent(0.7, -5.0, params);

  // Saturation: Vbe = 0.7V, Vbc = 0.5V
  double icSat = BjtEbersMoll::collectorCurrent(0.7, 0.5, params);

  // Saturation current should be less than active region current
  EXPECT_LT(icSat, icActive);
}

/* ----------------------------- Reverse Active Region ----------------------------- */

/** @test Reverse active region (BE reverse, BC forward). */
TEST(BjtEbersMollTest, ReverseActiveRegion) {
  BjtEbersMollParams params;

  // Vbe = -5.0V (reverse), Vbc = 0.7V (forward)
  double ic = BjtEbersMoll::collectorCurrent(-5.0, 0.7, params);
  double ib = BjtEbersMoll::baseCurrent(-5.0, 0.7, params);

  // Collector current should be negative (reverse direction)
  EXPECT_LT(ic, 0.0);

  // Base current should be positive
  EXPECT_GT(ib, 0.0);
}

/* ----------------------------- Kirchhoff Current Law ----------------------------- */

/** @test Kirchhoff: Ie = -(Ic + Ib). */
TEST(BjtEbersMollTest, KirchhoffCurrentLaw) {
  BjtEbersMollParams params;

  // Test in forward active region
  double vbe = 0.7;
  double vbc = -5.0;

  double ic = BjtEbersMoll::collectorCurrent(vbe, vbc, params);
  double ib = BjtEbersMoll::baseCurrent(vbe, vbc, params);
  double ie = BjtEbersMoll::emitterCurrent(vbe, vbc, params);

  // Kirchhoff: Ie + Ic + Ib = 0
  EXPECT_NEAR(ie + ic + ib, 0.0, 1e-9);
}

/** @test Kirchhoff in all regions. */
TEST(BjtEbersMollTest, KirchhoffAllRegions) {
  BjtEbersMollParams params;

  // Test multiple operating points
  struct TestPoint {
    double vbe;
    double vbc;
  };

  TestPoint points[] = {
      {-1.0, -5.0}, // Cutoff
      {0.7, -5.0},  // Forward active
      {0.7, 0.5},   // Saturation
      {-5.0, 0.7},  // Reverse active
  };

  for (const auto& p : points) {
    double ic = BjtEbersMoll::collectorCurrent(p.vbe, p.vbc, params);
    double ib = BjtEbersMoll::baseCurrent(p.vbe, p.vbc, params);
    double ie = BjtEbersMoll::emitterCurrent(p.vbe, p.vbc, params);

    EXPECT_NEAR(ie + ic + ib, 0.0, 1e-9);
  }
}

/* ----------------------------- Transconductances ----------------------------- */

/** @test Transconductance gm = dIc/dVbe. */
TEST(BjtEbersMollTest, TransconductanceForwardActive) {
  BjtEbersMollParams params;

  // Forward active: Vbe = 0.7V, Vbc = -5.0V
  double gm = BjtEbersMoll::transconductance(0.7, -5.0, params);

  // gm = (Is/Vt) * exp(Vbe/Vt)
  double expectedGm = (params.Is / params.Vt) * std::exp(0.7 / params.Vt);

  EXPECT_NEAR(gm, expectedGm, expectedGm * 1e-6);
}

/** @test Output conductance go = dIc/dVbc. */
TEST(BjtEbersMollTest, OutputConductance) {
  BjtEbersMollParams params;

  double go = BjtEbersMoll::outputConductance(0.7, -5.0, params);

  // go = -(Is/(Br*Vt)) * exp(Vbc/Vt)
  double expectedGo = -(params.Is / (params.Br * params.Vt)) * std::exp(-5.0 / params.Vt);

  EXPECT_NEAR(go, expectedGo, std::abs(expectedGo) * 1e-6);
}

/** @test Base-emitter conductance gbe = dIb/dVbe. */
TEST(BjtEbersMollTest, BaseConductanceBE) {
  BjtEbersMollParams params;

  double gbe = BjtEbersMoll::baseConductanceBE(0.7, -5.0, params);

  // gbe = (Is/(Bf*Vt)) * exp(Vbe/Vt)
  double expectedGbe = (params.Is / (params.Bf * params.Vt)) * std::exp(0.7 / params.Vt);

  EXPECT_NEAR(gbe, expectedGbe, expectedGbe * 1e-6);
}

/** @test Base-collector conductance gbc = dIb/dVbc. */
TEST(BjtEbersMollTest, BaseConductanceBC) {
  BjtEbersMollParams params;

  double gbc = BjtEbersMoll::baseConductanceBC(0.7, -5.0, params);

  // gbc = (Is/(Br*Vt)) * exp(Vbc/Vt)
  double expectedGbc = (params.Is / (params.Br * params.Vt)) * std::exp(-5.0 / params.Vt);

  EXPECT_NEAR(gbc, expectedGbc, std::abs(expectedGbc) * 1e-6);
}

/* ----------------------------- Numerical Derivative Validation ----------------------------- */

/** @test Transconductance matches numerical derivative dIc/dVbe. */
TEST(BjtEbersMollTest, TransconductanceNumericalDerivative) {
  BjtEbersMollParams params;
  const double VBE = 0.7;
  const double VBC = -5.0;
  const double DVBE = 1e-8;

  // Analytical transconductance
  double gmAnalytical = BjtEbersMoll::transconductance(VBE, VBC, params);

  // Numerical derivative: dIc/dVbe ~= (Ic(vbe+dvbe) - Ic(vbe-dvbe)) / (2*dvbe)
  double ic1 = BjtEbersMoll::collectorCurrent(VBE - DVBE, VBC, params);
  double ic2 = BjtEbersMoll::collectorCurrent(VBE + DVBE, VBC, params);
  double gmNumerical = (ic2 - ic1) / (2.0 * DVBE);

  // Should match within 1%
  EXPECT_NEAR(gmAnalytical, gmNumerical, std::abs(gmNumerical) * 0.01);
}

/** @test Output conductance matches numerical derivative dIc/dVbc. */
TEST(BjtEbersMollTest, OutputConductanceNumericalDerivative) {
  BjtEbersMollParams params;
  const double VBE = 0.7;
  const double VBC = -5.0;
  const double DVBC = 1e-8;

  // Analytical output conductance
  double goAnalytical = BjtEbersMoll::outputConductance(VBE, VBC, params);

  // Numerical derivative: dIc/dVbc ~= (Ic(vbc+dvbc) - Ic(vbc-dvbc)) / (2*dvbc)
  double ic1 = BjtEbersMoll::collectorCurrent(VBE, VBC - DVBC, params);
  double ic2 = BjtEbersMoll::collectorCurrent(VBE, VBC + DVBC, params);
  double goNumerical = (ic2 - ic1) / (2.0 * DVBC);

  // Use absolute tolerance for near-zero values, relative for larger values
  double tolerance = std::max(std::abs(goNumerical) * 0.01, 1e-20);
  EXPECT_NEAR(goAnalytical, goNumerical, tolerance);
}

/** @test Base-emitter conductance matches numerical derivative dIb/dVbe. */
TEST(BjtEbersMollTest, BaseConductanceBENumericalDerivative) {
  BjtEbersMollParams params;
  const double VBE = 0.7;
  const double VBC = -5.0;
  const double DVBE = 1e-8;

  // Analytical base-emitter conductance
  double gbeAnalytical = BjtEbersMoll::baseConductanceBE(VBE, VBC, params);

  // Numerical derivative: dIb/dVbe ~= (Ib(vbe+dvbe) - Ib(vbe-dvbe)) / (2*dvbe)
  double ib1 = BjtEbersMoll::baseCurrent(VBE - DVBE, VBC, params);
  double ib2 = BjtEbersMoll::baseCurrent(VBE + DVBE, VBC, params);
  double gbeNumerical = (ib2 - ib1) / (2.0 * DVBE);

  // Should match within 1%
  EXPECT_NEAR(gbeAnalytical, gbeNumerical, std::abs(gbeNumerical) * 0.01);
}

/** @test Base-collector conductance matches numerical derivative dIb/dVbc. */
TEST(BjtEbersMollTest, BaseConductanceBCNumericalDerivative) {
  BjtEbersMollParams params;
  const double VBE = 0.7;
  const double VBC = -5.0;
  const double DVBC = 1e-8;

  // Analytical base-collector conductance
  double gbcAnalytical = BjtEbersMoll::baseConductanceBC(VBE, VBC, params);

  // Numerical derivative: dIb/dVbc ~= (Ib(vbc+dvbc) - Ib(vbc-dvbc)) / (2*dvbc)
  double ib1 = BjtEbersMoll::baseCurrent(VBE, VBC - DVBC, params);
  double ib2 = BjtEbersMoll::baseCurrent(VBE, VBC + DVBC, params);
  double gbcNumerical = (ib2 - ib1) / (2.0 * DVBC);

  // Use absolute tolerance for near-zero values, relative for larger values
  double tolerance = std::max(std::abs(gbcNumerical) * 0.01, 1e-20);
  EXPECT_NEAR(gbcAnalytical, gbcNumerical, tolerance);
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test Region detection: cutoff. */
TEST(BjtEbersMollTest, RegionCutoff) {
  BjtEbersMollParams params;

  int r1 = BjtEbersMoll::region(-1.0, -5.0, params);
  int r2 = BjtEbersMoll::region(0.0, 0.0, params);
  int r3 = BjtEbersMoll::region(0.3, -1.0, params);

  EXPECT_EQ(r1, 0); // Both reverse
  EXPECT_EQ(r2, 0); // Both at zero
  EXPECT_EQ(r3, 0); // Below turn-on
}

/** @test Region detection: forward active. */
TEST(BjtEbersMollTest, RegionForwardActive) {
  BjtEbersMollParams params;

  int r = BjtEbersMoll::region(0.7, -5.0, params);

  EXPECT_EQ(r, 1); // BE forward, BC reverse
}

/** @test Region detection: reverse active. */
TEST(BjtEbersMollTest, RegionReverseActive) {
  BjtEbersMollParams params;

  int r = BjtEbersMoll::region(-5.0, 0.7, params);

  EXPECT_EQ(r, 2); // BE reverse, BC forward
}

/** @test Region detection: saturation. */
TEST(BjtEbersMollTest, RegionSaturation) {
  BjtEbersMollParams params;

  int r = BjtEbersMoll::region(0.7, 0.6, params);

  EXPECT_EQ(r, 3); // Both forward
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp cutoff state. */
TEST(BjtEbersMollTest, StampCutoff) {
  MnaSystem mna(4);
  BjtEbersMollParams params;
  const NetID COLLECTOR = 1;
  const NetID BASE = 2;
  const NetID EMITTER = 0;

  BjtEbersMoll::stamp(mna, COLLECTOR, BASE, EMITTER, -1.0, -5.0, params);

  // Should stamp small conductances (cutoff)
}

/** @test Stamp forward active state. */
TEST(BjtEbersMollTest, StampForwardActive) {
  MnaSystem mna(4);
  BjtEbersMollParams params;
  const NetID COLLECTOR = 1;
  const NetID BASE = 2;
  const NetID EMITTER = 0;

  BjtEbersMoll::stamp(mna, COLLECTOR, BASE, EMITTER, 0.7, -5.0, params);

  // Should stamp significant conductances
}

/** @test Stamp saturation state. */
TEST(BjtEbersMollTest, StampSaturation) {
  MnaSystem mna(4);
  BjtEbersMollParams params;
  const NetID COLLECTOR = 1;
  const NetID BASE = 2;
  const NetID EMITTER = 0;

  BjtEbersMoll::stamp(mna, COLLECTOR, BASE, EMITTER, 0.7, 0.5, params);

  // Should stamp conductances for saturation
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test Exponential Vbe dependence (Ic vs Vbe). */
TEST(BjtEbersMollTest, ExponentialVbeDependence) {
  BjtEbersMollParams params;
  const double VBC = -5.0; // Fixed (reverse-biased)

  double ic1 = BjtEbersMoll::collectorCurrent(0.6, VBC, params);
  double ic2 = BjtEbersMoll::collectorCurrent(0.66, VBC, params); // +60mV

  double ratio = ic2 / ic1;

  // Should be approximately 10x (one decade per 60mV for ideal BJT)
  EXPECT_NEAR(ratio, 10.0, 1.0); // Within 10% tolerance
}

/** @test Current gain beta = Ic/Ib varies with Bf. */
TEST(BjtEbersMollTest, CurrentGainVariation) {
  BjtEbersMollParams lowGain{.Is = 1e-14, .Bf = 50.0};
  BjtEbersMollParams highGain{.Is = 1e-14, .Bf = 200.0};

  // Forward active
  double icLow = BjtEbersMoll::collectorCurrent(0.7, -5.0, lowGain);
  double ibLow = BjtEbersMoll::baseCurrent(0.7, -5.0, lowGain);
  double betaLow = icLow / ibLow;

  double icHigh = BjtEbersMoll::collectorCurrent(0.7, -5.0, highGain);
  double ibHigh = BjtEbersMoll::baseCurrent(0.7, -5.0, highGain);
  double betaHigh = icHigh / ibHigh;

  EXPECT_NEAR(betaLow, 50.0, 5.0);
  EXPECT_NEAR(betaHigh, 200.0, 20.0);
}

/** @test Temperature effect (via Vt). */
TEST(BjtEbersMollTest, TemperatureEffect) {
  // Room temperature (300K): Vt ~= 26mV
  BjtEbersMollParams roomTemp{.Is = 1e-14, .Bf = 100.0, .Br = 1.0, .Vt = 0.026};

  // Hot temperature (350K): Vt ~= 30mV
  BjtEbersMollParams hotTemp{.Is = 1e-14, .Bf = 100.0, .Br = 1.0, .Vt = 0.030};

  double icRoom = BjtEbersMoll::collectorCurrent(0.7, -5.0, roomTemp);
  double icHot = BjtEbersMoll::collectorCurrent(0.7, -5.0, hotTemp);

  // Higher temperature -> lower current at same voltage
  EXPECT_GT(icRoom, icHot);
}

/** @test Saturation voltage (Vce_sat). */
TEST(BjtEbersMollTest, SaturationVoltage) {
  BjtEbersMollParams params;

  // In saturation: Vce = Vbe - Vbc
  // Vbe = 0.7V, Vbc = 0.5V -> Vce = 0.2V (typical saturation voltage)
  double ic = BjtEbersMoll::collectorCurrent(0.7, 0.5, params);

  // Collector current should be positive but reduced
  EXPECT_GT(ic, 0.0);
}

/** @test Early effect (output resistance variation). */
TEST(BjtEbersMollTest, EarlyEffect) {
  BjtEbersMollParams params;

  // Forward active with different Vbc
  double ic1 = BjtEbersMoll::collectorCurrent(0.7, -5.0, params);
  double ic2 = BjtEbersMoll::collectorCurrent(0.7, -10.0, params);

  // Basic Ebers-Moll doesn't model Early effect perfectly,
  // but there should be slight variation
  EXPECT_NEAR(ic1, ic2, ic1 * 0.1); // Within 10%
}

/** @test High injection (Vbe >> Vt). */
TEST(BjtEbersMollTest, HighInjection) {
  BjtEbersMollParams params;

  double ic = BjtEbersMoll::collectorCurrent(0.9, -5.0, params);

  // Very high current (exponential)
  EXPECT_GT(ic, 1e-1); // More than 100 mA
}

/** @test Low injection (Vbe near threshold). */
TEST(BjtEbersMollTest, LowInjection) {
  BjtEbersMollParams params;

  double ic = BjtEbersMoll::collectorCurrent(0.55, -5.0, params);

  // Low current (but still exponential, so 10s of uA near turn-on)
  EXPECT_LT(ic, 1e-4); // Less than 100 uA
  EXPECT_GT(ic, 1e-7); // More than 100 nA
}
