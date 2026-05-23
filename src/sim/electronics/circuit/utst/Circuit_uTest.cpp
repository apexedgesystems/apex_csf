/**
 * @file Circuit_uTest.cpp
 * @brief Unit tests for the Circuit construction and simulation API.
 *
 * Covers net allocation, stamp registration, companion management,
 * build lifecycle, solver access, and transient simulation using
 * a simple RC circuit with known analytical response.
 */

#include "src/sim/electronics/circuit/inc/Circuit.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"
#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::algorithms::transient::IntegrationMethod;
using sim::electronics::algorithms::transient::TransientConfig;
using sim::electronics::algorithms::transient::TransientResult;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;
using sim::electronics::circuit::Circuit;
using sim::electronics::circuit::CircuitNet;
using sim::electronics::devices::linear::ResistorModel;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed circuit has one net (ground) and no stamps. */
TEST(CircuitTest, DefaultConstruction) {
  Circuit ckt;
  EXPECT_EQ(ckt.netCount(), 1u) << "Fresh circuit has ground (net 0) only";
  EXPECT_EQ(ckt.stampCount(), 0u);
  EXPECT_FALSE(ckt.isBuilt());
}

/* ----------------------------- Net Allocation ----------------------------- */

/** @test addNet returns incrementing IDs starting at 1. */
TEST(CircuitTest, AddNetReturnsIncrementingIds) {
  Circuit ckt;
  CircuitNet n1 = ckt.addNet();
  CircuitNet n2 = ckt.addNet();
  CircuitNet n3 = ckt.addNet();
  EXPECT_EQ(n1.id, 1u);
  EXPECT_EQ(n2.id, 2u);
  EXPECT_EQ(n3.id, 3u);
}

/** @test addNet with name stores the name and returns incrementing IDs. */
TEST(CircuitTest, AddNetWithNameStoresName) {
  Circuit ckt;
  CircuitNet vcc = ckt.addNet("VCC");
  CircuitNet out = ckt.addNet("OUT");
  EXPECT_EQ(vcc.id, 1u);
  EXPECT_EQ(out.id, 2u);
  EXPECT_EQ(ckt.netName(vcc.id), "VCC");
  EXPECT_EQ(ckt.netName(out.id), "OUT");
}

/** @test ground always returns 0. */
TEST(CircuitTest, GroundReturnsZero) { EXPECT_EQ(Circuit::ground(), 0u); }

/** @test netCount tracks total nets including ground. */
TEST(CircuitTest, NetCountIncludesGround) {
  Circuit ckt;
  EXPECT_EQ(ckt.netCount(), 1u);
  ckt.addNet();
  EXPECT_EQ(ckt.netCount(), 2u);
  ckt.addNet("named");
  EXPECT_EQ(ckt.netCount(), 3u);
}

/** @test netName returns empty for unnamed nets. */
TEST(CircuitTest, NetNameEmptyForUnnamed) {
  Circuit ckt;
  ckt.addNet(); // unnamed, id=1
  EXPECT_TRUE(ckt.netName(1).empty());
}

/** @test netName returns empty for out-of-range IDs. */
TEST(CircuitTest, NetNameEmptyForOutOfRange) {
  Circuit ckt;
  EXPECT_TRUE(ckt.netName(999).empty());
}

/* ----------------------------- Stamp Registration ----------------------------- */

/** @test addStamp increments stamp count. */
TEST(CircuitTest, AddStampIncrementsCount) {
  Circuit ckt;
  EXPECT_EQ(ckt.stampCount(), 0u);

  ckt.addStamp([](MnaSystem&, double, const std::vector<double>&) {});
  EXPECT_EQ(ckt.stampCount(), 1u);

  ckt.addStamp([](MnaSystem&, double, const std::vector<double>&) {});
  EXPECT_EQ(ckt.stampCount(), 2u);
}

/* ----------------------------- Companion Management ----------------------------- */

