/**
 * @file CapacitorModel_uTest.cpp
 * @brief Unit tests for capacitor physics model.
 */

#include "src/sim/electronics/devices/linear/inc/CapacitorModel.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::linear::CapacitorModel;

/* ----------------------------- Helper Functions ----------------------------- */

constexpr double TOLERANCE = 1e-9;
constexpr double PI = 3.141592653589793;

/* ----------------------------- Reactance Tests ----------------------------- */

/** @test Reactance calculation (Xc = 1/(2*pi*f*C)). */
TEST(CapacitorModel, Reactance) {
  constexpr double C = 1e-6;   // 1 uF
  constexpr double f = 1000.0; // 1 kHz

  double xc = CapacitorModel::reactance(C, f);

  // Xc = 1 / (2 * pi * 1000 * 1e-6) ~ 159.15  ohm
  double expected = 1.0 / (2.0 * PI * f * C);
  EXPECT_NEAR(xc, expected, TOLERANCE);
  EXPECT_NEAR(xc, 159.15, 0.01); // Approximate value
}

/** @test Reactance at different frequencies. */
TEST(CapacitorModel, ReactanceVsFrequency) {
  constexpr double C = 100e-9; // 100 nF

  // At 1 kHz
  double xc_1k = CapacitorModel::reactance(C, 1000.0);
  EXPECT_NEAR(xc_1k, 1591.5, 0.1);

  // At 10 kHz (should be 1/10th of 1 kHz)
  double xc_10k = CapacitorModel::reactance(C, 10000.0);
  EXPECT_NEAR(xc_10k, xc_1k / 10.0, TOLERANCE);
}

/** @test Impedance equals reactance for ideal capacitor. */
TEST(CapacitorModel, ImpedanceEqualsReactance) {
  constexpr double C = 1e-6;
  constexpr double f = 1000.0;

  double xc = CapacitorModel::reactance(C, f);
  double z = CapacitorModel::impedance(C, f);

  EXPECT_NEAR(z, xc, TOLERANCE);
}

/* ----------------------------- Standard Values ----------------------------- */

/** @test Verify reactance for common capacitor values. */
TEST(CapacitorModel, StandardValues) {
  // 100 pF at 1 MHz
  double xc_100pF = CapacitorModel::reactance(100e-12, 1e6);
  EXPECT_NEAR(xc_100pF, 1591.5, 0.1);

  // 1 uF at 60 Hz (AC line frequency)
  double xc_1uF_60Hz = CapacitorModel::reactance(1e-6, 60.0);
  EXPECT_NEAR(xc_1uF_60Hz, 2653.0, 1.0);

  // 10 uF at 1 kHz (common filter cap)
  double xc_10uF_1kHz = CapacitorModel::reactance(10e-6, 1000.0);
  EXPECT_NEAR(xc_10uF_1kHz, 15.92, 0.01);
}
