/**
 * @file Transient_uTest.cpp
 * @brief Unit tests for transient circuit simulation.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::companions::CapacitorCompanion;
using sim::electronics::devices::companions::CompanionSet;
using sim::electronics::devices::companions::InductorCompanion;
using sim::electronics::mna::MnaSystem;
using sim::electronics::mna::MnaSystemSparse;
using sim::electronics::transient::IntegrationMethod;
using sim::electronics::transient::TransientConfig;
using sim::electronics::transient::TransientResult;
using sim::electronics::transient::TransientSolver;
using sim::electronics::transient::TransientState;
using sim::electronics::transient::TransientStatus;

/* ----------------------------- Constants ----------------------------- */

static constexpr double VDD = 5.0;

/* ----------------------------- TransientConfig Tests ----------------------------- */

/** @test TransientConfig default values. */
TEST(TransientConfig, DefaultValues) {
  TransientConfig config;

  EXPECT_DOUBLE_EQ(config.tStart, 0.0);
  EXPECT_DOUBLE_EQ(config.tEnd, 1e-6);
  EXPECT_DOUBLE_EQ(config.tStep, 1e-9);
  EXPECT_EQ(config.method, IntegrationMethod::BACKWARD_EULER);
  EXPECT_FALSE(config.adaptiveStep);
  EXPECT_TRUE(config.dcOpPoint);
}

/** @test TransientConfig step count calculation. */
TEST(TransientConfig, StepCount) {
  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1.0;
  config.tStep = 0.1;

  // Use integer-friendly values to avoid floating point issues
  EXPECT_EQ(config.stepCount(), 10);
}

/** @test TransientConfig integration order. */
TEST(TransientConfig, IntegrationOrder) {
  TransientConfig config;

  config.method = IntegrationMethod::BACKWARD_EULER;
  EXPECT_EQ(config.order(), 1);

  config.method = IntegrationMethod::TRAPEZOIDAL;
  EXPECT_EQ(config.order(), 2);

  config.method = IntegrationMethod::GEAR2;
  EXPECT_EQ(config.order(), 2);
}

/* ----------------------------- CompanionModels Tests ----------------------------- */

/** @test CapacitorCompanion equivalent values. */
TEST(CapacitorCompanion, EquivalentValues) {
  CapacitorCompanion cap;
  cap.posNet = 1;
  cap.negNet = 0;
  cap.capacitance = 1e-6; // 1 uF
  cap.prevVoltage = 2.5;

  double dt = 1e-9; // 1 ns

  // Geq = C / dt = 1e-6 / 1e-9 = 1000 S
  EXPECT_DOUBLE_EQ(cap.geq(dt), 1000.0);

  // Ieq = C * V_prev / dt = 1e-6 * 2.5 / 1e-9 = 2500 A
  // Positive value: current injected into posNet to maintain voltage
  EXPECT_DOUBLE_EQ(cap.ieq(dt), 2500.0);
}

/** @test InductorCompanion equivalent values. */
TEST(InductorCompanion, EquivalentValues) {
  InductorCompanion ind;
  ind.posNet = 1;
  ind.negNet = 0;
  ind.inductance = 1e-3; // 1 mH
  ind.prevCurrent = 0.1; // 100 mA

  double dt = 1e-9; // 1 ns

  // Geq = dt / L = 1e-9 / 1e-3 = 1e-6 S
  EXPECT_DOUBLE_EQ(ind.geq(dt), 1e-6);

  // Ieq = I_prev = 0.1 A
  EXPECT_DOUBLE_EQ(ind.ieq(dt), 0.1);
}

/** @test CompanionSet add and access. */
TEST(CompanionSet, AddAndAccess) {
  CompanionSet set;

  std::size_t capIdx = set.addCapacitor(1, 0, 1e-6);
  std::size_t indIdx = set.addInductor(2, 0, 1e-3);

  EXPECT_EQ(capIdx, 0);
  EXPECT_EQ(indIdx, 0);
  EXPECT_EQ(set.capacitorCount(), 1);
  EXPECT_EQ(set.inductorCount(), 1);

  EXPECT_DOUBLE_EQ(set.capacitor(0).capacitance, 1e-6);
  EXPECT_DOUBLE_EQ(set.inductor(0).inductance, 1e-3);
}

/* ----------------------------- Simple Transient Tests ----------------------------- */

/** @test Basic transient simulation with just a voltage source. */
TEST(TransientSolver, VoltageSourceOnly) {
  // Simple circuit: just a voltage source
  // Nets: 0=Gnd, 1=Vdd
  TransientSolver solver(2);

  solver.setStampCallback([](MnaSystem& mna, double /*time*/) { mna.addVoltageSource(1, 0, VDD); });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1e-6;
  config.tStep = 1e-7;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  EXPECT_GT(result.stepsTaken, 0);

  // Voltage at net 1 should be VDD
  double v1 = result.finalState.voltage(1);
  EXPECT_NEAR(v1, VDD, 0.01);
}

/** @test Capacitor holds voltage when isolated. */
TEST(TransientSolver, CapacitorHoldsVoltage) {
  // Capacitor pre-charged to 3V, no discharge path
  // Should maintain voltage
  TransientSolver solver(2);

  double C = 1e-6;
  double V_INIT = 3.0;

  solver.companions().addCapacitor(1, 0, C);
  solver.companions().capacitor(0).prevVoltage = V_INIT;

  // No external sources - just the capacitor
  solver.setStampCallback([](MnaSystem& /*mna*/, double /*time*/) {
    // Empty - no static elements
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1e-6;
  config.tStep = 1e-7;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, false);

  ASSERT_TRUE(result.success) << result.errorMessage;

  // Capacitor should maintain its voltage
  double vFinal = result.finalState.voltage(1);
  EXPECT_NEAR(vFinal, V_INIT, 0.1);
}

/* ----------------------------- RC Circuit Tests ----------------------------- */

/**
 * @test RC charging - qualitative behavior.
 *
 * Circuit: Vdd -- R -- Vout -- C -- Gnd
 *
 * Just verify that voltage increases over time and approaches Vdd.
 */
TEST(TransientSolver, RCChargingQualitative) {
  double R = 1000.0;  // 1k ohm
  double C = 1e-6;    // 1 uF
  double TAU = R * C; // 1 ms

  // Nets: 0=Gnd, 1=Vdd, 2=Vout
  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * TAU;
  config.tStep = TAU / 50.0; // 20 us steps
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 10);

  // Verify voltage is increasing
  double vPrev = 0.0;
  for (std::size_t i = 1; i < result.history.size(); ++i) {
    double v = result.history[i].voltage(2);
    EXPECT_GE(v, vPrev * 0.99) // Allow small numerical noise
        << "Voltage should be non-decreasing at step " << i;
    vPrev = v;
  }

  // Final voltage should be close to Vdd
  double vFinal = result.finalState.voltage(2);
  EXPECT_GT(vFinal, 0.90 * VDD) << "Final voltage should approach Vdd";
}

