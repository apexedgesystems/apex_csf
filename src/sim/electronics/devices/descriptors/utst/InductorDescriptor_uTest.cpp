/**
 * @file InductorDescriptor_uTest.cpp
 * @brief Unit tests for InductorDescriptor.
 */

#include "src/sim/electronics/devices/descriptors/inc/InductorDescriptor.hpp"

#include <gtest/gtest.h>

using sim::electronics::devices::descriptors::InductorDescriptor;

/* ----------------------------- Construction ----------------------------- */

/** @test Default construction with designated initializers. */
TEST(InductorDescriptor, DesignatedInitializers) {
  InductorDescriptor l{.posNet = 2, .negNet = 1, .inductance = 1e-3};

  EXPECT_EQ(l.posNet, 2u);
  EXPECT_EQ(l.negNet, 1u);
  EXPECT_DOUBLE_EQ(l.inductance, 1e-3);
}

/** @test Positional construction. */
TEST(InductorDescriptor, PositionalConstruction) {
  InductorDescriptor l{5, 0, 100e-6};

  EXPECT_EQ(l.posNet, 5u);
  EXPECT_EQ(l.negNet, 0u);
  EXPECT_DOUBLE_EQ(l.inductance, 100e-6);
}

/* ----------------------------- Standard Values ----------------------------- */

/** @test Nanohenry range (PCB traces, chip inductors). */
TEST(InductorDescriptor, NanohenryRange) {
  InductorDescriptor l1{1, 0, 1e-9};     // 1nH
  InductorDescriptor l10{1, 0, 10e-9};   // 10nH
  InductorDescriptor l100{1, 0, 100e-9}; // 100nH

  EXPECT_DOUBLE_EQ(l1.inductance, 1e-9);
  EXPECT_DOUBLE_EQ(l10.inductance, 10e-9);
  EXPECT_DOUBLE_EQ(l100.inductance, 100e-9);
}

/** @test Microhenry range (typical filter/choke). */
TEST(InductorDescriptor, MicrohenryRange) {
  InductorDescriptor l1{1, 0, 1e-6};     // 1uH
  InductorDescriptor l10{1, 0, 10e-6};   // 10uH
  InductorDescriptor l100{1, 0, 100e-6}; // 100uH

  EXPECT_DOUBLE_EQ(l1.inductance, 1e-6);
  EXPECT_DOUBLE_EQ(l10.inductance, 10e-6);
  EXPECT_DOUBLE_EQ(l100.inductance, 100e-6);
}

/** @test Millihenry range (power inductors). */
TEST(InductorDescriptor, MillihenryRange) {
  InductorDescriptor l1{1, 0, 1e-3};     // 1mH
  InductorDescriptor l10{1, 0, 10e-3};   // 10mH
  InductorDescriptor l100{1, 0, 100e-3}; // 100mH

  EXPECT_DOUBLE_EQ(l1.inductance, 1e-3);
  EXPECT_DOUBLE_EQ(l10.inductance, 10e-3);
  EXPECT_DOUBLE_EQ(l100.inductance, 100e-3);
}

/** @test Henry range (large transformers). */
TEST(InductorDescriptor, HenryRange) {
  InductorDescriptor l{1, 0, 1.0}; // 1H

  EXPECT_DOUBLE_EQ(l.inductance, 1.0);
}

/* ----------------------------- Topology ----------------------------- */

/** @test Ground referenced. */
TEST(InductorDescriptor, GroundReferenced) {
  InductorDescriptor l{10, 0, 10e-6};

  EXPECT_EQ(l.posNet, 10u);
  EXPECT_EQ(l.negNet, 0u);
}

/** @test Floating (series in path). */
TEST(InductorDescriptor, Floating) {
  InductorDescriptor l{7, 3, 1e-6};

  EXPECT_EQ(l.posNet, 7u);
  EXPECT_EQ(l.negNet, 3u);
  EXPECT_NE(l.posNet, 0u);
  EXPECT_NE(l.negNet, 0u);
}

/** @test Same net (degenerate, but valid topology). */
TEST(InductorDescriptor, SameNet) {
  InductorDescriptor l{5, 5, 100e-6};

  EXPECT_EQ(l.posNet, l.negNet);
}
