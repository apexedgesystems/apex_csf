/**
 * @file PmosDynamicLatch_uTest.cpp
 * @brief Unit tests for PmosDynamicLatchManager.
 *
 * Builds minimal duck-typed stand-ins for the Intel 4004 grid (transistor
 * list, component-type vector, NOR-output set) and exercises the
 * manager's initialization, charge-update integration, MNA stamping,
 * and direct-set helpers without spinning up a full chip simulation.
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/PmosDynamicLatch.hpp"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Components.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <unordered_set>
#include <vector>

using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::chips::intel4004::ComponentType;
using sim::electronics::chips::intel4004::DynamicLatchNode;
using sim::electronics::chips::intel4004::PmosDynamicLatchManager;

namespace {

struct StubTransistor {
  NetID drain;
  NetID gate;
  NetID source;
  bool isDiodeLoad = false;
};

struct StubGrid {
  std::vector<StubTransistor> transistors_;
  NetID vdd_ = 1;
};

constexpr double VDD = 5.0;

/// Build a minimal grid with one DYNAMIC_STORAGE transistor whose gate
/// is a non-NOR-output signal -- triggers latch-node registration.
StubGrid singleStorageGrid(NetID drain, NetID gate, NetID source) {
  StubGrid grid;
  grid.transistors_.push_back({drain, gate, source, /*isDiodeLoad=*/false});
  return grid;
}

/// One DYNAMIC_STORAGE transistor at indices[0] in `types` whose gate
/// is NOT in `norOutputNets`. All others are NOR_GATE_MEMBER (skipped).
struct ScenarioState {
  StubGrid grid;
  std::vector<ComponentType> types;
  std::unordered_set<NetID> norOutputNets;
};

ScenarioState singleStorageScenario(NetID drain, NetID gate, NetID source) {
  ScenarioState s;
  s.grid = singleStorageGrid(drain, gate, source);
  s.types = {ComponentType::DYNAMIC_STORAGE};
  return s;
}

/// Read back the latched voltage the manager holds for `net` by stamping into
/// an MNA system and locating the voltage source whose positive terminal is
/// `net`. NaN if not registered.
double readLatchVoltage(PmosDynamicLatchManager& mgr, std::size_t mnaSize, NetID net) {
  MnaSystem mna(mnaSize);
  mgr.stamp(mna);
  for (const auto& VS : mna.voltageSources()) {
    if (VS.pos == net) return VS.v;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed manager is empty */
TEST(PmosDynamicLatchManagerDefaultTest, DefaultIsEmpty) {
  const PmosDynamicLatchManager MGR;
  EXPECT_EQ(MGR.nodeCount(), 0u);
  EXPECT_DOUBLE_EQ(MGR.evalTime, 25e-9);
}

/** @test Default DynamicLatchNode is precharged to VDD with default cap */
TEST(DynamicLatchNodeDefaultTest, DefaultIsPrechargedAndUnconnected) {
  const DynamicLatchNode N;
  EXPECT_EQ(N.net, 0u);
  EXPECT_DOUBLE_EQ(N.voltage, 5.0);
  EXPECT_DOUBLE_EQ(N.capacitance, 100e-15);
  EXPECT_TRUE(N.transistors.empty());
}

/* ----------------------------- initialize() ----------------------------- */

/** @test initialize() registers latch nodes for non-NOR-gated DYNAMIC_STORAGE */
TEST(PmosDynamicLatchManagerTest, InitializeRegistersDynamicStorageNodes) {
  // drain=2, gate=3, source=4. gate (3) is NOT a NOR output, so registers.
  auto S = singleStorageScenario(/*drain=*/2, /*gate=*/3, /*source=*/4);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, /*classification (unused)=*/0, S.types, S.norOutputNets);

  // Both drain and source nets register (each as a node).
  EXPECT_EQ(mgr.nodeCount(), 2u);
}

/** @test Transistors with gate in norOutputNets are SKIPPED (NOR-gated = L1 path) */
TEST(PmosDynamicLatchManagerTest, InitializeSkipsNorGatedStorage) {
  auto S = singleStorageScenario(/*drain=*/2, /*gate=*/3, /*source=*/4);
  S.norOutputNets.insert(3); // gate is a NOR output -> skip
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);

  EXPECT_EQ(mgr.nodeCount(), 0u);
}

/** @test initialize() skips terminals that hit ground (net 0) or VDD */
TEST(PmosDynamicLatchManagerTest, InitializeSkipsGroundAndVddTerminals) {
  // drain=GND(0), source=VDD(1) -> both terminals filtered, no nodes.
  auto S = singleStorageScenario(/*drain=*/0, /*gate=*/3, /*source=*/1);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);

  EXPECT_EQ(mgr.nodeCount(), 0u);
}

/** @test Transistors classified as something other than DYNAMIC_STORAGE are skipped */
TEST(PmosDynamicLatchManagerTest, InitializeSkipsNonDynamicStorageTypes) {
  StubGrid grid = singleStorageGrid(2, 3, 4);
  std::vector<ComponentType> types = {ComponentType::NOR_GATE_MEMBER};
  std::unordered_set<NetID> norOutputNets;

  PmosDynamicLatchManager mgr;
  mgr.initialize(grid, 0, types, norOutputNets);

  EXPECT_EQ(mgr.nodeCount(), 0u);
}

