/**
 * @file NonlinearDescriptors_uTest.cpp
 * @brief Unit tests for nonlinear device descriptors (diode, BJT, MOSFET).
 */

#include "src/sim/electronics/devices/descriptors/inc/Descriptors.hpp"

#include <gtest/gtest.h>

using sim::electronics::devices::descriptors::BjtDescriptor;
using sim::electronics::devices::descriptors::DiodeDescriptor;
using sim::electronics::devices::descriptors::MosfetDescriptor;
using sim::electronics::mna::NetID;

/* ----------------------------- DiodeDescriptor ----------------------------- */

/** @test */
TEST(DiodeDescriptor, DefaultConstruction) {
  DiodeDescriptor d;
  EXPECT_EQ(d.anodeNet, 0U);
  EXPECT_EQ(d.cathodeNet, 0U);
  EXPECT_DOUBLE_EQ(d.area, 1.0);
}

/** @test */
TEST(DiodeDescriptor, ParameterizedConstruction) {
  const NetID ANODE = 5;
  const NetID CATHODE = 10;
  const double AREA = 2.5;

  DiodeDescriptor d{ANODE, CATHODE, AREA};

  EXPECT_EQ(d.anodeNet, ANODE);
  EXPECT_EQ(d.cathodeNet, CATHODE);
  EXPECT_DOUBLE_EQ(d.area, AREA);
}

/** @test */
TEST(DiodeDescriptor, DefaultArea) {
  DiodeDescriptor d{1, 2};

  EXPECT_EQ(d.anodeNet, 1U);
  EXPECT_EQ(d.cathodeNet, 2U);
  EXPECT_DOUBLE_EQ(d.area, 1.0);
}

/** @test */
TEST(DiodeDescriptor, LargeAreaScaling) {
  DiodeDescriptor d{1, 2, 10.0};
  EXPECT_DOUBLE_EQ(d.area, 10.0);
}

/** @test */
TEST(DiodeDescriptor, SmallAreaScaling) {
  DiodeDescriptor d{1, 2, 0.1};
  EXPECT_DOUBLE_EQ(d.area, 0.1);
}

/* ----------------------------- MosfetDescriptor ----------------------------- */

/** @test */
TEST(MosfetDescriptor, DefaultConstruction) {
  MosfetDescriptor m;
  EXPECT_EQ(m.drainNet, 0U);
  EXPECT_EQ(m.gateNet, 0U);
  EXPECT_EQ(m.sourceNet, 0U);
  EXPECT_EQ(m.bulkNet, 0U);
  EXPECT_DOUBLE_EQ(m.W, 1e-6);
  EXPECT_DOUBLE_EQ(m.L, 1e-6);
}

/** @test */
TEST(MosfetDescriptor, ParameterizedConstruction) {
  const NetID DRAIN = 1;
  const NetID GATE = 2;
  const NetID SOURCE = 3;
  const NetID BULK = 4;
  const double W = 10e-6;
  const double L = 0.5e-6;

  MosfetDescriptor m{DRAIN, GATE, SOURCE, BULK, W, L};

  EXPECT_EQ(m.drainNet, DRAIN);
  EXPECT_EQ(m.gateNet, GATE);
  EXPECT_EQ(m.sourceNet, SOURCE);
  EXPECT_EQ(m.bulkNet, BULK);
  EXPECT_DOUBLE_EQ(m.W, W);
  EXPECT_DOUBLE_EQ(m.L, L);
}

/** @test */
TEST(MosfetDescriptor, PmosConfiguration) {
  const NetID VDD = 5;
  const NetID INPUT = 10;
  const NetID OUTPUT = 15;

  MosfetDescriptor pmos{VDD, INPUT, OUTPUT, VDD, 10e-6, 1e-6};

  EXPECT_EQ(pmos.drainNet, VDD);
  EXPECT_EQ(pmos.gateNet, INPUT);
  EXPECT_EQ(pmos.sourceNet, OUTPUT);
  EXPECT_EQ(pmos.bulkNet, VDD);
}

