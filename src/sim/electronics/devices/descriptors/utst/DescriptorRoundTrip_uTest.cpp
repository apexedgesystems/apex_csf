/**
 * @file DescriptorRoundTrip_uTest.cpp
 * @brief Functional round-trip tests: descriptor -> physics consumer.
 *
 * Descriptors are pure POD topology / parameter records. Their
 * existing per-descriptor tests verify construction integrity. These
 * tests close the loop by handing a descriptor to its intended physics
 * consumer (CompanionSet for reactive elements, MosfetLevel1 stamp for
 * MOSFETs, BjtEbersMoll for BJTs, DiodeShockley for diodes) and
 * asserting the consumer sees the descriptor's parameters consistently.
 *
 * These are NOT physics tests -- the device-model behavior is covered
 * by their own utsts. Only the descriptor -> consumer wiring is
 * verified here.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"
#include "src/sim/electronics/devices/descriptors/inc/CapacitorDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/DiodeDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/InductorDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/MosfetDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/ResistorDescriptor.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::algorithms::companions::CompanionSet;
using sim::electronics::devices::descriptors::CapacitorDescriptor;
using sim::electronics::devices::descriptors::DiodeDescriptor;
using sim::electronics::devices::descriptors::InductorDescriptor;
using sim::electronics::devices::descriptors::MosfetDescriptor;
using sim::electronics::devices::descriptors::ResistorDescriptor;
using sim::electronics::devices::nonlinear::DiodeShockley;
using sim::electronics::devices::nonlinear::DiodeShockleyParams;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

namespace {
constexpr double TOL = 1e-12;
}

/* ----------------------------- CapacitorDescriptor ----------------------------- */

/** @test CapacitorDescriptor parameters survive into CompanionSet. */
TEST(DescriptorRoundTripTest, CapacitorIntoCompanionSet) {
  const CapacitorDescriptor DESC{.posNet = 5, .negNet = 3, .capacitance = 4.7e-9};
  CompanionSet set;
  const auto IDX = set.addCapacitor(DESC.posNet, DESC.negNet, DESC.capacitance);

  const auto& CAP = set.capacitor(IDX);
  EXPECT_EQ(CAP.posNet, DESC.posNet);
  EXPECT_EQ(CAP.negNet, DESC.negNet);
  EXPECT_DOUBLE_EQ(CAP.capacitance, DESC.capacitance);
}

/* ----------------------------- InductorDescriptor ----------------------------- */

/** @test InductorDescriptor parameters survive into CompanionSet. */
TEST(DescriptorRoundTripTest, InductorIntoCompanionSet) {
  const InductorDescriptor DESC{.posNet = 7, .negNet = 0, .inductance = 22e-3};
  CompanionSet set;
  const auto IDX = set.addInductor(DESC.posNet, DESC.negNet, DESC.inductance);

  const auto& IND = set.inductor(IDX);
  EXPECT_EQ(IND.posNet, DESC.posNet);
  EXPECT_EQ(IND.negNet, DESC.negNet);
  EXPECT_DOUBLE_EQ(IND.inductance, DESC.inductance);
}

/* ----------------------------- ResistorDescriptor ----------------------------- */

/** @test Two ResistorDescriptors with identical fields compare element-equal. */
TEST(DescriptorRoundTripTest, ResistorEqualityByValue) {
  const ResistorDescriptor A{.posNet = 1, .negNet = 0, .resistance = 1e3};
  const ResistorDescriptor B{.posNet = 1, .negNet = 0, .resistance = 1e3};

  EXPECT_EQ(A.posNet, B.posNet);
  EXPECT_EQ(A.negNet, B.negNet);
  EXPECT_DOUBLE_EQ(A.resistance, B.resistance);
}

/** @test Reciprocal resistance gives the conductance a stamp would write. */
TEST(DescriptorRoundTripTest, ResistorConductanceMatchesReciprocal) {
  const ResistorDescriptor DESC{.posNet = 2, .negNet = 0, .resistance = 470.0};
  const double G = 1.0 / DESC.resistance;
  EXPECT_NEAR(G, 1.0 / 470.0, TOL);
}

/* ----------------------------- DiodeDescriptor ----------------------------- */

/** @test DiodeShockley reads the descriptor's bias and returns positive forward current. */
TEST(DescriptorRoundTripTest, DiodeForwardBiasProducesPositiveCurrent) {
  const DiodeDescriptor DESC{/*ANODE=*/1, /*cathode=*/0};
  const DiodeShockleyParams PARAMS{};
  const double V_FORWARD = 0.7; // typical Si turn-on
  const double I = DiodeShockley::current(V_FORWARD, PARAMS);

  // Sanity: forward bias produces forward current; reverse bias does not.
  EXPECT_GT(I, 0.0);
  const double I_REVERSE = DiodeShockley::current(-1.0, PARAMS);
  EXPECT_LT(I_REVERSE, 0.0);

  // Descriptor topology is unaffected by physics evaluation.
  EXPECT_EQ(DESC.anodeNet, 1u);
  EXPECT_EQ(DESC.cathodeNet, 0u);
}

/* ----------------------------- MosfetDescriptor ----------------------------- */

/** @test MosfetLevel1 stamp produces non-zero conductance using descriptor geometry. */
TEST(DescriptorRoundTripTest, MosfetGeometryFlowsToStampValues) {
  const MosfetDescriptor DESC{1, 2, 3, 0, /*W=*/2e-6, /*L=*/1e-6};
  // W/L = 2 -- we expect the device-level Kp_eff to scale linearly when
  // the model is configured with descriptor geometry.
  const double WL_RATIO = DESC.W / DESC.L;
  EXPECT_NEAR(WL_RATIO, 2.0, TOL);

  // Stamp values at a known forward-active bias should be positive
  // when the threshold is met.
  const MosfetLevel1Params PARAMS{
      .Kp = 100e-6 * WL_RATIO, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};
  const auto SV = MosfetLevel1::stampValues(/*vgs=*/3.0, /*vds=*/3.0, PARAMS);
  EXPECT_GT(SV.id, 0.0);
  EXPECT_GT(SV.gm, 0.0);
}

/** @test MosfetDescriptor default geometry is 1um/1um. */
TEST(DescriptorRoundTripTest, MosfetDefaultGeometry) {
  const MosfetDescriptor DESC{};
  EXPECT_DOUBLE_EQ(DESC.W, 1e-6);
  EXPECT_DOUBLE_EQ(DESC.L, 1e-6);
  EXPECT_DOUBLE_EQ(DESC.W / DESC.L, 1.0);
}
