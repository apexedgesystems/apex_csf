/**
 * @file CapacitorDescriptor_uTest.cpp
 * @brief Unit tests for CapacitorDescriptor.
 */

#include "src/sim/electronics/devices/descriptors/inc/CapacitorDescriptor.hpp"

#include <gtest/gtest.h>

using sim::electronics::devices::descriptors::CapacitorDescriptor;

/* ----------------------------- Construction ----------------------------- */

/** @test Default construction with designated initializers. */
TEST(CapacitorDescriptor, DesignatedInitializers) {
  CapacitorDescriptor c{.posNet = 3, .negNet = 0, .capacitance = 1e-6};

  EXPECT_EQ(c.posNet, 3u);
  EXPECT_EQ(c.negNet, 0u);
  EXPECT_DOUBLE_EQ(c.capacitance, 1e-6);
}

/** @test Positional construction. */
TEST(CapacitorDescriptor, PositionalConstruction) {
  CapacitorDescriptor c{5, 2, 100e-12};

  EXPECT_EQ(c.posNet, 5u);
  EXPECT_EQ(c.negNet, 2u);
  EXPECT_DOUBLE_EQ(c.capacitance, 100e-12);
}

/* ----------------------------- Standard Values ----------------------------- */

/** @test Picofarad range (typical coupling/parasitic). */
TEST(CapacitorDescriptor, PicofaradRange) {
  CapacitorDescriptor c1{1, 0, 1e-12};     // 1pF
  CapacitorDescriptor c10{1, 0, 10e-12};   // 10pF
  CapacitorDescriptor c100{1, 0, 100e-12}; // 100pF

  EXPECT_DOUBLE_EQ(c1.capacitance, 1e-12);
  EXPECT_DOUBLE_EQ(c10.capacitance, 10e-12);
  EXPECT_DOUBLE_EQ(c100.capacitance, 100e-12);
}

/** @test Nanofarad range (typical decoupling). */
TEST(CapacitorDescriptor, NanofaradRange) {
  CapacitorDescriptor c1{1, 0, 1e-9};     // 1nF
  CapacitorDescriptor c10{1, 0, 10e-9};   // 10nF
  CapacitorDescriptor c100{1, 0, 100e-9}; // 100nF (0.1uF)

  EXPECT_DOUBLE_EQ(c1.capacitance, 1e-9);
  EXPECT_DOUBLE_EQ(c10.capacitance, 10e-9);
  EXPECT_DOUBLE_EQ(c100.capacitance, 100e-9);
}

/** @test Microfarad range (typical bulk/electrolytic). */
TEST(CapacitorDescriptor, MicrofaradRange) {
  CapacitorDescriptor c1{1, 0, 1e-6};     // 1uF
  CapacitorDescriptor c10{1, 0, 10e-6};   // 10uF
  CapacitorDescriptor c100{1, 0, 100e-6}; // 100uF

  EXPECT_DOUBLE_EQ(c1.capacitance, 1e-6);
  EXPECT_DOUBLE_EQ(c10.capacitance, 10e-6);
  EXPECT_DOUBLE_EQ(c100.capacitance, 100e-6);
}

/** @test Millifarad range (supercapacitors). */
TEST(CapacitorDescriptor, MillifaradRange) {
  CapacitorDescriptor c{1, 0, 1e-3}; // 1mF

  EXPECT_DOUBLE_EQ(c.capacitance, 1e-3);
}

/* ----------------------------- Topology ----------------------------- */

/** @test Ground referenced (common). */
TEST(CapacitorDescriptor, GroundReferenced) {
  CapacitorDescriptor c{10, 0, 10e-6};

  EXPECT_EQ(c.posNet, 10u);
  EXPECT_EQ(c.negNet, 0u);
}

/** @test Floating (AC coupling). */
TEST(CapacitorDescriptor, Floating) {
  CapacitorDescriptor c{7, 3, 1e-6};

  EXPECT_EQ(c.posNet, 7u);
  EXPECT_EQ(c.negNet, 3u);
  EXPECT_NE(c.posNet, 0u);
  EXPECT_NE(c.negNet, 0u);
}

/** @test Same net (degenerate, but valid topology). */
TEST(CapacitorDescriptor, SameNet) {
  CapacitorDescriptor c{5, 5, 100e-12};

  EXPECT_EQ(c.posNet, c.negNet);
}