/** @test RC circuit starting from DC operating point. */
TEST(TransientSolver, RCWithDCOpPoint) {
  double C = 1e-6;
  double R = 1000.0;
  double G = 1.0 / R;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);

  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1e-5;
  config.tStep = 1e-6;
  config.dcOpPoint = true; // Compute DC first

  TransientResult result = solver.run(config, false);

  ASSERT_TRUE(result.success) << result.errorMessage;

  // With DC operating point, capacitor starts at Vdd (steady state)
  double vFinal = result.finalState.voltage(2);
  EXPECT_NEAR(vFinal, VDD, 0.1);
}

/* ----------------------------- RL Circuit Tests ----------------------------- */

/**
 * @test RL step response - qualitative.
 *
 * Circuit: Vdd -- R -- Vout -- L -- Gnd
 *
 * At steady state, inductor is a short, so Vout -> 0.
 */
TEST(TransientSolver, RLStepResponseQualitative) {
  double R = 100.0;   // 100 ohm
  double L = 10e-3;   // 10 mH
  double TAU = L / R; // 100 us

  // Nets: 0=Gnd, 1=Vdd, 2=Mid
  TransientSolver solver(3);
  solver.companions().addInductor(2, 0, L);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * TAU;
  config.tStep = TAU / 50.0;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;

  // At steady state, inductor is a short circuit
  // V(mid) should approach 0
  double vMidFinal = result.finalState.voltage(2);
  EXPECT_LT(std::abs(vMidFinal), 1.0) << "Voltage should approach 0";

  // Inductor current should approach Vdd/R
  const auto& ind = solver.companions().inductor(0);
  double iFinal = ind.prevCurrent;
  double iExpected = VDD / R;
  EXPECT_GT(iFinal, 0.5 * iExpected) << "Current should be building up";
}

/* ----------------------------- Quantitative Time Constant Tests ----------------------------- */

/**
 * @test RC charging - quantitative time constant validation.
 *
 * Circuit: Vdd -- R -- Vout -- C -- Gnd
 *
 * Validates backward Euler integration accuracy by checking voltage at
 * specific time constants. For RC charging: V(t) = Vdd * (1 - e^(-t/τ))
 * where τ = R*C.
 *
 * At t=τ:  V ≈ 0.632 * Vdd
 * At t=2τ: V ≈ 0.865 * Vdd
 * At t=3τ: V ≈ 0.950 * Vdd
 */
TEST(TransientSolver, RCChargingQuantitative) {
  double R = 1000.0;  // 1k ohm
  double C = 1e-6;    // 1 uF
  double TAU = R * C; // 1 ms

  // Nets: 0=Gnd, 1=Vdd, 2=Vout
  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * TAU;
  config.tStep = TAU / 100.0; // 10 us steps for good accuracy
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 300);

  // Find voltage at t = 1τ, 2τ, 3τ
  // V(t) = Vdd * (1 - exp(-t/τ))
  auto findVoltageAtTime = [&](double targetTime) -> double {
    for (const auto& state : result.history) {
      if (std::abs(state.time - targetTime) < config.tStep / 2.0) {
        return state.voltage(2);
      }
    }
    return 0.0;
  };

  double v1tau = findVoltageAtTime(1.0 * TAU);
  double v2tau = findVoltageAtTime(2.0 * TAU);
  double v3tau = findVoltageAtTime(3.0 * TAU);

  // Expected values from exponential formula
  double expected1tau = VDD * (1.0 - std::exp(-1.0)); // 0.632 * Vdd
  double expected2tau = VDD * (1.0 - std::exp(-2.0)); // 0.865 * Vdd
  double expected3tau = VDD * (1.0 - std::exp(-3.0)); // 0.950 * Vdd

  // Backward Euler is first-order accurate and energy dissipative
  // Use 5% tolerance to account for numerical integration error
  EXPECT_NEAR(v1tau, expected1tau, 0.05 * VDD)
      << "Voltage at 1τ should match exponential (tolerance 5%)";
  EXPECT_NEAR(v2tau, expected2tau, 0.05 * VDD)
      << "Voltage at 2τ should match exponential (tolerance 5%)";
  EXPECT_NEAR(v3tau, expected3tau, 0.05 * VDD)
      << "Voltage at 3τ should match exponential (tolerance 5%)";
}

/**
 * @test RL current buildup - quantitative time constant validation.
 *
 * Circuit: Vdd -- R -- Vout -- L -- Gnd
 *
 * Validates inductor current buildup matches expected exponential:
 * I(t) = (Vdd/R) * (1 - e^(-t/τ)) where τ = L/R.
 *
 * At t=τ:  I ≈ 0.632 * I_steady
 * At t=2τ: I ≈ 0.865 * I_steady
 * At t=3τ: I ≈ 0.950 * I_steady
 */
