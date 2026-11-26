/**
 * @file AcMnaSystem_uTest.cpp
 * @brief Unit tests for AC (frequency domain) circuit analysis.
 *
 * Tests cover:
 * - Complex MNA solver
 * - Low-pass RC filter response
 * - High-pass RC filter response
 * - Cutoff frequency detection
 * - Frequency sweep / Bode plot generation
 */

#include "src/sim/electronics/algorithms/mna/inc/AcMnaSystem.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

using sim::electronics::algorithms::mna::AcMnaSystem;
using sim::electronics::algorithms::mna::Complex;
using sim::electronics::algorithms::mna::frequencySweep;
using sim::electronics::algorithms::mna::NetID;

/* ----------------------------- Constants ----------------------------- */

// Test circuit values
constexpr double R_1K = 1000.0;  // 1k ohm
constexpr double C_1UF = 1.0e-6; // 1 uF
constexpr double L_1MH = 1.0e-3; // 1 mH
constexpr double V_IN = 1.0;     // 1V AC input

// Low-pass RC: fc = 1/(2*pi*R*C)
// With R=1k, C=1uF: fc = 159.15 Hz
constexpr double FC_RC = 1.0 / (2.0 * std::numbers::pi * R_1K * C_1UF);

// Tolerance for frequency domain tests
constexpr double FREQ_TOL = 0.02; // 2% tolerance
constexpr double DB_TOL = 0.5;    // 0.5 dB tolerance
constexpr double PHASE_TOL = 3.0; // 3 degree tolerance

/* ----------------------------- Basic AC MNA ----------------------------- */

/** @test Verify pure resistive circuit at any frequency. */
TEST(AcMnaTest, ResistiveCircuit_FrequencyIndependent) {
  // Voltage divider: 5V -> R1=1k -> Vout -> R2=1k -> GND
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  // Test at multiple frequencies - result should be same
  for (double freq : {10.0, 100.0, 1000.0, 10000.0}) {
    AcMnaSystem ac(3, 2.0 * std::numbers::pi * freq);

    // Stamp resistors
    ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
    ac.stampConductance(VOUT, GND, 1.0 / R_1K);

    // Add voltage source
    ac.addVoltageSource(VIN, GND, Complex(5.0, 0.0));

    auto result = ac.solve();
    ASSERT_TRUE(result.success);

    // Voltage divider: Vout = 5V * 0.5 = 2.5V
    EXPECT_NEAR(std::abs(result.nodeVoltages[VOUT]), 2.5, 0.01);
    EXPECT_NEAR(std::arg(result.nodeVoltages[VOUT]), 0.0, 0.01);
  }
}

/** @test Verify capacitor impedance varies with frequency. */
TEST(AcMnaTest, CapacitorImpedance) {
  // Simple RC: Vin -> R=1k -> Vout -> C=1uF -> GND
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  // At DC (very low freq), capacitor is open circuit -> Vout = Vin
  {
    AcMnaSystem ac(3, 2.0 * std::numbers::pi * 0.01); // 0.01 Hz
    ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
    ac.stampCapacitor(VOUT, GND, C_1UF);
    ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

    auto result = ac.solve();
    ASSERT_TRUE(result.success);

    // At very low freq, cap is nearly open, Vout ~ Vin
    EXPECT_GT(std::abs(result.nodeVoltages[VOUT]), 0.99);
  }

  // At high freq, capacitor is short circuit -> Vout ~ 0
  {
    AcMnaSystem ac(3, 2.0 * std::numbers::pi * 100000.0); // 100 kHz
    ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
    ac.stampCapacitor(VOUT, GND, C_1UF);
    ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

    auto result = ac.solve();
    ASSERT_TRUE(result.success);

    // At high freq, cap is nearly short, Vout ~ 0
    EXPECT_LT(std::abs(result.nodeVoltages[VOUT]), 0.1);
  }
}

/* ----------------------------- Low-Pass RC Filter ----------------------------- */

