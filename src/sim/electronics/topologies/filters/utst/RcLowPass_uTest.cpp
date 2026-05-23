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

#include "src/sim/electronics/topologies/filters/inc/RcLowPass.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

using sim::electronics::algorithms::transient::IntegrationMethod;
using sim::electronics::algorithms::transient::TransientConfig;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;
using sim::electronics::topologies::filters::RcLowPass;

/* ----------------------------- Construction Tests ----------------------------- */

/** @test Construction stores R and C, computes correct cutoff frequency. */
TEST(RcLowPassTest, ConstructionStoresValues) {
  // 1k ohm, 159 nF -> ~1 kHz cutoff
  RcLowPass filter(1e3, 159e-9);

  EXPECT_NEAR(filter.cutoffHz(), 1000.0, 5.0) << "Cutoff should be ~1 kHz for R=1k, C=159nF";
  EXPECT_NEAR(filter.tau(), 159e-6, 1e-9) << "Time constant should be R*C = 159 us";
}

/** @test Net accessors return valid IDs. */
TEST(RcLowPassTest, NetAccessors) {
  RcLowPass filter(1e3, 1e-6);
  // inNet and outNet should be different and non-zero (ground is 0)
  EXPECT_NE(filter.inNet(), 0u);
  EXPECT_NE(filter.outNet(), 0u);
  EXPECT_NE(filter.inNet(), filter.outNet());
}

/** @test Different R and C produce expected cutoff. */
TEST(RcLowPassTest, ConstructionDifferentValues) {
  RcLowPass filter(10e3, 1e-6); // 10k ohm, 1 uF
  // tau = 10ms, fc = 1/(2*pi*tau) ~ 15.92 Hz
  EXPECT_NEAR(filter.cutoffHz(), 15.915, 0.01);
  EXPECT_NEAR(filter.tau(), 10e-3, 1e-6);
}

/* ----------------------------- DC Operating Point ----------------------------- */

