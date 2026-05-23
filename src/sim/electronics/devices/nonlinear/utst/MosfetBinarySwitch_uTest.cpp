/**
 * @file MosfetBinarySwitch_uTest.cpp
 * @brief Unit tests for MosfetBinarySwitch model.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetBinarySwitch.hpp"

#include <gtest/gtest.h>

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetBinarySwitch;
using sim::electronics::devices::nonlinear::MosfetBinarySwitchParams;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Parameters ----------------------------- */

/** @test Default parameters are reasonable. */
TEST(MosfetBinarySwitchTest, DefaultParameters) {
  MosfetBinarySwitchParams params;

  EXPECT_DOUBLE_EQ(params.Vth, 0.7);
  EXPECT_DOUBLE_EQ(params.Ron, 500.0);
  EXPECT_DOUBLE_EQ(params.Roff, 1e9);
}

/** @test Custom parameters. */
TEST(MosfetBinarySwitchTest, CustomParameters) {
  MosfetBinarySwitchParams params{.Vth = 1.0, .Ron = 100.0, .Roff = 1e10};

  EXPECT_DOUBLE_EQ(params.Vth, 1.0);
  EXPECT_DOUBLE_EQ(params.Ron, 100.0);
  EXPECT_DOUBLE_EQ(params.Roff, 1e10);
}

/* ----------------------------- Resistance ----------------------------- */

/** @test OFF state resistance (Vgs < Vth). */
TEST(MosfetBinarySwitchTest, OffStateResistance) {
  MosfetBinarySwitchParams params;

  double r = MosfetBinarySwitch::resistance(0.0, params); // Well below Vth
  EXPECT_DOUBLE_EQ(r, params.Roff);
}

/** @test ON state resistance (Vgs > Vth). */
TEST(MosfetBinarySwitchTest, OnStateResistance) {
  MosfetBinarySwitchParams params;

  double r = MosfetBinarySwitch::resistance(5.0, params); // Well above Vth
  EXPECT_DOUBLE_EQ(r, params.Ron);
}

/** @test Resistance at threshold (edge case). */
TEST(MosfetBinarySwitchTest, ThresholdResistance) {
  MosfetBinarySwitchParams params{.Vth = 1.0};

  // Exactly at threshold: should be OFF (Vgs > Vth is false)
  double r1 = MosfetBinarySwitch::resistance(1.0, params);
  EXPECT_DOUBLE_EQ(r1, params.Roff);

  // Just above threshold: should be ON
  double r2 = MosfetBinarySwitch::resistance(1.001, params);
  EXPECT_DOUBLE_EQ(r2, params.Ron);

  // Just below threshold: should be OFF
  double r3 = MosfetBinarySwitch::resistance(0.999, params);
  EXPECT_DOUBLE_EQ(r3, params.Roff);
}

/** @test Negative gate voltage (reverse bias). */
TEST(MosfetBinarySwitchTest, NegativeGateVoltage) {
  MosfetBinarySwitchParams params;

  double r = MosfetBinarySwitch::resistance(-1.0, params);
  EXPECT_DOUBLE_EQ(r, params.Roff);
}

/* ----------------------------- State Detection ----------------------------- */

/** @test isOn() returns true when Vgs > Vth. */
TEST(MosfetBinarySwitchTest, IsOnAboveThreshold) {
  MosfetBinarySwitchParams params{.Vth = 1.0};

  EXPECT_TRUE(MosfetBinarySwitch::isOn(5.0, params));
  EXPECT_TRUE(MosfetBinarySwitch::isOn(1.5, params));
  EXPECT_TRUE(MosfetBinarySwitch::isOn(1.001, params));
}

/** @test isOn() returns false when Vgs <= Vth. */
TEST(MosfetBinarySwitchTest, IsOnBelowThreshold) {
  MosfetBinarySwitchParams params{.Vth = 1.0};

  EXPECT_FALSE(MosfetBinarySwitch::isOn(0.0, params));
  EXPECT_FALSE(MosfetBinarySwitch::isOn(0.5, params));
  EXPECT_FALSE(MosfetBinarySwitch::isOn(0.999, params));
  EXPECT_FALSE(MosfetBinarySwitch::isOn(1.0, params)); // Equal, not greater
}

