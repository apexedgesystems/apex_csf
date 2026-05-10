/**
 * @file ComponentClassification_uTest.cpp
 * @brief Unit tests for Intel 4004 component classification.
 *
 * Verifies that all 2242 transistors are classified into the four
 * component types with expected counts.
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Components.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>

using sim::electronics::chips::intel4004::classifyComponents;
using sim::electronics::chips::intel4004::ComponentType;
using sim::electronics::chips::intel4004::Intel4004Grid;
using sim::electronics::chips::intel4004::loadSpiceNetlist;

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = INTEL4004_DATA_DIR "/lajos-4004.spice";
#else
static const std::string SPICE_PATH = "src/sim/electronics/chips/intel4004/netlist/data/lajos-4004.spice";
#endif

/** @test All 2242 transistors are classified (no UNKNOWN). */
TEST(ComponentClassificationTest, AllClassified) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);

  auto result = classifyComponents(grid);

  EXPECT_EQ(result.types.size(), grid.transistors_.size());
  EXPECT_EQ(result.unknownCount(), 0) << "All transistors should be classified";

  std::size_t total =
      result.norGateCount() + result.passGateCount() + result.dynamicCount() + result.loadCount();
  EXPECT_EQ(total, grid.transistors_.size()) << "Counts should sum to total";}

/** @test NOR gate member count roughly matches gate extraction (427 gates x ~4 transistors). */
TEST(ComponentClassificationTest, NorGateCount) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);

  auto result = classifyComponents(grid);

  // 427 gates x ~3 transistors each (1 load + ~2 pull-downs on average) ~ 1200-1500
  EXPECT_GT(result.norGateCount(), 1100) << "Should have substantial NOR gate members";
  EXPECT_LT(result.norGateCount(), 1600) << "Shouldn't classify everything as NOR";
}

/** @test Pass gate count matches gate-level extraction (~133). */
TEST(ComponentClassificationTest, PassGateCount) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);

  auto result = classifyComponents(grid);

  // Should be in the 100-200 range
  EXPECT_GT(result.passGateCount(), 80) << "Should have pass gates";
  EXPECT_LT(result.passGateCount(), 300) << "Too many pass gates";
}

/** @test Dynamic storage nodes exist and are a minority. */
TEST(ComponentClassificationTest, DynamicStorageExists) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);

  auto result = classifyComponents(grid);

  EXPECT_GT(result.dynamicCount(), 0) << "Should have some dynamic storage";
  EXPECT_LT(result.dynamicCount(), 700) << "Dynamic shouldn't dominate";
}

/**
 * @test Schematic-exact counts match the ingested netlist.
 *
 * Anchors the classifier to the ingested netlist:
 *   - Total transistor count: 2242
 *   - Per-component-type counts derived from connectivity analysis
 *
 * If any of these change unexpectedly, either the netlist drifted or the
 * classifier changed. Both warrant investigation.
 */
TEST(ComponentClassificationTest, MatchesIntelSchematic) {
  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004Grid grid;
  auto circuit = grid.buildCircuit(NETLIST);

  auto result = classifyComponents(grid);

  // Total transistor count from the ingested netlist.
  EXPECT_EQ(grid.transistors_.size(), 2242u)
      << "Ingested netlist must contain exactly 2242 transistors";

  // Per-component-type counts derived from the schematic.
  EXPECT_EQ(result.norGateCount(), 1305u)
      << "NOR gate members: 1305 (~58% of 2242)";
  EXPECT_EQ(result.passGateCount(), 222u)
      << "Pass gates: 222";
  EXPECT_EQ(result.dynamicCount(), 610u)
      << "Dynamic storage: 610 (272 NOR-output-gated + 338 latch feedback)";
  EXPECT_EQ(result.loadCount(), 105u)
      << "Standalone loads: 105 (depletion loads not in NOR gates)";

  // All accounted for (no UNKNOWN class)
  EXPECT_EQ(result.unknownCount(), 0u);
}