/** @test Low-pass RC filter at cutoff frequency: -3dB, -45 degrees. */
TEST(AcFilterTest, LowPassRC_AtCutoff) {
  // Low-pass: Vin -> R -> Vout -> C -> GND
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  AcMnaSystem ac(3, 2.0 * std::numbers::pi * FC_RC);

  ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
  ac.stampCapacitor(VOUT, GND, C_1UF);
  ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

  auto result = ac.solve();
  ASSERT_TRUE(result.success);

  Complex vOut = result.nodeVoltages[VOUT];
  double magnitude = std::abs(vOut);
  double magnitudeDb = 20.0 * std::log10(magnitude);
  double phaseDeg = std::arg(vOut) * 180.0 / std::numbers::pi;

  // At cutoff: |H| = 1/sqrt(2) = 0.707 = -3.01 dB
  EXPECT_NEAR(magnitude, 1.0 / std::sqrt(2.0), 0.01);
  EXPECT_NEAR(magnitudeDb, -3.01, DB_TOL);

  // At cutoff: phase = -45 degrees
  EXPECT_NEAR(phaseDeg, -45.0, PHASE_TOL);
}

/** @test Low-pass RC filter well below cutoff: ~0dB, ~0 degrees. */
TEST(AcFilterTest, LowPassRC_BelowCutoff) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;
  double freq = FC_RC / 10.0; // One decade below

  AcMnaSystem ac(3, 2.0 * std::numbers::pi * freq);
  ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
  ac.stampCapacitor(VOUT, GND, C_1UF);
  ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

  auto result = ac.solve();
  ASSERT_TRUE(result.success);

  double magnitude = std::abs(result.nodeVoltages[VOUT]);
  double phaseDeg = std::arg(result.nodeVoltages[VOUT]) * 180.0 / std::numbers::pi;

  // Well below cutoff: nearly unity gain, near-zero phase
  EXPECT_GT(magnitude, 0.99);
  EXPECT_NEAR(phaseDeg, -5.7, PHASE_TOL); // atan(0.1) ~ 5.7 deg
}

/** @test Low-pass RC filter well above cutoff: -20dB/decade slope. */
TEST(AcFilterTest, LowPassRC_AboveCutoff) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;
  double freq = FC_RC * 10.0; // One decade above

  AcMnaSystem ac(3, 2.0 * std::numbers::pi * freq);
  ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
  ac.stampCapacitor(VOUT, GND, C_1UF);
  ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

  auto result = ac.solve();
  ASSERT_TRUE(result.success);

  double magnitudeDb = 20.0 * std::log10(std::abs(result.nodeVoltages[VOUT]));
  double phaseDeg = std::arg(result.nodeVoltages[VOUT]) * 180.0 / std::numbers::pi;

  // One decade above: ~-20dB from DC, phase approaching -90
  EXPECT_NEAR(magnitudeDb, -20.0, 1.0);    // -20dB/decade
  EXPECT_NEAR(phaseDeg, -84.3, PHASE_TOL); // atan(10) ~ 84 deg
}

/* ----------------------------- High-Pass RC Filter ----------------------------- */

/** @test High-pass RC filter at cutoff frequency. */
TEST(AcFilterTest, HighPassRC_AtCutoff) {
  // High-pass: Vin -> C -> Vout -> R -> GND
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  AcMnaSystem ac(3, 2.0 * std::numbers::pi * FC_RC);

  ac.stampCapacitor(VIN, VOUT, C_1UF);
  ac.stampConductance(VOUT, GND, 1.0 / R_1K);
  ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

  auto result = ac.solve();
  ASSERT_TRUE(result.success);

  Complex vOut = result.nodeVoltages[VOUT];
  double magnitude = std::abs(vOut);
  double phaseDeg = std::arg(vOut) * 180.0 / std::numbers::pi;

  // At cutoff: |H| = 1/sqrt(2) = -3dB
  EXPECT_NEAR(magnitude, 1.0 / std::sqrt(2.0), 0.01);

  // High-pass at cutoff: phase = +45 degrees
  EXPECT_NEAR(phaseDeg, 45.0, PHASE_TOL);
}