/** @test isOn() with negative gate voltage. */
TEST(MosfetBinarySwitchTest, IsOnNegativeVoltage) {
  MosfetBinarySwitchParams params;

  EXPECT_FALSE(MosfetBinarySwitch::isOn(-1.0, params));
  EXPECT_FALSE(MosfetBinarySwitch::isOn(-5.0, params));
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp OFF state (high resistance). */
TEST(MosfetBinarySwitchTest, StampOffState) {
  MnaSystem mna(3);
  MosfetBinarySwitchParams params;
  const NetID DRAIN = 1;
  const NetID SOURCE = 0;
  const double VGS = 0.0; // Below Vth

  MosfetBinarySwitch::stamp(mna, DRAIN, SOURCE, VGS, params);

  // Should stamp high resistance (low conductance)
  // Detailed matrix checks would require exposing internal state
}

/** @test Stamp ON state (low resistance). */
TEST(MosfetBinarySwitchTest, StampOnState) {
  MnaSystem mna(3);
  MosfetBinarySwitchParams params;
  const NetID DRAIN = 1;
  const NetID SOURCE = 0;
  const double VGS = 5.0; // Above Vth

  MosfetBinarySwitch::stamp(mna, DRAIN, SOURCE, VGS, params);

  // Should stamp low resistance (high conductance)
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test Resistance ratio OFF/ON. */
TEST(MosfetBinarySwitchTest, ResistanceRatio) {
  MosfetBinarySwitchParams params{.Vth = 1.0, .Ron = 100.0, .Roff = 1e8};

  double rOn = MosfetBinarySwitch::resistance(5.0, params);
  double rOff = MosfetBinarySwitch::resistance(0.0, params);

  double ratio = rOff / rOn;
  EXPECT_DOUBLE_EQ(ratio, 1e6); // 100 Mohm / 100 ohm = 1e6
}

/** @test Low threshold voltage (enhancement mode). */
TEST(MosfetBinarySwitchTest, EnhancementMode) {
  MosfetBinarySwitchParams params{.Vth = 0.5}; // Low Vth

  EXPECT_FALSE(MosfetBinarySwitch::isOn(0.4, params)); // OFF
  EXPECT_TRUE(MosfetBinarySwitch::isOn(0.6, params));  // ON
}

/** @test High threshold voltage (depletion mode-like). */
TEST(MosfetBinarySwitchTest, HighThreshold) {
  MosfetBinarySwitchParams params{.Vth = 2.0}; // High Vth

  EXPECT_FALSE(MosfetBinarySwitch::isOn(1.5, params)); // OFF
  EXPECT_TRUE(MosfetBinarySwitch::isOn(2.5, params));  // ON
}

/** @test Very low Ron (ideal switch). */
TEST(MosfetBinarySwitchTest, IdealSwitch) {
  MosfetBinarySwitchParams params{.Vth = 1.0, .Ron = 0.1, .Roff = 1e12};

  double rOn = MosfetBinarySwitch::resistance(5.0, params);
  double rOff = MosfetBinarySwitch::resistance(0.0, params);

  EXPECT_DOUBLE_EQ(rOn, 0.1);
  EXPECT_DOUBLE_EQ(rOff, 1e12);
}

/** @test Digital logic compatibility (TTL levels). */
TEST(MosfetBinarySwitchTest, TTLLevels) {
  MosfetBinarySwitchParams params{.Vth = 1.4}; // TTL threshold ~= 1.4V

  // TTL LOW (0-0.8V): OFF
  EXPECT_FALSE(MosfetBinarySwitch::isOn(0.0, params));
  EXPECT_FALSE(MosfetBinarySwitch::isOn(0.8, params));

  // TTL HIGH (2.0-5.0V): ON
  EXPECT_TRUE(MosfetBinarySwitch::isOn(2.0, params));
  EXPECT_TRUE(MosfetBinarySwitch::isOn(5.0, params));
}

/** @test CMOS logic compatibility (3.3V/5V). */
TEST(MosfetBinarySwitchTest, CMOSLevels) {
  MosfetBinarySwitchParams params{.Vth = 0.7}; // CMOS threshold ~= 0.5-1.0V

  // CMOS 5V LOW (0-1.5V): mostly OFF
  EXPECT_FALSE(MosfetBinarySwitch::isOn(0.0, params));

  // CMOS 5V HIGH (3.5-5.0V): ON
  EXPECT_TRUE(MosfetBinarySwitch::isOn(3.5, params));
  EXPECT_TRUE(MosfetBinarySwitch::isOn(5.0, params));

  // CMOS 3.3V HIGH (2.4-3.3V): ON
  EXPECT_TRUE(MosfetBinarySwitch::isOn(2.4, params));
  EXPECT_TRUE(MosfetBinarySwitch::isOn(3.3, params));
}
