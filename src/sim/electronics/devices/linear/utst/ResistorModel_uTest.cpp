/**
 * @file ResistorModel_uTest.cpp
 * @brief Unit tests for resistor physics model.
 */

#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::linear::ResistorModel;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::MnaSystemSparse;

/* ----------------------------- Helper Functions ----------------------------- */

constexpr double TOLERANCE = 1e-12;

/* ----------------------------- Conductance Tests ----------------------------- */

/** @test Conductance calculation (Ohm's law: G = 1/R). */
TEST(ResistorModelTest, Conductance) {
  EXPECT_NEAR(ResistorModel::conductance(1000.0), 0.001, TOLERANCE); // 1k ohm -> 1mS
  EXPECT_NEAR(ResistorModel::conductance(100.0), 0.01, TOLERANCE);   // 100 ohm -> 10mS
  EXPECT_NEAR(ResistorModel::conductance(10.0), 0.1, TOLERANCE);     // 10 ohm -> 100mS
  EXPECT_NEAR(ResistorModel::conductance(1.0), 1.0, TOLERANCE);      // 1 ohm -> 1S
}

/** @test Conductance is reciprocal of resistance. */
TEST(ResistorModelTest, ConductanceReciprocal) {
  constexpr double R = 4700.0; // 4.7k ohm
  double g = ResistorModel::conductance(R);
  EXPECT_NEAR(1.0 / g, R, TOLERANCE);
}

/* ----------------------------- Current Tests ----------------------------- */

/** @test Current calculation (Ohm's law: I = V/R). */
TEST(ResistorModelTest, Current) {
  EXPECT_NEAR(ResistorModel::current(5.0, 1000.0), 0.005, TOLERANCE); // 5V / 1k ohm = 5mA
  EXPECT_NEAR(ResistorModel::current(3.3, 100.0), 0.033, TOLERANCE);  // 3.3V / 100 ohm = 33mA
  EXPECT_NEAR(ResistorModel::current(1.0, 10.0), 0.1, TOLERANCE);     // 1V / 10 ohm = 100mA
}

/** @test Zero voltage yields zero current. */
TEST(ResistorModelTest, ZeroVoltage) {
  EXPECT_NEAR(ResistorModel::current(0.0, 1000.0), 0.0, TOLERANCE);
}

/** @test Negative voltage yields negative current. */
TEST(ResistorModelTest, NegativeVoltage) {
  EXPECT_NEAR(ResistorModel::current(-5.0, 1000.0), -0.005, TOLERANCE);
}

/* ----------------------------- Stamping Tests (Dense) ----------------------------- */

/** @test Stamp resistor into dense MNA system. */
TEST(ResistorModelTest, StampDense) {
  constexpr std::size_t NET_COUNT = 3;
  constexpr int VDD = 1;
  constexpr int OUTPUT = 2;
  constexpr int GND = 0;

  MnaSystem mna(NET_COUNT);

  // Voltage divider: VDD --(R1=10k ohm)-- OUTPUT --(R2=10k ohm)-- GND
  ResistorModel::stamp(mna, VDD, OUTPUT, 10e3); // R1
  ResistorModel::stamp(mna, OUTPUT, GND, 10e3); // R2

  // Voltage source: VDD = 5V
  mna.addVoltageSource(VDD, GND, 5.0);

  // Solve
  auto result = mna.solve();

  // Verify voltage divider: OUTPUT should be ~2.5V (half of 5V)
  EXPECT_NEAR(result.nodeVoltages[OUTPUT], 2.5, 1e-6);
}

/** @test Stamp multiple resistors (series resistance check). */
TEST(ResistorModelTest, SeriesResistance) {
  constexpr std::size_t NET_COUNT = 4;
  constexpr int VDD = 1;
  constexpr int MID1 = 2;
  constexpr int MID2 = 3;
  constexpr int GND = 0;

  MnaSystem mna(NET_COUNT);

  // Series: VDD --(1k ohm)-- MID1 --(1k ohm)-- MID2 --(1k ohm)-- GND
  ResistorModel::stamp(mna, VDD, MID1, 1000.0);
  ResistorModel::stamp(mna, MID1, MID2, 1000.0);
  ResistorModel::stamp(mna, MID2, GND, 1000.0);

  // Voltage source: VDD = 3V
  mna.addVoltageSource(VDD, GND, 3.0);

  // Solve
  auto result = mna.solve();

  // Verify voltage drop: each resistor sees 1V
  EXPECT_NEAR(result.nodeVoltages[VDD] - result.nodeVoltages[MID1], 1.0, 1e-6);
  EXPECT_NEAR(result.nodeVoltages[MID1] - result.nodeVoltages[MID2], 1.0, 1e-6);
  EXPECT_NEAR(result.nodeVoltages[MID2] - result.nodeVoltages[GND], 1.0, 1e-6);
}

/* ----------------------------- Stamping Tests (Sparse) ----------------------------- */

/** @test Stamp resistor into sparse MNA system. */
TEST(ResistorModelTest, StampSparse) {
  constexpr std::size_t NET_COUNT = 3;
  constexpr int VDD = 1;
  constexpr int OUTPUT = 2;
  constexpr int GND = 0;

  MnaSystemSparse mna(NET_COUNT);

  // Voltage divider: VDD --(R1=10k ohm)-- OUTPUT --(R2=10k ohm)-- GND
  ResistorModel::stamp(mna, VDD, OUTPUT, 10e3); // R1
  ResistorModel::stamp(mna, OUTPUT, GND, 10e3); // R2

  // Voltage source: VDD = 5V
  mna.addVoltageSource(VDD, GND, 5.0);

  // Factorize and solve (sparse requires explicit factorize)
  ASSERT_TRUE(mna.factorize());
  auto result = mna.solve();

  // Verify voltage divider: OUTPUT should be ~2.5V (half of 5V)
  EXPECT_NEAR(result.nodeVoltages[OUTPUT], 2.5, 1e-6);
}

/* ----------------------------- Ohm's Law Validation ----------------------------- */

/** @test Verify Ohm's law consistency (V = I * R). */
TEST(ResistorModelTest, OhmsLawConsistency) {
  constexpr double V = 5.0;
  constexpr double R = 1000.0;

  double i = ResistorModel::current(V, R);
  double v_reconstructed = i * R;

  EXPECT_NEAR(v_reconstructed, V, TOLERANCE);
}

/** @test Verify conductance-current relationship (I = G * V). */
TEST(ResistorModelTest, ConductanceCurrentRelationship) {
  constexpr double V = 3.3;
  constexpr double R = 470.0;

  double g = ResistorModel::conductance(R);
  double i = ResistorModel::current(V, R);

  EXPECT_NEAR(i, g * V, TOLERANCE);
}