/* ----------------------------- Frequency Sweep ----------------------------- */

/** @test Frequency sweep finds correct cutoff frequency. */
TEST(AcSweepTest, LowPassRC_FindCutoff) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  auto sweep = frequencySweep(3, VIN, VOUT, V_IN,
                              1.0,     // Start: 1 Hz
                              10000.0, // End: 10 kHz
                              20,      // Points per decade
                              [](AcMnaSystem& ac, double /*omega*/) {
                                ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
                                ac.stampCapacitor(VOUT, GND, C_1UF);
                              });

  ASSERT_GT(sweep.points.size(), 10u);

  // Find -3dB cutoff
  double fc = findCutoffFrequency(sweep);

  // Expected: fc = 159.15 Hz
  EXPECT_NEAR(fc, FC_RC, FC_RC * FREQ_TOL);
}

/** @test A sweep whose ends are equal is one finite point at that frequency. */
TEST(AcSweepTest, FrequencySweepSinglePointIsFinite) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  const auto SWEEP =
      frequencySweep(3, VIN, VOUT, V_IN, 100.0, 100.0, 10, [](AcMnaSystem& ac, double /*omega*/) {
        ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
        ac.stampCapacitor(VOUT, GND, C_1UF);
      });

  // RC low-pass at f: |H| = 1/sqrt(1 + (f/fc)^2), phase = -atan(f/fc).
  const double RATIO = 100.0 / FC_RC;
  ASSERT_EQ(SWEEP.points.size(), 1U);
  const auto& POINT = SWEEP.points.front();
  EXPECT_DOUBLE_EQ(POINT.frequency, 100.0);
  EXPECT_DOUBLE_EQ(POINT.omega, 2.0 * std::numbers::pi * 100.0);
  EXPECT_NEAR(POINT.magnitudeDb, -10.0 * std::log10(1.0 + RATIO * RATIO), 1e-9);
  EXPECT_NEAR(POINT.phaseDegrees, -std::atan(RATIO) * 180.0 / std::numbers::pi, 1e-9);
}

/** @test A span shorter than the spacing of its points per decade still has both ends. */
TEST(AcSweepTest, FrequencySweepShortSpanHasBothEnds) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  // 100 Hz to 150 Hz is 0.18 decade; two points per decade are 0.5 decade apart.
  const auto SWEEP =
      frequencySweep(3, VIN, VOUT, V_IN, 100.0, 150.0, 2, [](AcMnaSystem& ac, double /*omega*/) {
        ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
        ac.stampCapacitor(VOUT, GND, C_1UF);
      });

  ASSERT_EQ(SWEEP.points.size(), 2U);
  const std::array<double, 2> HERTZ = {100.0, 150.0};
  for (std::size_t i = 0; i < HERTZ.size(); ++i) {
    const double RATIO = HERTZ[i] / FC_RC;
    EXPECT_DOUBLE_EQ(SWEEP.points[i].frequency, HERTZ[i]);
    EXPECT_NEAR(SWEEP.points[i].magnitudeDb, -10.0 * std::log10(1.0 + RATIO * RATIO), 1e-9);
  }
}

/** @test A sweep has floor(decades * points per decade) intervals and both ends exactly. */
TEST(AcSweepTest, FrequencySweepPointCountAndEnds) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;
  struct Span {
    double startHz;
    double endHz;
    std::size_t pointsPerDecade;
    std::size_t points;
    bool wholeDecades;
  };
  // Three decades at 10 per decade, twice (the second's logarithms are not
  // whole numbers), four decades at 20, and a span of no whole decade.
  // (2.37 decades, where the 23 intervals stretch to end on 7 kHz).
  for (const Span& SPAN : {Span{10.0, 10000.0, 10, 31, true}, Span{20.0, 20000.0, 10, 31, true},
                           Span{1.0, 10000.0, 20, 81, true}, Span{30.0, 7000.0, 10, 24, false}}) {
    const auto SWEEP = frequencySweep(3, VIN, VOUT, V_IN, SPAN.startHz, SPAN.endHz,
                                      SPAN.pointsPerDecade, [](AcMnaSystem& ac, double /*omega*/) {
                                        ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
                                        ac.stampCapacitor(VOUT, GND, C_1UF);
                                      });
    ASSERT_EQ(SWEEP.points.size(), SPAN.points) << SPAN.startHz << " Hz to " << SPAN.endHz;
    // The ends are the frequencies given, to the last bit.
    EXPECT_EQ(SWEEP.points.front().frequency, SPAN.startHz);
    EXPECT_EQ(SWEEP.points.back().frequency, SPAN.endHz);
    if (SPAN.wholeDecades) {
      // One decade on is ten times the frequency.
      EXPECT_NEAR(SWEEP.points[SPAN.pointsPerDecade].frequency, 10.0 * SPAN.startHz,
                  1e-9 * SPAN.startHz);
    }
  }
}