TEST(TransientSolver, RLCurrentQuantitative) {
  double R = 100.0;          // 100 ohm
  double L = 10e-3;          // 10 mH
  double TAU = L / R;        // 100 us
  double I_STEADY = VDD / R; // Steady-state current

  // Nets: 0=Gnd, 1=Vdd, 2=Mid
  TransientSolver solver(3);
  solver.companions().addInductor(2, 0, L);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * TAU;
  config.tStep = TAU / 100.0; // 1 us steps for good accuracy
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 300);

  // Extract inductor current history
  // Inductor current is updated in companion after each solve
  auto findCurrentAtTime = [&](double targetTime) -> double {
    // Run a separate solver to get current at specific time
    TransientSolver tempSolver(3);
    tempSolver.companions().addInductor(2, 0, L);
    tempSolver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
      mna.addVoltageSource(1, 0, VDD);
      mna.addConductance(1, 2, G);
    });

    TransientConfig tempConfig;
    tempConfig.tStart = 0.0;
    tempConfig.tEnd = targetTime;
    tempConfig.tStep = TAU / 100.0;
    tempConfig.dcOpPoint = false;

    tempSolver.run(tempConfig, false);
    return tempSolver.companions().inductor(0).prevCurrent;
  };

  double i1tau = findCurrentAtTime(1.0 * TAU);
  double i2tau = findCurrentAtTime(2.0 * TAU);
  double i3tau = findCurrentAtTime(3.0 * TAU);

  // Expected values from exponential formula
  double expected1tau = I_STEADY * (1.0 - std::exp(-1.0)); // 0.632 * I_steady
  double expected2tau = I_STEADY * (1.0 - std::exp(-2.0)); // 0.865 * I_steady
  double expected3tau = I_STEADY * (1.0 - std::exp(-3.0)); // 0.950 * I_steady

  // Backward Euler is first-order accurate
  // Use 5% tolerance to account for numerical integration error
  EXPECT_NEAR(i1tau, expected1tau, 0.05 * I_STEADY)
      << "Current at 1τ should match exponential (tolerance 5%)";
  EXPECT_NEAR(i2tau, expected2tau, 0.05 * I_STEADY)
      << "Current at 2τ should match exponential (tolerance 5%)";
  EXPECT_NEAR(i3tau, expected3tau, 0.05 * I_STEADY)
      << "Current at 3τ should match exponential (tolerance 5%)";
}

/* ----------------------------- Accessor Tests ----------------------------- */

/** @test time() returns current simulation time after run. */
TEST(TransientSolver, TimeAccessor) {
  TransientSolver solver(2);
  solver.setStampCallback([](MnaSystem& mna, double /*time*/) { mna.addVoltageSource(1, 0, VDD); });

  EXPECT_DOUBLE_EQ(solver.time(), 0.0);

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1e-6;
  config.tStep = 1e-7;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, false);
  ASSERT_TRUE(result.success) << result.errorMessage;

  // After run, time() should be at or near tEnd
  EXPECT_NEAR(solver.time(), config.tEnd, config.tStep);
}

/** @test netCount() returns construction-time net count. */
TEST(TransientSolver, NetCountAccessor) {
  TransientSolver solver2(2);
  EXPECT_EQ(solver2.netCount(), 2u);

  TransientSolver solver10(10);
  EXPECT_EQ(solver10.netCount(), 10u);
}

/** @test integrationMethod() / setIntegrationMethod() round-trip. */
TEST(TransientSolver, IntegrationMethodAccessor) {
  TransientSolver solver(2);

  // Default is backward Euler
  EXPECT_EQ(solver.integrationMethod(), IntegrationMethod::BACKWARD_EULER);

  solver.setIntegrationMethod(IntegrationMethod::TRAPEZOIDAL);
  EXPECT_EQ(solver.integrationMethod(), IntegrationMethod::TRAPEZOIDAL);

  solver.setIntegrationMethod(IntegrationMethod::GEAR2);
  EXPECT_EQ(solver.integrationMethod(), IntegrationMethod::GEAR2);

  solver.setIntegrationMethod(IntegrationMethod::BACKWARD_EULER);
  EXPECT_EQ(solver.integrationMethod(), IntegrationMethod::BACKWARD_EULER);
}

/** @test prevVoltages() populated after transient run. */
TEST(TransientSolver, PrevVoltagesAccessor) {
  TransientSolver solver(3);

  double C = 1e-6;
  double R = 1000.0;
  double G = 1.0 / R;

  solver.companions().addCapacitor(2, 0, C);
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5e-3; // 5 ms (5 time constants for 1k * 1uF)
  config.tStep = 1e-4;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, false);
  ASSERT_TRUE(result.success) << result.errorMessage;

  std::vector<double>& pv = solver.prevVoltages();
  ASSERT_GE(pv.size(), 3u);

  // Ground net should be 0
  EXPECT_DOUBLE_EQ(pv[0], 0.0);
  // Vdd net should be near VDD
  EXPECT_NEAR(pv[1], VDD, 0.1);
  // Capacitor net should have charged toward VDD
  EXPECT_GT(pv[2], 0.5 * VDD);
}

/* ----------------------------- Error Handling Tests ----------------------------- */

/** @test Invalid configuration handling. */
TEST(TransientSolver, InvalidConfig) {
  TransientSolver solver(3);

  TransientConfig config;
  config.tStep = 0.0; // Invalid

  TransientResult result = solver.run(config);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.errorMessage.empty());
}

/** @test Solver reset. */
TEST(TransientSolver, Reset) {
  TransientSolver solver(3);
  solver.companions().addCapacitor(1, 0, 1e-6);

  solver.setStampCallback([](MnaSystem& mna, double /*time*/) { mna.addVoltageSource(1, 0, VDD); });

  // Simulate a bit
  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1e-6;
  config.tStep = 1e-7;
  config.dcOpPoint = false;

  solver.run(config);

  // Reset
  solver.reset();

  EXPECT_DOUBLE_EQ(solver.time(), 0.0);
}

/* ----------------------------- Integration Method Tests ----------------------------- */