/** @test addCapacitor adds to companion set and returns index. */
TEST(CircuitTest, AddCapacitorReturnsIndex) {
  Circuit ckt;
  CircuitNet a = ckt.addNet();
  std::size_t idx0 = ckt.addCapacitor(a.id, Circuit::ground(), 1e-6);
  std::size_t idx1 = ckt.addCapacitor(a.id, Circuit::ground(), 2e-6);
  EXPECT_EQ(idx0, 0u);
  EXPECT_EQ(idx1, 1u);
}

/** @test addInductor adds to companion set and returns index. */
TEST(CircuitTest, AddInductorReturnsIndex) {
  Circuit ckt;
  CircuitNet a = ckt.addNet();
  std::size_t idx0 = ckt.addInductor(a.id, Circuit::ground(), 1e-3);
  std::size_t idx1 = ckt.addInductor(a.id, Circuit::ground(), 2e-3);
  EXPECT_EQ(idx0, 0u);
  EXPECT_EQ(idx1, 1u);
}

/** @test companions() returns mutable and const references. */
TEST(CircuitTest, CompanionsAccessors) {
  Circuit ckt;
  CircuitNet a = ckt.addNet();
  ckt.addCapacitor(a.id, Circuit::ground(), 1e-6);

  // Mutable access
  auto& mutableSet = ckt.companions();
  EXPECT_EQ(mutableSet.capacitorCount(), 1u);

  // Const access
  const Circuit& CONST_CKT = ckt;
  const auto& CONST_SET = CONST_CKT.companions();
  EXPECT_EQ(CONST_SET.capacitorCount(), 1u);
}

/* ----------------------------- Build Lifecycle ----------------------------- */

/** @test isBuilt is false before build, true after. */
TEST(CircuitTest, IsBuiltLifecycle) {
  Circuit ckt;
  ckt.addNet();
  EXPECT_FALSE(ckt.isBuilt());

  ckt.build();
  EXPECT_TRUE(ckt.isBuilt());
}

/** @test Calling build multiple times rebuilds the solver. */
TEST(CircuitTest, BuildMultipleTimes) {
  Circuit ckt;
  ckt.addNet();
  ckt.build();
  EXPECT_TRUE(ckt.isBuilt());

  ckt.build();
  EXPECT_TRUE(ckt.isBuilt()) << "Rebuilding should keep isBuilt true";
}

/* ----------------------------- Reset ----------------------------- */

/** @test resetSolver clears built state. */
TEST(CircuitTest, ResetSolverClearsBuiltState) {
  Circuit ckt;
  ckt.addNet();
  ckt.build();
  EXPECT_TRUE(ckt.isBuilt());

  ckt.resetSolver();
  EXPECT_FALSE(ckt.isBuilt());
}

/** @test resetSolver preserves nets, stamps, and companions. */
TEST(CircuitTest, ResetSolverPreservesCircuitDefinition) {
  Circuit ckt;
  CircuitNet a = ckt.addNet("A");
  ckt.addStamp([](MnaSystem&, double, const std::vector<double>&) {});
  ckt.addCapacitor(a.id, Circuit::ground(), 1e-6);
  ckt.build();

  ckt.resetSolver();
  EXPECT_EQ(ckt.netCount(), 2u);
  EXPECT_EQ(ckt.stampCount(), 1u);
  EXPECT_EQ(ckt.netName(a.id), "A");
  EXPECT_EQ(ckt.companions().capacitorCount(), 1u);
}

/* ----------------------------- Solver Access ----------------------------- */

/** @test solver() auto-builds if not already built. */
TEST(CircuitTest, SolverAutoBuild) {
  Circuit ckt;
  ckt.addNet();
  EXPECT_FALSE(ckt.isBuilt());

  [[maybe_unused]] auto& s = ckt.solver();
  EXPECT_TRUE(ckt.isBuilt());
}

/* ----------------------------- DC Operating Point ----------------------------- */

