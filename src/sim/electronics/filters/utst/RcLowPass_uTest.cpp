/**
 * @file RcLowPass_uTest.cpp
 * @brief Unit tests for first-order RC low-pass filter circuit model.
 *
 * Validates against analytical solutions:
 *   - Step response: V_out(t) = V_in * (1 - exp(-t/tau))
 *   - DC operating point: V_out(steady) = V_in
 *   - Time constant: tau = R*C
 *   - Cutoff frequency: f_c = 1 / (2pi*R*C)
 */

#include "src/sim/electronics/filters/inc/RcLowPass.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::filters::RcLowPass;
using sim::electronics::transient::IntegrationMethod;
using sim::electronics::transient::TransientConfig;
using sim::electronics::transient::TransientState;
using sim::electronics::transient::TransientStatus;

/* ----------------------------- Construction Tests ----------------------------- */

/** @test Construction stores R and C, computes correct cutoff frequency. */
TEST(RcLowPass, ConstructionStoresValues) {
  // 1k ohm, 159 nF -> ~1 kHz cutoff
  RcLowPass filter(1e3, 159e-9);

  EXPECT_NEAR(filter.cutoffHz(), 1000.0, 5.0) << "Cutoff should be ~1 kHz for R=1k, C=159nF";
  EXPECT_NEAR(filter.tau(), 159e-6, 1e-9) << "Time constant should be R*C = 159 us";
}

/** @test Net accessors return valid IDs. */
TEST(RcLowPass, NetAccessors) {
  RcLowPass filter(1e3, 1e-6);
  // inNet and outNet should be different and non-zero (ground is 0)
  EXPECT_NE(filter.inNet(), 0u);
  EXPECT_NE(filter.outNet(), 0u);
  EXPECT_NE(filter.inNet(), filter.outNet());
}

/** @test Different R and C produce expected cutoff. */
TEST(RcLowPass, ConstructionDifferentValues) {
  RcLowPass filter(10e3, 1e-6); // 10k ohm, 1 uF
  // tau = 10ms, fc = 1/(2*pi*tau) ~ 15.92 Hz
  EXPECT_NEAR(filter.cutoffHz(), 15.915, 0.01);
  EXPECT_NEAR(filter.tau(), 10e-3, 1e-6);
}

/* ----------------------------- DC Operating Point ----------------------------- */

/** @test DC operating point: V_out should equal V_in (no current flows steady-state). */
TEST(RcLowPass, DcOperatingPoint) {
  RcLowPass filter(1e3, 1e-6);
  filter.build();
  filter.setInputVoltage(5.0);

  TransientState state;
  state.resize(filter.circuit().netCount(), 0);

  TransientStatus status = filter.circuit().computeDC(state);
  EXPECT_EQ(status, TransientStatus::SUCCESS);

  EXPECT_NEAR(state.nodeVoltages[filter.outNet()], 5.0, 0.001)
      << "DC: V_out should equal V_in (no current through R steady-state)";
}

/* ----------------------------- Transient Step Response ----------------------------- */

/** @test Step response matches analytical solution at multiple time points. */
TEST(RcLowPass, StepResponseMatchesAnalytical) {
  // Choose values for easy timing: R=1k, C=1uF -> tau=1ms
  RcLowPass filter(1e3, 1e-6);
  filter.build();
  filter.setInputVoltage(0.0); // Start at 0V

  TransientState state;
  state.resize(filter.circuit().netCount(), 0);

  // DC initial condition at 0V
  filter.circuit().computeDC(state);

  // Apply step input at t=0
  filter.setInputVoltage(5.0);

  // Step at multiple time points and compare with analytical
  // Step size: tau/100 = 10us. Use Backward Euler.
  const double DT = 10e-6;
  filter.circuit().solver().setIntegrationMethod(IntegrationMethod::BACKWARD_EULER);

  // After 0.5*tau, V_out should be ~39.3% of step
  for (int i = 0; i < 50; ++i) {
    filter.circuit().solver().step(DT, state);
  }
  double t1 = 50 * DT; // 0.5 ms = 0.5*tau
  double v1 = state.nodeVoltages[filter.outNet()];
  double expected1 = filter.analyticalStepResponse(5.0, t1);
  EXPECT_NEAR(v1, expected1, 0.15)
      << "At t=0.5tau, V_out should be ~" << expected1 << "V, got " << v1 << "V";

  // After 1*tau, V_out should be ~63.2% of step
  for (int i = 0; i < 50; ++i) {
    filter.circuit().solver().step(DT, state);
  }
  double t2 = 100 * DT; // 1 ms = 1*tau
  double v2 = state.nodeVoltages[filter.outNet()];
  double expected2 = filter.analyticalStepResponse(5.0, t2);
  EXPECT_NEAR(v2, expected2, 0.15)
      << "At t=tau, V_out should be ~" << expected2 << "V, got " << v2 << "V";

  // After 5*tau, V_out should be ~99.3% of step (essentially steady state)
  for (int i = 0; i < 400; ++i) {
    filter.circuit().solver().step(DT, state);
  }
  double v3 = state.nodeVoltages[filter.outNet()];
  EXPECT_NEAR(v3, 5.0, 0.1) << "At t=5tau, V_out should be ~5V (steady state), got " << v3 << "V";
}

/* ----------------------------- Analytical Helpers ----------------------------- */

/** @test Analytical step response formula. */
TEST(RcLowPass, AnalyticalStepResponse) {
  RcLowPass filter(1e3, 1e-6); // tau = 1ms

  EXPECT_NEAR(filter.analyticalStepResponse(5.0, 0.0), 0.0, 1e-9) << "At t=0, V_out=0";
  EXPECT_NEAR(filter.analyticalStepResponse(5.0, 1e-3), 5.0 * (1.0 - std::exp(-1.0)), 1e-6)
      << "At t=tau, V_out=V_in*(1-1/e)";
  EXPECT_NEAR(filter.analyticalStepResponse(5.0, 5e-3), 5.0 * (1.0 - std::exp(-5.0)), 1e-6)
      << "At t=5tau, V_out~V_in";
}

/** @test Magnitude response: -3dB at cutoff. */
TEST(RcLowPass, MagnitudeResponseAtCutoff) {
  RcLowPass filter(1e3, 1e-6); // fc ~ 159 Hz
  // At fc: |H| = 1/sqrt(2) = 0.7071 (-3 dB)
  EXPECT_NEAR(filter.analyticalMagnitudeResponse(filter.cutoffHz()), 1.0 / std::sqrt(2.0), 1e-6)
      << "Magnitude at cutoff should be -3 dB (0.7071)";
}

/** @test Magnitude response: 1 at DC, 0 at infinity. */
TEST(RcLowPass, MagnitudeResponseLimits) {
  RcLowPass filter(1e3, 1e-6);
  // DC: |H(0)| = 1
  EXPECT_NEAR(filter.analyticalMagnitudeResponse(0.0), 1.0, 1e-9);
  // Very high frequency: |H| -> 0
  EXPECT_LT(filter.analyticalMagnitudeResponse(1e9), 0.001);
}