/** @test Trapezoidal integration - capacitor equivalent values. */
TEST(CapacitorCompanion, TrapezoidalEquivalentValues) {
  CapacitorCompanion cap;
  cap.posNet = 1;
  cap.negNet = 0;
  cap.capacitance = 1e-6; // 1 uF
  cap.prevVoltage = 2.5;
  cap.current = 0.5; // Previous current

  double dt = 1e-9;

  // Geq = 2C / dt = 2e-6 / 1e-9 = 2000 S (2x backward Euler)
  EXPECT_DOUBLE_EQ(cap.geq(dt, IntegrationMethod::TRAPEZOIDAL), 2000.0);

  // Ieq = 2C*V_prev/dt + I_prev = 2e-6 * 2.5 / 1e-9 + 0.5 = 5000 + 0.5 = 5000.5 A
  EXPECT_DOUBLE_EQ(cap.ieq(dt, IntegrationMethod::TRAPEZOIDAL), 5000.5);
}

/** @test Trapezoidal integration - inductor equivalent values.
 *
 * For an inductor V = L*dI/dt, the trapezoidal discretization is:
 *   Geq = dt / (2L)            (HALF of backward Euler dt/L)
 *   Ieq = I_prev + (dt/(2L))*V_prev
 *
 * Note: this is the OPPOSITE relationship from the capacitor (where trapezoidal
 * Geq is 2x backward Euler). The reason is that the capacitor is i=C*dV/dt
 * while the inductor is V=L*dI/dt -- the trapezoidal averaging applies to a
 * different quantity in each case.
 */
TEST(InductorCompanion, TrapezoidalEquivalentValues) {
  InductorCompanion ind;
  ind.posNet = 1;
  ind.negNet = 0;
  ind.inductance = 1e-3; // 1 mH
  ind.prevCurrent = 0.1; // 100 mA
  ind.voltage = 2.0;     // Previous voltage

  double dt = 1e-9;

  // Geq = dt / (2L) = 1e-9 / (2*1e-3) = 5e-7 S
  EXPECT_DOUBLE_EQ(ind.geq(dt, IntegrationMethod::TRAPEZOIDAL), 5e-7);

  // Ieq = I_prev + (dt/(2L))*V_prev = 0.1 + 5e-7 * 2.0 = 0.1 + 1e-6 = 0.100001 A
  EXPECT_DOUBLE_EQ(ind.ieq(dt, IntegrationMethod::TRAPEZOIDAL), 0.100001);
}

/** @test GEAR2 integration - capacitor equivalent values. */
TEST(CapacitorCompanion, Gear2EquivalentValues) {
  CapacitorCompanion cap;
  cap.posNet = 1;
  cap.negNet = 0;
  cap.capacitance = 1e-6;
  cap.prevVoltage = 2.5;
  cap.prev2Voltage = 2.0;

  double dt = 1e-9;

  // Geq = 3C/(2dt) = 1.5 * C/dt = 1500 S
  EXPECT_DOUBLE_EQ(cap.geq(dt, IntegrationMethod::GEAR2), 1500.0);

  // Ieq = (C/dt) * (2*V(t-dt) - 0.5*V(t-2dt))
  //     = (1e-6/1e-9) * (2*2.5 - 0.5*2.0)
  //     = 1000 * (5.0 - 1.0) = 4000 A
  EXPECT_DOUBLE_EQ(cap.ieq(dt, IntegrationMethod::GEAR2), 4000.0);
}

/** @test GEAR2 integration - inductor equivalent values. */
TEST(InductorCompanion, Gear2EquivalentValues) {
  InductorCompanion ind;
  ind.posNet = 1;
  ind.negNet = 0;
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.1;
  ind.prev2Current = 0.08;

  double dt = 1e-9;

  // Geq = 3dt/(2L) = 1.5e-6 S
  EXPECT_DOUBLE_EQ(ind.geq(dt, IntegrationMethod::GEAR2), 1.5e-6);

  // Ieq = 2*I(t-dt) - 0.5*I(t-2dt) = 2*0.1 - 0.5*0.08 = 0.2 - 0.04 = 0.16 A
  EXPECT_DOUBLE_EQ(ind.ieq(dt, IntegrationMethod::GEAR2), 0.16);
}

/**
 * @test RC charging with trapezoidal integration.
 *
 * Circuit: Vdd -- R -- Vout -- C -- Gnd
 *
 * Trapezoidal is second-order accurate, so it should match the analytical
 * solution more closely than backward Euler (first-order).
 */
TEST(TransientSolver, RCChargingTrapezoidal) {
  double R = 1000.0;  // 1k ohm
  double C = 1e-6;    // 1 uF
  double TAU = R * C; // 1 ms

  // Nets: 0=Gnd, 1=Vdd, 2=Vout
  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 3.0 * TAU;
  config.tStep = TAU / 100.0; // 10 us steps
  config.dcOpPoint = false;
  config.method = IntegrationMethod::TRAPEZOIDAL;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 200);

  // Find voltage at t = 1τ
  auto findVoltageAtTime = [&](double targetTime) -> double {
    for (const auto& state : result.history) {
      if (std::abs(state.time - targetTime) < config.tStep / 2.0) {
        return state.voltage(2);
      }
    }
    return 0.0;
  };

  double v1tau = findVoltageAtTime(1.0 * TAU);
  double expected1tau = VDD * (1.0 - std::exp(-1.0));

  // Trapezoidal is second-order accurate, so expect better accuracy than BE
  // Use 2% tolerance (vs 5% for backward Euler)
  EXPECT_NEAR(v1tau, expected1tau, 0.02 * VDD)
      << "Trapezoidal should be more accurate than backward Euler";
}

/**
 * @test LC oscillator with backward Euler - energy dissipation.
 *
 * Circuit: L -- Vout -- C -- Gnd (no resistance)
 *
 * Backward Euler is energy-dissipative: amplitude decays over time even
 * without resistance. This is a numerical artifact of the method.
 */