/** @test Arguments that describe no sweep return no points. */
TEST(AcSweepTest, FrequencySweepRefusesWhatIsNoSweep) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;
  const auto POINTS = [](double inputVolts, double startHz, double endHz,
                         std::size_t pointsPerDecade) {
    return frequencySweep(3, VIN, VOUT, inputVolts, startHz, endHz, pointsPerDecade,
                          [](AcMnaSystem& ac, double /*omega*/) {
                            ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
                            ac.stampCapacitor(VOUT, GND, C_1UF);
                          })
        .points.size();
  };
  const double NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();
  const double INFINITE = std::numeric_limits<double>::infinity();

  EXPECT_EQ(POINTS(V_IN, 0.0, 1000.0, 10), 0U) << "start at zero";
  EXPECT_EQ(POINTS(V_IN, -10.0, 1000.0, 10), 0U) << "negative start";
  EXPECT_EQ(POINTS(V_IN, 1000.0, 10.0, 10), 0U) << "end below start";
  EXPECT_EQ(POINTS(V_IN, NOT_A_NUMBER, 1000.0, 10), 0U) << "start not a number";
  EXPECT_EQ(POINTS(V_IN, 10.0, NOT_A_NUMBER, 10), 0U) << "end not a number";
  EXPECT_EQ(POINTS(V_IN, 10.0, INFINITE, 10), 0U) << "infinite end";
  EXPECT_EQ(POINTS(V_IN, 10.0, 1000.0, 0), 0U) << "no points per decade";
  EXPECT_EQ(POINTS(0.0, 10.0, 1000.0, 10), 0U) << "no input voltage";
  EXPECT_EQ(POINTS(-1.0, 10.0, 1000.0, 10), 0U) << "negative input magnitude";
  EXPECT_EQ(POINTS(NOT_A_NUMBER, 10.0, 1000.0, 10), 0U) << "input not a number";
  EXPECT_EQ(POINTS(INFINITE, 10.0, 1000.0, 10), 0U) << "infinite input";
}

/** @test Frequency sweep generates valid Bode plot data. */
TEST(AcSweepTest, BodePlotData) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  auto sweep = frequencySweep(3, VIN, VOUT, V_IN,
                              10.0,    // Start: 10 Hz
                              10000.0, // End: 10 kHz
                              10,      // Points per decade
                              [](AcMnaSystem& ac, double /*omega*/) {
                                ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
                                ac.stampCapacitor(VOUT, GND, C_1UF);
                              });

  // Verify we have points across 3 decades
  ASSERT_GT(sweep.points.size(), 25u);

  // 10 Hz (well below cutoff): expect ~0 dB
  EXPECT_GT(sweep.points.front().magnitudeDb, -1.0);

  // Last point (10 kHz, well above cutoff): significant attenuation
  EXPECT_LT(sweep.points.back().magnitudeDb, -30.0);

  // Verify monotonic decrease for low-pass
  for (std::size_t i = 1; i < sweep.points.size(); ++i) {
    EXPECT_LE(sweep.points[i].magnitudeDb, sweep.points[i - 1].magnitudeDb + 0.1);
  }
}

/* ----------------------------- Inductor Tests ----------------------------- */