/** @test initialize() is idempotent -- repeated calls reset state */
TEST(PmosDynamicLatchManagerTest, InitializeIsIdempotent) {
  auto S = singleStorageScenario(2, 3, 4);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);
  EXPECT_EQ(mgr.nodeCount(), 2u);

  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);
  EXPECT_EQ(mgr.nodeCount(), 2u); // not 4: re-init resets
}

/* ----------------------------- updateCharges() ----------------------------- */

/** @test updateCharges leaves node voltage unchanged when the pass gate is OFF.
 *
 * With both drain/source forced to 4.0V and gate held at VDD=5V, Vsg = max(node,
 * other) - gate = 4-5 = -1 <= 0, so the PMOS pass-gate is OFF in the model's
 * formulation (line 169 of PmosDynamicLatch.hpp). iNet = 0 -> dv = 0 -> voltage
 * unchanged.
 */
TEST(PmosDynamicLatchManagerTest, UpdateChargesNoCurrentLeavesVoltageStable) {
  auto S = singleStorageScenario(/*drain=*/2, /*gate=*/3, /*source=*/4);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);

  mgr.forceNodeVoltage(2, 4.0);
  mgr.forceNodeVoltage(4, 4.0);
  std::vector<double> prev(8, VDD); // gate at VDD turns PMOS off
  prev[0] = 0.0;
  mgr.updateCharges(prev, /*dt=*/1e-9, VDD);

  EXPECT_DOUBLE_EQ(readLatchVoltage(mgr, 8, 2), 4.0);
  EXPECT_DOUBLE_EQ(readLatchVoltage(mgr, 8, 4), 4.0);
}

/** @test updateCharges clamps node voltage to VDD when above the rail.
 *
 * Lines 195-196 of PmosDynamicLatch.hpp: after integrating dv the manager
 * clamps node.voltage to [0, vdd]. Forcing a node to VDD+5 and stepping with
 * zero current still triggers the rail clamp on the cached voltage.
 */
TEST(PmosDynamicLatchManagerTest, UpdateChargesClampsToRails) {
  auto S = singleStorageScenario(2, 3, 4);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);

  mgr.forceNodeVoltage(2, /*above-rail*/ VDD + 5.0);
  std::vector<double> prev(8, VDD); // gate at VDD -> PMOS off, no current
  prev[0] = 0.0;
  mgr.updateCharges(prev, 1e-9, VDD);

  EXPECT_DOUBLE_EQ(readLatchVoltage(mgr, 8, 2), VDD);
}

/** @test updateCharges limits |dv| per step to 0.5V even under unphysically large
 *        I*dt/C contributions.
 *
 * Lines 189-190 of PmosDynamicLatch.hpp clamp dv to [-0.5, 0.5]. With node at
 * 2.0V, the other terminal at 0V, gate at 0V (strong Vsg = 2), and dt=1e-3 with
 * the default 100 fF cap, the raw step would be enormous; the limiter caps it
 * at 0.5V/step. Direction: PMOS conducts source->drain, node 2 is the high
 * terminal so current LEAVES it (line 179), so voltage steps DOWN by 0.5V.
 */
TEST(PmosDynamicLatchManagerTest, UpdateChargesLimitsPerStepDelta) {
  auto S = singleStorageScenario(/*drain=*/2, /*gate=*/3, /*source=*/4);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);

  mgr.forceNodeVoltage(2, 2.0);
  mgr.forceNodeVoltage(4, 0.0);
  std::vector<double> prev(8, 0.0);
  prev[1] = VDD;
  prev[3] = 0.0; // gate
  prev[4] = 0.0; // source = the other terminal
  mgr.updateCharges(prev, /*dt=*/1e-3, VDD); // unphysically large dt

  // Without the limiter, dv would be huge -> clamped to rail. With the
  // limiter, |dv| <= 0.5V, so node 2 falls to 2.0 - 0.5 = 1.5V.
  EXPECT_NEAR(readLatchVoltage(mgr, 8, 2), 1.5, 1e-9);
}

/* ----------------------------- forceNodeVoltage() + stamp() ----------------------------- */

/** @test forceNodeVoltage on an unknown net is a no-op */
TEST(PmosDynamicLatchManagerTest, ForceNodeVoltageUnknownNetIsNoOp) {
  auto S = singleStorageScenario(2, 3, 4);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);

  // Force a net that wasn't registered -- should not crash, should not add a node.
  mgr.forceNodeVoltage(99, 1.5);
  EXPECT_EQ(mgr.nodeCount(), 2u);
}

/** @test stamp() adds one voltage source per registered latch node */
TEST(PmosDynamicLatchManagerTest, StampAddsVoltageSourcePerNode) {
  auto S = singleStorageScenario(2, 3, 4);
  PmosDynamicLatchManager mgr;
  mgr.initialize(S.grid, 0, S.types, S.norOutputNets);

  MnaSystem mna(/*netCount=*/8);
  EXPECT_EQ(mna.voltageSourceCount(), 0u);
  mgr.stamp(mna);
  EXPECT_EQ(mna.voltageSourceCount(), 2u);
}

/* ----------------------------- Determinism ----------------------------- */

/** @test Repeated initialize+nodeCount give the same answer */
TEST(PmosDynamicLatchManagerDeterminismTest, NodeCountIsRepeatable) {
  auto S = singleStorageScenario(2, 3, 4);
  PmosDynamicLatchManager mgr;
  for (int i = 0; i < 10; ++i) {
    mgr.initialize(S.grid, 0, S.types, S.norOutputNets);
    EXPECT_EQ(mgr.nodeCount(), 2u);
  }
}