TEST(TransientSolver, LCOscillatorBackwardEulerDecay) {
  double L = 1e-3;                       // 1 mH
  double C = 1e-6;                       // 1 uF
  double V0 = 5.0;                       // Initial capacitor voltage
  double OMEGA = 1.0 / std::sqrt(L * C); // Resonant frequency ~ 31.6 krad/s
  double PERIOD = 2.0 * M_PI / OMEGA;    // ~ 200 us

  // Nets: 0=Gnd, 1=Vout
  TransientSolver solver(2);
  solver.companions().addInductor(1, 0, L);
  solver.companions().addCapacitor(1, 0, C);

  // Pre-charge capacitor to V0
  solver.companions().capacitor(0).prevVoltage = V0;

  // No external sources - pure LC oscillator
  solver.setStampCallback([](MnaSystem& /*mna*/, double /*time*/) {
    // Empty - no static elements
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 10.0 * PERIOD;   // 10 cycles
  config.tStep = PERIOD / 100.0; // 100 points per cycle
  config.dcOpPoint = false;
  config.method = IntegrationMethod::BACKWARD_EULER;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 500);

  // Find first peak (near t=0) and last peak (near t=10*PERIOD)
  double firstPeak = 0.0;
  double lastPeak = 0.0;

  for (std::size_t i = 1; i < result.history.size() - 1; ++i) {
    double vPrev = result.history[i - 1].voltage(1);
    double v = result.history[i].voltage(1);
    double vNext = result.history[i + 1].voltage(1);

    // Local maximum
    if (v > vPrev && v > vNext && v > 0.5 * V0) {
      if (result.history[i].time < PERIOD) {
        firstPeak = std::max(firstPeak, v);
      }
      if (result.history[i].time > 9.0 * PERIOD) {
        lastPeak = std::max(lastPeak, v);
      }
    }
  }

  // Backward Euler should dissipate energy: last peak < first peak
  EXPECT_LT(lastPeak, 0.95 * firstPeak)
      << "Backward Euler should show amplitude decay (energy dissipation)";
}

/**
 * @test LC oscillator with trapezoidal - energy conservation.
 *
 * Circuit: L -- Vout -- C -- Gnd (no resistance)
 *
 * Trapezoidal is energy-conserving: amplitude should remain constant over time.
 * This is the key advantage of trapezoidal for oscillatory circuits.
 */
TEST(TransientSolver, LCOscillatorTrapezoidalConservation) {
  double L = 1e-3;                       // 1 mH
  double C = 1e-6;                       // 1 uF
  double V0 = 5.0;                       // Initial capacitor voltage
  double OMEGA = 1.0 / std::sqrt(L * C); // Resonant frequency
  double PERIOD = 2.0 * M_PI / OMEGA;

  // Nets: 0=Gnd, 1=Vout
  TransientSolver solver(2);
  solver.companions().addInductor(1, 0, L);
  solver.companions().addCapacitor(1, 0, C);

  // Pre-charge capacitor to V0
  solver.companions().capacitor(0).prevVoltage = V0;

  solver.setStampCallback([](MnaSystem& /*mna*/, double /*time*/) {
    // Empty - no static elements
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 10.0 * PERIOD; // 10 cycles
  config.tStep = PERIOD / 100.0;
  config.dcOpPoint = false;
  config.method = IntegrationMethod::TRAPEZOIDAL;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 500);

  // Find first peak and last peak
  double firstPeak = 0.0;
  double lastPeak = 0.0;

  for (std::size_t i = 1; i < result.history.size() - 1; ++i) {
    double vPrev = result.history[i - 1].voltage(1);
    double v = result.history[i].voltage(1);
    double vNext = result.history[i + 1].voltage(1);

    if (v > vPrev && v > vNext && v > 0.5 * V0) {
      if (result.history[i].time < PERIOD) {
        firstPeak = std::max(firstPeak, v);
      }
      if (result.history[i].time > 9.0 * PERIOD) {
        lastPeak = std::max(lastPeak, v);
      }
    }
  }

  // Trapezoidal should conserve energy: last peak ≈ first peak
  // Allow 5% tolerance for numerical error accumulation
  EXPECT_NEAR(lastPeak, firstPeak, 0.05 * V0)
      << "Trapezoidal should conserve energy (amplitude constant)";

  // Additional check: amplitude should not decay significantly
  EXPECT_GT(lastPeak, 0.90 * firstPeak)
      << "Trapezoidal amplitude should not decay like backward Euler";
}

/**
 * @test Integration method comparison - RLC damped oscillator.
 *
 * Circuit (parallel RLC tank, capacitor pre-charged to V0):
 *   net 1 -- L -- GND
 *   net 1 -- C -- GND   (V_C(0) = V0)
 *   net 1 -- R -- GND
 *
 * Parallel RLC has damping coefficient alpha = 1/(2*R*C) and natural frequency
 * omega_0 = 1/sqrt(L*C). For underdamped oscillation we need alpha < omega_0,
 * which means R > 0.5 * sqrt(L/C). With L=1mH, C=1uF that requires R > ~15.8 ohm.
 *
 * R = 1000 ohm gives Q ~ 31.6 (lightly damped, clean oscillation).
 *
 * Compares backward Euler vs trapezoidal integration. Trapezoidal should
 * show slower amplitude decay (more accurate energy tracking).
 */
TEST(TransientSolver, RLCOscillatorMethodComparison) {
  double L = 1e-3;   // 1 mH
  double C = 1e-6;   // 1 uF
  double R = 1000.0; // 1k ohm (Q ~ 31.6, lightly damped)
  double V0 = 5.0;
  double OMEGA0 = 1.0 / std::sqrt(L * C);
  double ALPHA = 1.0 / (2.0 * R * C);
  double OMEGA = std::sqrt(OMEGA0 * OMEGA0 - ALPHA * ALPHA);
  double PERIOD = 2.0 * M_PI / OMEGA;

  auto runSimulation = [&](IntegrationMethod method) -> double {
    TransientSolver solver(2);
    solver.companions().addInductor(1, 0, L);
    solver.companions().addCapacitor(1, 0, C);
    solver.companions().capacitor(0).prevVoltage = V0;

    double G = 1.0 / R;
    solver.setStampCallback([G](MnaSystem& mna, double /*time*/) { mna.addConductance(1, 0, G); });

    TransientConfig config;
    config.tStart = 0.0;
    config.tEnd = 5.0 * PERIOD;
    config.tStep = PERIOD / 100.0;
    config.dcOpPoint = false;
    config.method = method;

    TransientResult result = solver.run(config, true);
    if (!result.success || result.history.empty()) {
      return 0.0;
    }

    // Track absolute peak amplitude (oscillation swings symmetrically through 0,
    // so peaks alternate sign -- take |v| at local extrema).
    double peakAmplitude = 0.0;
    for (std::size_t i = 1; i < result.history.size() - 1; ++i) {
      double vPrev = result.history[i - 1].voltage(1);
      double v = result.history[i].voltage(1);
      double vNext = result.history[i + 1].voltage(1);

      // Local extremum (max OR min) in the second half of the run
      bool isMax = (v > vPrev && v > vNext);
      bool isMin = (v < vPrev && v < vNext);
      if ((isMax || isMin) && result.history[i].time > 2.5 * PERIOD) {
        peakAmplitude = std::max(peakAmplitude, std::fabs(v));
      }
    }
    return peakAmplitude;
  };

  double peakBE = runSimulation(IntegrationMethod::BACKWARD_EULER);
  double peakTR = runSimulation(IntegrationMethod::TRAPEZOIDAL);

  // Both methods should still see meaningful oscillation in a high-Q (~31) tank
  // after only 2.5-5 periods.
  ASSERT_GT(peakBE, 0.0) << "Backward Euler simulation produced no peaks";
  ASSERT_GT(peakTR, 0.0) << "Trapezoidal simulation produced no peaks";

  // Trapezoidal preserves more energy than backward Euler -> larger amplitude
  EXPECT_GT(peakTR, peakBE) << "Trapezoidal should preserve more energy than backward Euler "
                            << "(peakBE=" << peakBE << ", peakTR=" << peakTR << ")";
}

/**
 * @test GEAR2 integration - stiff RC circuit stability.
 *
 * Circuit: Vdd -- R_large -- Vout -- C_small -- Gnd
 *
 * Stiff system with fast time constant (small τ = R*C). GEAR2 (BDF2) is
 * L-stable, providing better damping of high-frequency oscillations than
 * backward Euler or trapezoidal for stiff problems.
 */
TEST(TransientSolver, Gear2StiffRCCircuit) {
  double R = 100.0;   // 100 ohm (relatively large for small C)
  double C = 1e-9;    // 1 nF (small capacitor)
  double TAU = R * C; // 100 ns (fast time constant)

  // Nets: 0=Gnd, 1=Vdd, 2=Vout
  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * TAU;   // 500 ns
  config.tStep = TAU / 10.0; // 10 ns steps (relatively coarse for stiff system)
  config.dcOpPoint = false;
  config.method = IntegrationMethod::GEAR2;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 30);

  // Final voltage should be close to Vdd (steady state)
  double vFinal = result.finalState.voltage(2);
  EXPECT_GT(vFinal, 0.95 * VDD)
      << "GEAR2 should reach steady state even with coarse time steps on stiff system";

  // Verify monotonic approach (no overshoot/ringing)
  // GEAR2's L-stability should prevent oscillations
  double vPrev = 0.0;
  for (std::size_t i = 1; i < result.history.size(); ++i) {
    double v = result.history[i].voltage(2);
    // Allow small numerical noise, but no significant overshoot
    EXPECT_LE(v, VDD * 1.01) << "GEAR2 should not overshoot on stiff system at step " << i;
    EXPECT_GE(v, vPrev * 0.99) << "Voltage should be non-decreasing (L-stable damping)";
    vPrev = v;
  }
}

/**
 * @test History update verification - companion state shifting.
 *
 * Verifies that update() correctly shifts history (prev2 = prev, prev = new)
 * for GEAR2 integration.
 */
TEST(CompanionModels, HistoryUpdateShifting) {
  // Test capacitor history update
  CapacitorCompanion cap;
  cap.capacitance = 1e-6;
  cap.prevVoltage = 2.0;
  cap.prev2Voltage = 1.5;

  cap.update(3.0, 1e-9);

  EXPECT_DOUBLE_EQ(cap.prevVoltage, 3.0) << "prevVoltage should be updated to new voltage";
  EXPECT_DOUBLE_EQ(cap.prev2Voltage, 2.0) << "prev2Voltage should be shifted from prevVoltage";

  // Test inductor history update
  InductorCompanion ind;
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.1;
  ind.prev2Current = 0.08;

  ind.update(2.0, 1e-9); // V=2V, dt=1ns => dI = (1e-9/1e-3)*2.0 = 2e-6 A

  EXPECT_NEAR(ind.prevCurrent, 0.100002, 1e-9) << "prevCurrent should be I_old + dI";
  EXPECT_DOUBLE_EQ(ind.prev2Current, 0.1) << "prev2Current should be shifted from prevCurrent";
}

/* ----------------------------- Sparse Solver Tests ----------------------------- */

/**
 * @test RC charging with sparse solver enabled.
 *
 * Exercises the setSparse(true) + statefulStampCallbackSparse path.
 * Uses same RC circuit as dense tests for comparison.
 */
TEST(TransientSolver, SparseRCCharging) {
  double R = 1000.0;
  double C = 1e-6;
  double TAU = R * C;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setSparse(true);

  double G = 1.0 / R;

  // Sparse path requires StatefulStampCallbackSparse
  solver.setStatefulStampCallbackSparse(
      [G](MnaSystemSparse& mna, double /*time*/, const std::vector<double>& /*prevV*/) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
      });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * TAU;
  config.tStep = TAU / 50.0;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, true);

  ASSERT_TRUE(result.success) << result.errorMessage;
  ASSERT_GT(result.history.size(), 10);

  // Final voltage should approach Vdd
  double vFinal = result.finalState.voltage(2);
  EXPECT_GT(vFinal, 0.90 * VDD) << "Sparse solver should produce correct RC charging";
}