/** @test Inductor impedance increases with frequency. */
TEST(AcMnaTest, InductorImpedance) {
  // RL circuit: Vin -> R=1k -> Vout -> L=1mH -> GND
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;

  // At low freq, inductor is short -> Vout ~ 0
  {
    AcMnaSystem ac(3, 2.0 * std::numbers::pi * 100.0); // 100 Hz
    ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
    ac.stampInductor(VOUT, GND, L_1MH);
    ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

    auto result = ac.solve();
    ASSERT_TRUE(result.success);

    // At low freq, inductor impedance is low
    // Z_L = j*omega*L = j*2*pi*100*0.001 = j*0.628 ohms
    // Voltage divider: Vout = Vin * Z_L / (R + Z_L)
    EXPECT_LT(std::abs(result.nodeVoltages[VOUT]), 0.01);
  }

  // At high freq, inductor is open -> Vout ~ Vin
  {
    AcMnaSystem ac(3, 2.0 * std::numbers::pi * 1000000.0); // 1 MHz
    ac.stampConductance(VIN, VOUT, 1.0 / R_1K);
    ac.stampInductor(VOUT, GND, L_1MH);
    ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

    auto result = ac.solve();
    ASSERT_TRUE(result.success);

    // At high freq, inductor impedance >> R (allowing some margin)
    EXPECT_GT(std::abs(result.nodeVoltages[VOUT]), 0.98);
  }
}

/* ----------------------------- RLC Circuit ----------------------------- */

/** @test Series RLC resonance. */
TEST(AcMnaTest, SeriesRLC_Resonance) {
  // Series RLC: Vin -> R -> L -> C -> GND, measure across R
  // Resonant freq: f0 = 1/(2*pi*sqrt(LC))
  // At resonance, L and C cancel, leaving only R

  constexpr double L = 10.0e-3; // 10 mH
  constexpr double C = 10.0e-6; // 10 uF
  constexpr double R = 100.0;   // 100 ohm

  // Resonant frequency
  double f0 = 1.0 / (2.0 * std::numbers::pi * std::sqrt(L * C));
  // f0 = 1/(2*pi*sqrt(0.01 * 0.00001)) = 1/(2*pi*0.000316) ~ 503 Hz

  constexpr NetID GND = 0, VIN = 1, V1 = 2, V2 = 3;

  // At resonance
  {
    AcMnaSystem ac(4, 2.0 * std::numbers::pi * f0);
    ac.stampConductance(VIN, V1, 1.0 / R);
    ac.stampInductor(V1, V2, L);
    ac.stampCapacitor(V2, GND, C);
    ac.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

    auto result = ac.solve();
    ASSERT_TRUE(result.success);

    // At resonance, current is maximum (only R limits it)
    // I = V/R = 1/100 = 10mA
    // Voltage across R = I*R = V_in (all voltage across R)
    Complex vAcrossR = result.nodeVoltages[VIN] - result.nodeVoltages[V1];
    EXPECT_NEAR(std::abs(vAcrossR), V_IN, 0.05);
  }
}

/* ----------------------------- Direct API coverage ----------------------------- */

/** @test setFrequency / omega round-trip */
TEST(AcMnaTest, SetFrequencyMatchesOmega) {
  AcMnaSystem ac(2);
  ac.setFrequency(1000.0);
  EXPECT_NEAR(ac.omega(), 2.0 * std::numbers::pi * 1000.0, 1e-9);

  ac.setFrequency(1e6);
  EXPECT_NEAR(ac.omega(), 2.0 * std::numbers::pi * 1e6, 1.0);
}

