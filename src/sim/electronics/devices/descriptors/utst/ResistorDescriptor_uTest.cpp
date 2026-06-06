/**
 * @file ResistorDescriptor_uTest.cpp
 * @brief Unit tests for ResistorDescriptor.
 */

#include "src/sim/electronics/devices/descriptors/inc/ResistorDescriptor.hpp"

#include <gtest/gtest.h>

using sim::electronics::devices::descriptors::ResistorDescriptor;

/* ----------------------------- Construction ----------------------------- */

/** @test Default construction with designated initializers. */
TEST(ResistorDescriptorTest, DesignatedInitializers) {
  ResistorDescriptor r{.posNet = 1, .negNet = 0, .resistance = 1000.0};

  EXPECT_EQ(r.posNet, 1u);
  EXPECT_EQ(r.negNet, 0u);
  EXPECT_DOUBLE_EQ(r.resistance, 1000.0);
}

/** @test Positional construction. */
TEST(ResistorDescriptorTest, PositionalConstruction) {
  ResistorDescriptor r{5, 2, 4700.0};

  EXPECT_EQ(r.posNet, 5u);
  EXPECT_EQ(r.negNet, 2u);
  EXPECT_DOUBLE_EQ(r.resistance, 4700.0);
}

/** @test Zero resistance (short circuit). */
TEST(ResistorDescriptorTest, ZeroResistance) {
  ResistorDescriptor r{1, 0, 0.0};

  EXPECT_EQ(r.posNet, 1u);
  EXPECT_EQ(r.negNet, 0u);
  EXPECT_DOUBLE_EQ(r.resistance, 0.0);
}

/** @test Large resistance values. */
TEST(ResistorDescriptorTest, LargeResistance) {
  ResistorDescriptor r{3, 0, 1e6}; // 1 M ohm

  EXPECT_DOUBLE_EQ(r.resistance, 1e6);
}

/* ----------------------------- Standard Values ----------------------------- */

/** @test E12 series resistor values (common). */
TEST(ResistorDescriptorTest, E12Series) {
  ResistorDescriptor r10{1, 0, 10.0};
  [[maybe_unused]] ResistorDescriptor r12{1, 0, 12.0};
  [[maybe_unused]] ResistorDescriptor r15{1, 0, 15.0};
  [[maybe_unused]] ResistorDescriptor r18{1, 0, 18.0};
  [[maybe_unused]] ResistorDescriptor r22{1, 0, 22.0};
  [[maybe_unused]] ResistorDescriptor r27{1, 0, 27.0};
  [[maybe_unused]] ResistorDescriptor r33{1, 0, 33.0};
  [[maybe_unused]] ResistorDescriptor r39{1, 0, 39.0};
  ResistorDescriptor r47{1, 0, 47.0};
  [[maybe_unused]] ResistorDescriptor r56{1, 0, 56.0};
  [[maybe_unused]] ResistorDescriptor r68{1, 0, 68.0};
  ResistorDescriptor r82{1, 0, 82.0};

  EXPECT_DOUBLE_EQ(r10.resistance, 10.0);
  EXPECT_DOUBLE_EQ(r47.resistance, 47.0);
  EXPECT_DOUBLE_EQ(r82.resistance, 82.0);
}

/** @test Common decade multiples. */
TEST(ResistorDescriptorTest, CommonValues) {
  ResistorDescriptor r100{1, 0, 100.0};  // 100 ohm
  ResistorDescriptor r1k{1, 0, 1e3};     // 1k ohm
  ResistorDescriptor r10k{1, 0, 10e3};   // 10k ohm
  ResistorDescriptor r100k{1, 0, 100e3}; // 100k ohm
  ResistorDescriptor r1M{1, 0, 1e6};     // 1M ohm

  EXPECT_DOUBLE_EQ(r100.resistance, 100.0);
  EXPECT_DOUBLE_EQ(r1k.resistance, 1000.0);
  EXPECT_DOUBLE_EQ(r10k.resistance, 10000.0);
  EXPECT_DOUBLE_EQ(r100k.resistance, 100000.0);
  EXPECT_DOUBLE_EQ(r1M.resistance, 1e6);
}

/* ----------------------------- Topology ----------------------------- */

/** @test Same net (degenerate case). */
TEST(ResistorDescriptorTest, SameNet) {
  ResistorDescriptor r{5, 5, 1000.0};

  EXPECT_EQ(r.posNet, r.negNet);
  EXPECT_EQ(r.posNet, 5u);
}

/** @test Ground referenced. */
TEST(ResistorDescriptorTest, GroundReferenced) {
  ResistorDescriptor r{10, 0, 10e3};

  EXPECT_EQ(r.posNet, 10u);
  EXPECT_EQ(r.negNet, 0u);
}

/** @test Floating (neither terminal at ground). */
TEST(ResistorDescriptorTest, Floating) {
  ResistorDescriptor r{7, 3, 2200.0};

  EXPECT_EQ(r.posNet, 7u);
  EXPECT_EQ(r.negNet, 3u);
  EXPECT_NE(r.posNet, 0u);
  EXPECT_NE(r.negNet, 0u);
}