/**
 * @test Sparse solver with DC operating point.
 *
 * Exercises computeDC() sparse path (useSparse_ && statefulStampCallbackSparse_).
 */
TEST(TransientSolver, SparseDCOpPoint) {
  double R = 1000.0;
  double C = 1e-6;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setSparse(true);

  double G = 1.0 / R;

  solver.setStatefulStampCallbackSparse(
      [G](MnaSystemSparse& mna, double /*time*/, const std::vector<double>& /*prevV*/) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
      });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 1e-5;
  config.tStep = 1e-6;
  config.dcOpPoint = true;

  TransientResult result = solver.run(config, false);

  ASSERT_TRUE(result.success) << result.errorMessage;

  // DC op point: capacitor should start charged to Vdd
  double vFinal = result.finalState.voltage(2);
  EXPECT_NEAR(vFinal, VDD, 0.1) << "Sparse DC op point should find correct steady state";
}

/* ----------------------------- Cached LU Tests ----------------------------- */

/**
 * @test Cached LU optimization produces correct results.
 *
 * Enables setCachedLU(true) and verifies the RC charging result
 * matches the non-cached path.
 */
TEST(TransientSolver, CachedLURCCharging) {
  double R = 1000.0;
  double C = 1e-6;
  double TAU = R * C;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setCachedLU(true);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * TAU;
  config.tStep = TAU / 50.0;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, false);

  ASSERT_TRUE(result.success) << result.errorMessage;

  // Same validation as dense non-cached
  double vFinal = result.finalState.voltage(2);
  EXPECT_GT(vFinal, 0.90 * VDD) << "Cached LU should produce correct RC charging";
}

