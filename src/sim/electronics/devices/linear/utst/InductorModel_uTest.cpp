/**
 * @file InductorModel_uTest.cpp
 * @brief Unit tests for inductor physics model.
 */

#include "src/sim/electronics/devices/linear/inc/InductorModel.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::linear::InductorModel;

/* ----------------------------- Helper Functions ----------------------------- */

constexpr double TOLERANCE = 1e-9;
constexpr double PI = 3.141592653589793;

/* ----------------------------- Reactance Tests ----------------------------- */

/** @test Reactance calculation (XL = 2*pi*f*L). */
TEST(InductorModel, Reactance) {
  constexpr double L = 1e-3;   // 1 mH
  constexpr double f = 1000.0; // 1 kHz

  double xl = InductorModel::reactance(L, f);

  // XL = 2 * pi * 1000 * 1e-3 ~ 6.283  ohm
  double expected = 2.0 * PI * f * L;
  EXPECT_NEAR(xl, expected, TOLERANCE);
  EXPECT_NEAR(xl, 6.283, 0.001); // Approximate value
}

/** @test Reactance at different frequencies. */
TEST(InductorModel, ReactanceVsFrequency) {
  constexpr double L = 100e-6; // 100 uH

  // At 1 kHz
  double xl_1k = InductorModel::reactance(L, 1000.0);
  EXPECT_NEAR(xl_1k, 0.6283, 0.0001);

  // At 10 kHz (should be 10x of 1 kHz)
  double xl_10k = InductorModel::reactance(L, 10000.0);
  EXPECT_NEAR(xl_10k, xl_1k * 10.0, TOLERANCE);
}

/** @test Impedance equals reactance for ideal inductor. */
TEST(InductorModel, ImpedanceEqualsReactance) {
  constexpr double L = 1e-3;
  constexpr double f = 1000.0;

  double xl = InductorModel::reactance(L, f);
  double z = InductorModel::impedance(L, f);

  EXPECT_NEAR(z, xl, TOLERANCE);
}

/* ----------------------------- Standard Values ----------------------------- */

/** @test Verify reactance for common inductor values. */
TEST(InductorModel, StandardValues) {
  // 1 uH at 1 MHz
  double xl_1uH = InductorModel::reactance(1e-6, 1e6);
  EXPECT_NEAR(xl_1uH, 6.283, 0.001);

  // 10 mH at 60 Hz (AC line frequency)
  double xl_10mH_60Hz = InductorModel::reactance(10e-3, 60.0);
  EXPECT_NEAR(xl_10mH_60Hz, 3.77, 0.01);

  // 100 uH at 1 kHz (common filter inductor)
  double xl_100uH_1kHz = InductorModel::reactance(100e-6, 1000.0);
  EXPECT_NEAR(xl_100uH_1kHz, 0.6283, 0.0001);
}

/* ----------------------------- Reactance vs Frequency Linearity ----------------------------- */

/** @test Verify linear relationship between reactance and frequency. */
TEST(InductorModel, ReactanceFrequencyLinearity) {
  constexpr double L = 1e-3;

  double xl_100Hz = InductorModel::reactance(L, 100.0);
  double xl_200Hz = InductorModel::reactance(L, 200.0);

  // Doubling frequency should double reactance
  EXPECT_NEAR(xl_200Hz, xl_100Hz * 2.0, TOLERANCE);
}