/** @test */
TEST(MosfetDescriptor, NmosConfiguration) {
  const NetID GND = 0;
  const NetID INPUT = 10;
  const NetID OUTPUT = 15;

  MosfetDescriptor nmos{OUTPUT, INPUT, GND, GND, 10e-6, 1e-6};

  EXPECT_EQ(nmos.drainNet, OUTPUT);
  EXPECT_EQ(nmos.gateNet, INPUT);
  EXPECT_EQ(nmos.sourceNet, GND);
  EXPECT_EQ(nmos.bulkNet, GND);
}

/** @test */
TEST(MosfetDescriptor, MinimumGeometry) {
  MosfetDescriptor m{1, 2, 3, 4, 0.18e-6, 0.18e-6};
  EXPECT_DOUBLE_EQ(m.W, 0.18e-6);
  EXPECT_DOUBLE_EQ(m.L, 0.18e-6);
}

/** @test */
TEST(MosfetDescriptor, LargeGeometry) {
  MosfetDescriptor m{1, 2, 3, 4, 100e-6, 10e-6};
  EXPECT_DOUBLE_EQ(m.W, 100e-6);
  EXPECT_DOUBLE_EQ(m.L, 10e-6);
}

/** @test */
TEST(MosfetDescriptor, AspectRatio) {
  MosfetDescriptor m{1, 2, 3, 4, 20e-6, 2e-6};
  const double aspectRatio = m.W / m.L;
  EXPECT_DOUBLE_EQ(aspectRatio, 10.0);
}

/* ----------------------------- BjtDescriptor ----------------------------- */

/** @test */
TEST(BjtDescriptor, DefaultConstruction) {
  BjtDescriptor q;
  EXPECT_EQ(q.collectorNet, 0U);
  EXPECT_EQ(q.baseNet, 0U);
  EXPECT_EQ(q.emitterNet, 0U);
  EXPECT_DOUBLE_EQ(q.area, 1.0);
}

/** @test */
TEST(BjtDescriptor, ParameterizedConstruction) {
  const NetID COLLECTOR = 5;
  const NetID BASE = 10;
  const NetID EMITTER = 15;
  const double AREA = 3.0;

  BjtDescriptor q{COLLECTOR, BASE, EMITTER, AREA};

  EXPECT_EQ(q.collectorNet, COLLECTOR);
  EXPECT_EQ(q.baseNet, BASE);
  EXPECT_EQ(q.emitterNet, EMITTER);
  EXPECT_DOUBLE_EQ(q.area, AREA);
}

/** @test */
TEST(BjtDescriptor, DefaultArea) {
  BjtDescriptor q{1, 2, 3};

  EXPECT_EQ(q.collectorNet, 1U);
  EXPECT_EQ(q.baseNet, 2U);
  EXPECT_EQ(q.emitterNet, 3U);
  EXPECT_DOUBLE_EQ(q.area, 1.0);
}

/** @test */
TEST(BjtDescriptor, NpnConfiguration) {
  const NetID VDD = 5;
  const NetID BASE = 10;
  const NetID GND = 0;

  BjtDescriptor npn{VDD, BASE, GND, 1.0};

  EXPECT_EQ(npn.collectorNet, VDD);
  EXPECT_EQ(npn.baseNet, BASE);
  EXPECT_EQ(npn.emitterNet, GND);
}

/** @test */
TEST(BjtDescriptor, PnpConfiguration) {
  const NetID VDD = 5;
  const NetID BASE = 10;
  const NetID OUTPUT = 15;

  BjtDescriptor pnp{OUTPUT, BASE, VDD, 1.0};

  EXPECT_EQ(pnp.collectorNet, OUTPUT);
  EXPECT_EQ(pnp.baseNet, BASE);
  EXPECT_EQ(pnp.emitterNet, VDD);
}

/** @test */
TEST(BjtDescriptor, LargeAreaScaling) {
  BjtDescriptor q{1, 2, 3, 5.0};
  EXPECT_DOUBLE_EQ(q.area, 5.0);
}

/** @test */
TEST(BjtDescriptor, SmallAreaScaling) {
  BjtDescriptor q{1, 2, 3, 0.25};
  EXPECT_DOUBLE_EQ(q.area, 0.25);
}