/** @test computeDC solves a resistive voltage divider. */
TEST(CircuitTest, ComputeDcVoltageDivider) {
  // 5V source -> R1=1k -> node A -> R2=1k -> ground
  // Expected V(A) = 2.5V
  Circuit ckt;
  CircuitNet nodeA = ckt.addNet("A");

  const double R1 = 1e3;
  const double R2 = 1e3;
  const double VIN = 5.0;

  ckt.addStamp([nodeA, R2, VIN](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeA.id, Circuit::ground(), VIN);
    ResistorModel::stamp(mna, nodeA.id, Circuit::ground(), R2);
  });

  // For a voltage divider with a single internal node, we need two nodes.
  // Simpler approach: vsrc to node_in, R1 from node_in to nodeA, R2 from nodeA to gnd.
  Circuit ckt2;
  CircuitNet nodeIn = ckt2.addNet("IN");
  CircuitNet nodeOut = ckt2.addNet("OUT");

  ckt2.addStamp([nodeIn, nodeOut, R1, R2, VIN](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeIn.id, Circuit::ground(), VIN);
    ResistorModel::stamp(mna, nodeIn.id, nodeOut.id, R1);
    ResistorModel::stamp(mna, nodeOut.id, Circuit::ground(), R2);
  });

  TransientState state;
  state.resize(ckt2.netCount(), 1);

  TransientStatus status = ckt2.computeDC(state);
  EXPECT_EQ(status, TransientStatus::SUCCESS);
  EXPECT_NEAR(state.nodeVoltages[nodeOut.id], 2.5, 0.01)
      << "Voltage divider midpoint should be V_in/2";
  EXPECT_NEAR(state.nodeVoltages[nodeIn.id], 5.0, 0.01) << "Input node should be at V_in";
}

/* ----------------------------- Single Step ----------------------------- */

/** @test step advances simulation by one time step. */
TEST(CircuitTest, StepSingleTimeStep) {
  // Simple RC: Vsrc=5V -> R=1k -> node_out -> C=1uF -> gnd
  // tau = R*C = 1ms
  Circuit ckt;
  CircuitNet nodeIn = ckt.addNet("IN");
  CircuitNet nodeOut = ckt.addNet("OUT");

  const double R = 1e3;
  const double VIN = 5.0;

  ckt.addStamp([nodeIn, nodeOut, R, VIN](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeIn.id, Circuit::ground(), VIN);
    ResistorModel::stamp(mna, nodeIn.id, nodeOut.id, R);
  });
  ckt.addCapacitor(nodeOut.id, Circuit::ground(), 1e-6);
  ckt.build();

  TransientState state;
  state.resize(ckt.netCount(), 1);

  // DC first to initialize
  TransientStatus dcStatus = ckt.computeDC(state);
  EXPECT_EQ(dcStatus, TransientStatus::SUCCESS);

  // Single step
  const double DT = 10e-6;
  TransientStatus stepStatus = ckt.step(DT, state);
  EXPECT_EQ(stepStatus, TransientStatus::SUCCESS);
  EXPECT_GT(state.time, 0.0) << "Time should have advanced after step";
}

/* ----------------------------- Transient Simulation ----------------------------- */

/** @test simulate runs a full RC transient and converges to steady state. */
TEST(CircuitTest, SimulateRcConvergesToSteadyState) {
  // RC circuit: Vsrc=5V -> R=1k -> node_out -> C=1uF -> gnd
  // tau = 1ms. After 5*tau = 5ms, V_out should be ~5V.
  Circuit ckt;
  CircuitNet nodeIn = ckt.addNet("IN");
  CircuitNet nodeOut = ckt.addNet("OUT");

  const double R = 1e3;
  const double C = 1e-6;
  const double VIN = 5.0;

  ckt.addStamp([nodeIn, nodeOut, R, VIN](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeIn.id, Circuit::ground(), VIN);
    ResistorModel::stamp(mna, nodeIn.id, nodeOut.id, R);
  });
  ckt.addCapacitor(nodeOut.id, Circuit::ground(), C);

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5e-3; // 5*tau
  config.tStep = 10e-6;
  config.method = IntegrationMethod::BACKWARD_EULER;
  config.dcOpPoint = true;

  TransientResult result = ckt.simulate(config, false);
  EXPECT_TRUE(result.success) << "Simulation should complete: " << result.errorMessage;
  EXPECT_TRUE(ckt.isBuilt()) << "simulate should auto-build";
  EXPECT_GT(result.stepsTaken, 0u);

  // After 5*tau the capacitor should be nearly fully charged
  double vOut = result.finalState.nodeVoltages[nodeOut.id];
  EXPECT_NEAR(vOut, VIN, 0.15) << "After 5*tau, V_out should be ~V_in, got " << vOut;
}