/** @test DC operating point: V_out should equal V_in (no current flows steady-state). */
TEST(RcLowPassTest, DcOperatingPoint) {
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
TEST(RcLowPassTest, StepResponseMatchesAnalytical) {
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
TEST(RcLowPassTest, AnalyticalStepResponse) {
  RcLowPass filter(1e3, 1e-6); // tau = 1ms

  EXPECT_NEAR(filter.analyticalStepResponse(5.0, 0.0), 0.0, 1e-9) << "At t=0, V_out=0";
  EXPECT_NEAR(filter.analyticalStepResponse(5.0, 1e-3), 5.0 * (1.0 - std::exp(-1.0)), 1e-6)
      << "At t=tau, V_out=V_in*(1-1/e)";
  EXPECT_NEAR(filter.analyticalStepResponse(5.0, 5e-3), 5.0 * (1.0 - std::exp(-5.0)), 1e-6)
      << "At t=5tau, V_out~V_in";
}

/** @test Magnitude response: -3dB at cutoff. */
TEST(RcLowPassTest, MagnitudeResponseAtCutoff) {
  RcLowPass filter(1e3, 1e-6); // fc ~ 159 Hz
  // At fc: |H| = 1/sqrt(2) = 0.7071 (-3 dB)
  EXPECT_NEAR(filter.analyticalMagnitudeResponse(filter.cutoffHz()), 1.0 / std::sqrt(2.0), 1e-6)
      << "Magnitude at cutoff should be -3 dB (0.7071)";
}

/** @test Magnitude response: 1 at DC, 0 at infinity. */
TEST(RcLowPassTest, MagnitudeResponseLimits) {
  RcLowPass filter(1e3, 1e-6);
  // DC: |H(0)| = 1
  EXPECT_NEAR(filter.analyticalMagnitudeResponse(0.0), 1.0, 1e-9);
  // Very high frequency: |H| -> 0
  EXPECT_LT(filter.analyticalMagnitudeResponse(1e9), 0.001);
}

/* ----------------------------- Transient Frequency Response ----------------------------- */

namespace {

/// Drive the filter with a unit-amplitude sine wave at frequency `freqHz`,
/// settle for `settleTau` time constants, then measure peak-to-peak of the
/// output over one full period. Returns measured gain (vout_pp / 2.0).
double measureTransientGain(RcLowPass& filter, double freqHz, double dt, double settleTau) {
  filter.build();
  filter.setInputVoltage(0.0);
  TransientState state;
  state.resize(filter.circuit().netCount(), 0);
  filter.circuit().computeDC(state);
  filter.circuit().solver().setIntegrationMethod(IntegrationMethod::TRAPEZOIDAL);

  const double OMEGA = 2.0 * M_PI * freqHz;
  const double SETTLE_TIME = settleTau * filter.tau();
  const auto SETTLE_STEPS = static_cast<std::size_t>(SETTLE_TIME / dt);

  // Settle.
  double t = 0.0;
  for (std::size_t i = 0; i < SETTLE_STEPS; ++i) {
    t += dt;
    filter.setInputVoltage(std::sin(OMEGA * t));
    filter.circuit().solver().step(dt, state);
  }

  // Measure peak-to-peak over one full period.
  const double PERIOD = 1.0 / freqHz;
  const auto MEASURE_STEPS = static_cast<std::size_t>(PERIOD / dt) + 1;
  double vMin = std::numeric_limits<double>::infinity();
  double vMax = -std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < MEASURE_STEPS; ++i) {
    t += dt;
    filter.setInputVoltage(std::sin(OMEGA * t));
    filter.circuit().solver().step(dt, state);
    const double V_OUT = state.nodeVoltages[filter.outNet()];
    vMin = std::min(vMin, V_OUT);
    vMax = std::max(vMax, V_OUT);
  }
  return (vMax - vMin) / 2.0;
}

} // namespace

/** @test Sine input below cutoff passes through with near-unity gain. */
TEST(RcLowPassTest, TransientFrequencyResponseBelowCutoff) {
  // R=1k, C=1uF -> fc ~ 159 Hz. Test at f=10 Hz (~16x below cutoff).
  RcLowPass filter(1e3, 1e-6);
  const double F = 10.0;
  const double MEASURED = measureTransientGain(filter, F, /*dt=*/100e-6,
                                               /*settleTau=*/8.0);
  const double EXPECTED = filter.analyticalMagnitudeResponse(F);
  EXPECT_NEAR(MEASURED, EXPECTED, 0.02) << "Below-cutoff gain should match analytical: expected "
                                        << EXPECTED << ", measured " << MEASURED;
}

/** @test Sine input at cutoff frequency produces -3 dB (0.707) gain. */
TEST(RcLowPassTest, TransientFrequencyResponseAtCutoff) {
  RcLowPass filter(1e3, 1e-6);
  const double F = filter.cutoffHz();
  const double MEASURED = measureTransientGain(filter, F, /*dt=*/10e-6,
                                               /*settleTau=*/8.0);
  // Expected: 1/sqrt(2) ~ 0.7071. Allow 5% for trapezoidal numerical error.
  EXPECT_NEAR(MEASURED, 1.0 / std::sqrt(2.0), 0.04)
      << "At cutoff, gain should be -3 dB (0.7071); measured " << MEASURED;
}

/** @test Sine input above cutoff is attenuated according to 1/(omega*tau) roll-off. */
TEST(RcLowPassTest, TransientFrequencyResponseAboveCutoff) {
  RcLowPass filter(1e3, 1e-6);
  // 10x above cutoff: |H| ~= 1/sqrt(101) ~ 0.0995
  const double F = 10.0 * filter.cutoffHz();
  const double MEASURED = measureTransientGain(filter, F, /*dt=*/2e-6,
                                               /*settleTau=*/8.0);
  const double EXPECTED = filter.analyticalMagnitudeResponse(F);
  EXPECT_NEAR(MEASURED, EXPECTED, 0.02) << "Above-cutoff gain should match analytical: expected "
                                        << EXPECTED << ", measured " << MEASURED;
}

/** @test Frequency response is monotonically decreasing across the swept range. */
TEST(RcLowPassTest, TransientFrequencyResponseMonotonic) {
  RcLowPass filter(1e3, 1e-6);
  // Sweep three frequencies spanning the cutoff: 0.1*fc, fc, 10*fc.
  const double FC = filter.cutoffHz();
  const double LOW = measureTransientGain(filter, 0.1 * FC, 100e-6, 8.0);
  const double MID = measureTransientGain(filter, FC, 10e-6, 8.0);
  const double HIGH = measureTransientGain(filter, 10.0 * FC, 2e-6, 8.0);
  EXPECT_GT(LOW, MID) << "Gain at 0.1*fc must exceed gain at fc";
  EXPECT_GT(MID, HIGH) << "Gain at fc must exceed gain at 10*fc";
}