/** @test addVoltageSource returns sequential indices and increments voltageSourceCount */
TEST(AcMnaTest, AddVoltageSourceReturnsSequentialIndex) {
  AcMnaSystem ac(4);
  EXPECT_EQ(ac.voltageSourceCount(), 0u);
  EXPECT_EQ(ac.addVoltageSource(1, 0, Complex(1.0, 0.0)), 0u);
  EXPECT_EQ(ac.voltageSourceCount(), 1u);
  EXPECT_EQ(ac.addVoltageSource(2, 0, Complex(0.0, 1.0)), 1u);
  EXPECT_EQ(ac.voltageSourceCount(), 2u);
  EXPECT_EQ(ac.addVoltageSource(3, 0, Complex(0.5, 0.5)), 2u);
  EXPECT_EQ(ac.voltageSourceCount(), 3u);
}

/** @test netCount accessor reflects construction */
TEST(AcMnaTest, NetCountReflectsConstruction) {
  const AcMnaSystem AC{8};
  EXPECT_EQ(AC.netCount(), 8u);
}

/** @test clear() resets Y_, I_, and voltageSources_ so a subsequent solve at a
 *        new frequency reflects that frequency, not the cached state from before.
 *
 * Topology: VIN -> R -> VOUT -> C -> GND (RC low-pass, fc = 1/(2*pi*R*C)
 * = 159 Hz for R=1k, C=1uF). For a 1st-order low-pass:
 *   - At f = 100 Hz (below cutoff): |H| > 0.5 (light attenuation)
 *   - At f = 5 kHz (>=30x cutoff): |H| < 0.05 (heavy attenuation)
 * The post-clear solve must produce a magnitude consistent with the NEW
 * frequency, not the prior one.
 */
TEST(AcMnaTest, ClearResetsMatrix) {
  constexpr NetID VIN_NET = 1, VOUT = 2;
  constexpr double F_LOW = 100.0;
  constexpr double F_HIGH = 5000.0;

  AcMnaSystem ac(3, 2.0 * std::numbers::pi * F_LOW);
  ac.stampConductance(VIN_NET, VOUT, 1.0 / R_1K);
  ac.stampCapacitor(VOUT, 0, C_1UF);
  ac.addVoltageSource(VIN_NET, 0, Complex(V_IN, 0.0));
  const auto LOW = ac.solve();
  ASSERT_TRUE(LOW.success);
  const double MAG_LOW = std::abs(LOW.nodeVoltages[VOUT]);

  ac.clear();
  EXPECT_EQ(ac.voltageSourceCount(), 0u);

  ac.setFrequency(2.0 * std::numbers::pi * F_HIGH);
  ac.stampConductance(VIN_NET, VOUT, 1.0 / R_1K);
  ac.stampCapacitor(VOUT, 0, C_1UF);
  ac.addVoltageSource(VIN_NET, 0, Complex(V_IN, 0.0));
  const auto HIGH = ac.solve();
  ASSERT_TRUE(HIGH.success);
  const double MAG_HIGH = std::abs(HIGH.nodeVoltages[VOUT]);

  // 100 Hz is below the cutoff -> the filter passes most of the input.
  EXPECT_GT(MAG_LOW, 0.5);
  // 5 kHz is >= 30x cutoff -> heavy attenuation by the new frequency.
  EXPECT_LT(MAG_HIGH, 0.1);
  // And the post-clear result must reflect the new frequency, not the cached one.
  EXPECT_LT(MAG_HIGH, MAG_LOW * 0.2);
}

/** @test clearCurrents() leaves the admittance (G + j*w*C) matrix intact: a
 *        follow-up solve driven only by a voltage source produces the same
 *        nodal answer as if no prior current source had been stamped.
 *
 * Topology: VIN -> R -> VOUT -> C -> GND (RC low-pass).
 */