/** @test simulate auto-builds if not already built. */
TEST(CircuitTest, SimulateAutoBuild) {
  Circuit ckt;
  CircuitNet nodeIn = ckt.addNet("IN");
  CircuitNet nodeOut = ckt.addNet("OUT");

  ckt.addStamp([nodeIn, nodeOut](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeIn.id, Circuit::ground(), 1.0);
    ResistorModel::stamp(mna, nodeIn.id, nodeOut.id, 1e3);
    ResistorModel::stamp(mna, nodeOut.id, Circuit::ground(), 1e3);
  });

  EXPECT_FALSE(ckt.isBuilt());

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1e-6;
  config.tStep = 1e-9;

  TransientResult result = ckt.simulate(config, false);
  EXPECT_TRUE(ckt.isBuilt());
  EXPECT_TRUE(result.success);
}

/** @test simulate with history records all time points. */
TEST(CircuitTest, SimulateWithHistory) {
  Circuit ckt;
  CircuitNet nodeIn = ckt.addNet("IN");

  ckt.addStamp([nodeIn](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeIn.id, Circuit::ground(), 3.3);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 100e-9;
  config.tStep = 10e-9;
  config.dcOpPoint = false;

  TransientResult result = ckt.simulate(config, true);
  EXPECT_TRUE(result.success);
  EXPECT_FALSE(result.history.empty()) << "History should be recorded when requested";
}

/* ----------------------------- Step Auto-Build ----------------------------- */

/** @test step auto-builds if not already built. */
TEST(CircuitTest, StepAutoBuild) {
  Circuit ckt;
  CircuitNet nodeIn = ckt.addNet("IN");

  ckt.addStamp([nodeIn](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeIn.id, Circuit::ground(), 1.0);
    ResistorModel::stamp(mna, nodeIn.id, Circuit::ground(), 1e3);
  });

  EXPECT_FALSE(ckt.isBuilt());

  TransientState state;
  state.resize(ckt.netCount(), 1);
  TransientStatus status = ckt.step(1e-9, state);
  EXPECT_TRUE(ckt.isBuilt());
  EXPECT_EQ(status, TransientStatus::SUCCESS);
}

/* ----------------------------- ComputeDC Auto-Build ----------------------------- */

/** @test computeDC auto-builds if not already built. */
TEST(CircuitTest, ComputeDcAutoBuild) {
  Circuit ckt;
  CircuitNet nodeIn = ckt.addNet("IN");

  ckt.addStamp([nodeIn](MnaSystem& mna, double, const std::vector<double>&) {
    mna.addVoltageSource(nodeIn.id, Circuit::ground(), 2.0);
    ResistorModel::stamp(mna, nodeIn.id, Circuit::ground(), 1e3);
  });

  EXPECT_FALSE(ckt.isBuilt());

  TransientState state;
  state.resize(ckt.netCount(), 1);
  TransientStatus status = ckt.computeDC(state);
  EXPECT_TRUE(ckt.isBuilt());
  EXPECT_EQ(status, TransientStatus::SUCCESS);
  EXPECT_NEAR(state.nodeVoltages[nodeIn.id], 2.0, 0.01);
}

/* ----------------------------- Stamp Count ----------------------------- */

/** @test stampCount returns zero for fresh circuit. */
TEST(CircuitTest, StampCountZeroInitially) {
  Circuit ckt;
  EXPECT_EQ(ckt.stampCount(), 0u);
}