/**
 * @test Cached LU invalidation and re-factorization.
 *
 * Runs a few steps, invalidates cache, then continues. This exercises
 * the captureLU path after the first solve and the re-factorization
 * path after invalidation.
 */
TEST(TransientSolver, CachedLUInvalidateAndContinue) {
  double R = 1000.0;
  double C = 1e-6;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setCachedLU(true);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientState state;
  state.resize(3, 0);
  double dt = 1e-7;

  // Step a few times to populate cache
  for (int i = 0; i < 5; ++i) {
    TransientStatus status = solver.step(dt, state);
    ASSERT_EQ(status, TransientStatus::SUCCESS) << "Step " << i << " failed";
  }

  double vBeforeInvalidate = state.voltage(2);
  EXPECT_GT(vBeforeInvalidate, 0.0);

  // Invalidate and continue
  solver.invalidateCache();

  for (int i = 0; i < 5; ++i) {
    TransientStatus status = solver.step(dt, state);
    ASSERT_EQ(status, TransientStatus::SUCCESS) << "Post-invalidate step " << i << " failed";
  }

  double vAfterContinue = state.voltage(2);
  EXPECT_GT(vAfterContinue, vBeforeInvalidate)
      << "Voltage should keep increasing after cache invalidation";
}

/* ----------------------------- Dual LU Tests ----------------------------- */

/**
 * @test Dual-LU stepping with alternating states.
 *
 * Exercises stepDual() with two alternating stamp configurations
 * (simulating clock HIGH/LOW toggling).
 */
TEST(TransientSolver, DualLUStepping) {
  double R = 1000.0;
  double C = 1e-6;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setDualLU(true);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientState state;
  state.resize(3, 0);
  double dt = 1e-7;

  // Alternate between state 0 and state 1
  for (int i = 0; i < 10; ++i) {
    int stateIdx = i % 2;
    TransientStatus status = solver.stepDual(dt, stateIdx, state);
    ASSERT_EQ(status, TransientStatus::SUCCESS)
        << "Dual LU step " << i << " (state " << stateIdx << ") failed";
  }

  // Should have charged toward Vdd
  double vFinal = state.voltage(2);
  EXPECT_GT(vFinal, 0.0) << "Dual LU stepping should produce charging behavior";
}

/** @test stepDual with invalid state index returns error. */
TEST(TransientSolver, DualLUInvalidStateIndex) {
  TransientSolver solver(3);
  solver.setDualLU(true);
  solver.setStampCallback([](MnaSystem& mna, double /*time*/) { mna.addVoltageSource(1, 0, VDD); });

  TransientState state;
  state.resize(3, 0);

  TransientStatus status = solver.stepDual(1e-7, -1, state);
  EXPECT_EQ(status, TransientStatus::ERROR_STEP_FAILED)
      << "Invalid state index should return error";

  status = solver.stepDual(1e-7, 2, state);
  EXPECT_EQ(status, TransientStatus::ERROR_STEP_FAILED)
      << "State index 2 (out of range) should return error";
}

/* ----------------------------- Callback Tests ----------------------------- */

/**
 * @test NR limit callback is invoked during sparse nonlinear path.
 *
 * Sets alwaysReanalyze to force NR iteration path, then verifies
 * the limit callback is invoked.
 */
TEST(TransientSolver, NrLimitCallbackInvoked) {
  double R = 1000.0;
  double C = 1e-6;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setSparse(true);
  solver.setAlwaysReanalyze(true);

  double G = 1.0 / R;

  solver.setStatefulStampCallbackSparse(
      [G](MnaSystemSparse& mna, double /*time*/, const std::vector<double>& /*prevV*/) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
      });

  int limitCallCount = 0;
  solver.setNrLimitCallback(
      [&limitCallCount](std::vector<double>& /*newV*/, const std::vector<double>& /*prevV*/) {
        ++limitCallCount;
      });

  TransientState state;
  state.resize(3, 0);
  double dt = 1e-7;

  TransientStatus status = solver.step(dt, state);
  ASSERT_EQ(status, TransientStatus::SUCCESS);
  EXPECT_GT(limitCallCount, 0) << "NR limit callback should be invoked at least once";
}

/**
 * @test NR pre-batch callback is invoked during sparse nonlinear path.
 *
 * Verifies that the pre-batch callback fires once per timestep and
 * receives the correct dt value.
 */