TEST(AcMnaTest, ClearCurrentsLeavesAdmittanceIntact) {
  constexpr NetID VIN_NET = 1, VOUT = 2;
  constexpr double FREQ = 100.0;
  const double OMEGA = 2.0 * std::numbers::pi * FREQ;

  // Reference: never stamp a current source.
  AcMnaSystem ref(3, OMEGA);
  ref.stampConductance(VIN_NET, VOUT, 1.0 / R_1K);
  ref.stampCapacitor(VOUT, 0, C_1UF);
  ref.addVoltageSource(VIN_NET, 0, Complex(V_IN, 0.0));
  const auto REF_R = ref.solve();
  ASSERT_TRUE(REF_R.success);

  // Same topology, but stamp + clear a current source first.
  AcMnaSystem ac(3, OMEGA);
  ac.stampConductance(VIN_NET, VOUT, 1.0 / R_1K);
  ac.stampCapacitor(VOUT, 0, C_1UF);
  ac.stampCurrent(VIN_NET, 0, Complex(1e-3, 0.0));
  ac.clearCurrents();
  ac.addVoltageSource(VIN_NET, 0, Complex(V_IN, 0.0));
  const auto AC_R = ac.solve();
  ASSERT_TRUE(AC_R.success);

  // The admittance was unchanged; the cleared current should have no residual
  // effect; node voltages must match the reference solve.
  EXPECT_NEAR(std::abs(AC_R.nodeVoltages[VOUT] - REF_R.nodeVoltages[VOUT]), 0.0, 1e-12);
}

/** @test Cached factorize + solveFactorized matches single-shot solve */
TEST(AcMnaTest, FactorizedSolveMatchesSingleShot) {
  constexpr NetID GND = 0, VIN = 1, VOUT = 2;
  constexpr double OMEGA = 2.0 * std::numbers::pi * 100.0;

  AcMnaSystem ac1(3, OMEGA);
  ac1.stampConductance(VIN, VOUT, 1.0 / R_1K);
  ac1.stampCapacitor(VOUT, GND, C_1UF);
  ac1.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));
  const auto SHOT = ac1.solve();
  ASSERT_TRUE(SHOT.success);

  AcMnaSystem ac2(3, OMEGA);
  ac2.stampConductance(VIN, VOUT, 1.0 / R_1K);
  ac2.stampCapacitor(VOUT, GND, C_1UF);
  ac2.addVoltageSource(VIN, GND, Complex(V_IN, 0.0));

  sim::electronics::algorithms::mna::AcMnaFactorizedWorkspace ws;
  ws.prepare(8);
  std::vector<Complex> nodeV(3);
  std::vector<Complex> vsrcCurr(1);
#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(ac2.factorize(ws));
  EXPECT_TRUE(ws.isFactorized());

  ASSERT_TRUE(ac2.solveFactorized(ws, nodeV.data(), vsrcCurr.data()));

  EXPECT_NEAR(std::abs(nodeV[VOUT] - SHOT.nodeVoltages[VOUT]), 0.0, 1e-9);
#else
  // Kept factors need LAPACKE: without it both calls refuse.
  EXPECT_FALSE(ac2.factorize(ws));
  EXPECT_FALSE(ws.isFactorized());
  EXPECT_FALSE(ac2.solveFactorized(ws, nodeV.data(), vsrcCurr.data()));
#endif
}

/** @test invalidate() forces a fresh factorization */
TEST(AcMnaTest, FactorizedWorkspaceInvalidate) {
  sim::electronics::algorithms::mna::AcMnaFactorizedWorkspace ws;
  ws.prepare(4);
  EXPECT_FALSE(ws.isFactorized());

  AcMnaSystem ac(2, 2.0 * std::numbers::pi * 100.0);
  ac.stampConductance(1, 0, 1.0 / R_1K);
  ac.addVoltageSource(1, 0, Complex(V_IN, 0.0));
#if COMPAT_HAVE_LAPACKE
  ASSERT_TRUE(ac.factorize(ws));
  EXPECT_TRUE(ws.isFactorized());
#else
  EXPECT_FALSE(ac.factorize(ws));
#endif

  ws.invalidate();
  EXPECT_FALSE(ws.isFactorized());
}

/** @test Solve workspace canHandle reports capacity correctly */
TEST(AcMnaTest, SolveWorkspaceCapacityCheck) {
  sim::electronics::algorithms::mna::AcMnaSolveWorkspace ws;
  ws.prepare(8);
  EXPECT_TRUE(ws.canHandle(8));
  EXPECT_TRUE(ws.canHandle(4));
  EXPECT_FALSE(ws.canHandle(9));
}