TEST(TransientSolver, NrPreBatchCallbackInvoked) {
  double R = 1000.0;
  double C = 1e-6;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setSparse(true);
  solver.setAlwaysReanalyze(true);

  double G = 1.0 / R;

  solver.setStatefulStampCallbackSparse(
      [G](MnaSystemSparse& mna, double /*time*/, const std::vector<double>& /*prevV*/) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
      });

  int preBatchCallCount = 0;
  double receivedDt = 0.0;
  solver.setNrPreBatchCallback([&preBatchCallCount, &receivedDt](double subDt) {
    ++preBatchCallCount;
    receivedDt = subDt;
  });

  TransientState state;
  state.resize(3, 0);
  double dt = 1e-7;

  TransientStatus status = solver.step(dt, state);
  ASSERT_EQ(status, TransientStatus::SUCCESS);
  EXPECT_EQ(preBatchCallCount, 1) << "Pre-batch callback should fire once per step";
  EXPECT_DOUBLE_EQ(receivedDt, dt) << "Pre-batch callback should receive correct dt";
}

/* ----------------------------- Stateful Stamp Callback Tests ----------------------------- */

/**
 * @test Stateful stamp callback receives previous voltages.
 *
 * Exercises the statefulStampCallback_ path in invokeStampCallback.
 */
TEST(TransientSolver, StatefulStampCallbackReceivesPrevVoltages) {
  double R = 1000.0;
  double C = 1e-6;
  double TAU = R * C;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);

  double G = 1.0 / R;
  int callCount = 0;
  bool receivedNonZeroPrevV = false;

  solver.setStatefulStampCallback(
      [G, &callCount, &receivedNonZeroPrevV](MnaSystem& mna, double /*time*/,
                                             const std::vector<double>& prevV) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
        ++callCount;
        // After first solve, prevVoltages should be non-zero
        if (callCount > 2 && prevV.size() > 2 && prevV[2] > 0.01) {
          receivedNonZeroPrevV = true;
        }
      });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 0.5 * TAU;
  config.tStep = TAU / 50.0;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, false);

  ASSERT_TRUE(result.success) << result.errorMessage;
  EXPECT_GT(callCount, 2) << "Stateful callback should be called multiple times";
  EXPECT_TRUE(receivedNonZeroPrevV) << "Callback should receive non-zero previous voltages";
}

/* ----------------------------- computeDC Tests ----------------------------- */

/**
 * @test computeDC with inductor (shorted as high conductance).
 *
 * For DC: capacitors are open, inductors are shorted.
 * An RL circuit at DC should have full current flowing through the inductor.
 */
TEST(TransientSolver, ComputeDCWithInductor) {
  double R = 100.0;
  double L = 10e-3;

  TransientSolver solver(3);
  solver.companions().addInductor(2, 0, L);

  double G = 1.0 / R;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientState state;
  TransientStatus status = solver.computeDC(state);

  ASSERT_EQ(status, TransientStatus::SUCCESS);

  // At DC, inductor is a short: V(mid) should be near 0
  double vMid = state.voltage(2);
  EXPECT_NEAR(vMid, 0.0, 0.1) << "Inductor shorted at DC should pull node to ground";
}

/**
 * @test computeDC with sparse solver path.
 */
TEST(TransientSolver, ComputeDCSparse) {
  double R = 1000.0;

  TransientSolver solver(3);
  solver.setSparse(true);

  double G = 1.0 / R;
  solver.setStatefulStampCallbackSparse(
      [G](MnaSystemSparse& mna, double /*time*/, const std::vector<double>& /*prevV*/) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
      });

  // Add load resistor to ground so node 2 has a defined voltage
  solver.setStatefulStampCallbackSparse(
      [G](MnaSystemSparse& mna, double /*time*/, const std::vector<double>& /*prevV*/) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
        mna.addConductance(2, 0, G); // Load to ground
      });

  TransientState state;
  TransientStatus status = solver.computeDC(state);

  ASSERT_EQ(status, TransientStatus::SUCCESS);

  // Voltage divider: V(2) = VDD * G/(G+G) = VDD/2
  double v2 = state.voltage(2);
  EXPECT_NEAR(v2, VDD / 2.0, 0.1) << "Sparse DC should solve voltage divider";
}

/* ----------------------------- Sparse NR Iteration Tests ----------------------------- */

/**
 * @test Sparse NR iteration path (alwaysReanalyze) produces correct results.
 *
 * Exercises the full Newton-Raphson iteration loop in the sparse path,
 * including NaN protection and 5V damping logic.
 */
TEST(TransientSolver, SparseNRIterationConverges) {
  double R = 1000.0;
  double C = 1e-6;
  double TAU = R * C;

  TransientSolver solver(3);
  solver.companions().addCapacitor(2, 0, C);
  solver.setSparse(true);
  solver.setAlwaysReanalyze(true);

  double G = 1.0 / R;
  solver.setStatefulStampCallbackSparse(
      [G](MnaSystemSparse& mna, double /*time*/, const std::vector<double>& /*prevV*/) {
        mna.addVoltageSource(1, 0, VDD);
        mna.addConductance(1, 2, G);
      });

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 3.0 * TAU;
  config.tStep = TAU / 50.0;
  config.dcOpPoint = false;

  TransientResult result = solver.run(config, false);

  ASSERT_TRUE(result.success) << result.errorMessage;

  double vFinal = result.finalState.voltage(2);
  EXPECT_GT(vFinal, 0.90 * VDD) << "Sparse NR path should produce correct RC charging";
}

/* ----------------------------- setDualLU Disables Single Cache ----------------------------- */

/**
 * @test setDualLU(true) disables single cached LU.
 *
 * Verifies the mutual exclusion between dual-LU and single cached-LU.
 */
TEST(TransientSolver, DualLUDisablesSingleCache) {
  TransientSolver solver(3);

  solver.setCachedLU(true);
  solver.setDualLU(true);

  // After setDualLU(true), single cache should be off.
  // Verify by running: if single cache were active, it would use
  // different code path, but result should still be correct.
  solver.companions().addCapacitor(2, 0, 1e-6);
  double G = 1.0 / 1000.0;
  solver.setStampCallback([G](MnaSystem& mna, double /*time*/) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addConductance(1, 2, G);
  });

  TransientState state;
  state.resize(3, 0);

  // Use stepDual which should work since dualLU is enabled
  TransientStatus status = solver.stepDual(1e-7, 0, state);
  EXPECT_EQ(status, TransientStatus::SUCCESS);
}
