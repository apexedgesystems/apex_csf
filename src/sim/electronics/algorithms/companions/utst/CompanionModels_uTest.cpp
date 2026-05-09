/**
 * @file CompanionModels_uTest.cpp
 * @brief Unit tests for CapacitorCompanion and InductorCompanion.
 *
 * Verifies the discretized Geq / Ieq stamps for the three integration
 * methods (Backward Euler, Trapezoidal, GEAR2) and the state-update
 * step that advances the device through one time step.
 *
 * Notes:
 *  - Tests use closed-form values so the assertions are deterministic.
 *  - No MNA system is constructed for the math tests; we exercise
 *    `geq`, `ieq`, and `update` directly.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::algorithms::companions::CapacitorCompanion;
using sim::electronics::algorithms::companions::InductorCompanion;
using sim::electronics::algorithms::transient::IntegrationMethod;

namespace {

constexpr double TOL = 1e-12;

}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed CapacitorCompanion is a zero-state placeholder */
TEST(CapacitorCompanionDefaultTest, DefaultIsZero) {
  const CapacitorCompanion CAP{};
  EXPECT_EQ(CAP.posNet, 0u);
  EXPECT_EQ(CAP.negNet, 0u);
  EXPECT_DOUBLE_EQ(CAP.capacitance, 0.0);
  EXPECT_DOUBLE_EQ(CAP.prevVoltage, 0.0);
  EXPECT_DOUBLE_EQ(CAP.prev2Voltage, 0.0);
  EXPECT_DOUBLE_EQ(CAP.current, 0.0);
}

/** @test Default-constructed InductorCompanion is a zero-state placeholder */
TEST(InductorCompanionDefaultTest, DefaultIsZero) {
  const InductorCompanion IND{};
  EXPECT_EQ(IND.posNet, 0u);
  EXPECT_EQ(IND.negNet, 0u);
  EXPECT_DOUBLE_EQ(IND.inductance, 0.0);
  EXPECT_DOUBLE_EQ(IND.prevCurrent, 0.0);
  EXPECT_DOUBLE_EQ(IND.prev2Current, 0.0);
  EXPECT_DOUBLE_EQ(IND.voltage, 0.0);
}

/* ----------------------------- CapacitorCompanion Geq ----------------------------- */

/** @test Capacitor Backward-Euler Geq is C/dt */
TEST(CapacitorCompanionGeqTest, BackwardEulerFormula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6; // 1 uF
  const double DT = 1e-3;
  EXPECT_NEAR(cap.geq(DT, IntegrationMethod::BACKWARD_EULER),
              cap.capacitance / DT, TOL);
}

/** @test Capacitor Trapezoidal Geq is 2C/dt */
TEST(CapacitorCompanionGeqTest, TrapezoidalFormula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  const double DT = 1e-3;
  EXPECT_NEAR(cap.geq(DT, IntegrationMethod::TRAPEZOIDAL),
              2.0 * cap.capacitance / DT, TOL);
}

/** @test Capacitor GEAR2 Geq is 1.5*C/dt (BDF2) */
TEST(CapacitorCompanionGeqTest, Gear2Formula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  const double DT = 1e-3;
  EXPECT_NEAR(cap.geq(DT, IntegrationMethod::GEAR2),
              1.5 * cap.capacitance / DT, TOL);
}

/** @test Capacitor Geq scales linearly with capacitance and inversely with dt */
TEST(CapacitorCompanionGeqTest, Scaling) {
  CapacitorCompanion cap{};
  cap.capacitance = 2e-9; // 2 nF
  const double G_AT_1NS = cap.geq(1e-9, IntegrationMethod::BACKWARD_EULER);
  const double G_AT_2NS = cap.geq(2e-9, IntegrationMethod::BACKWARD_EULER);
  // Halving dt should double Geq.
  EXPECT_NEAR(G_AT_1NS, 2.0 * G_AT_2NS, TOL);

  cap.capacitance = 4e-9;
  const double G_BIGGER = cap.geq(1e-9, IntegrationMethod::BACKWARD_EULER);
  EXPECT_NEAR(G_BIGGER, 2.0 * G_AT_1NS, TOL);
}

/* ----------------------------- CapacitorCompanion Ieq ----------------------------- */

/** @test Capacitor Backward-Euler Ieq is C*Vprev/dt */
TEST(CapacitorCompanionIeqTest, BackwardEulerFormula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  cap.prevVoltage = 0.5;
  const double DT = 1e-4;
  EXPECT_NEAR(cap.ieq(DT, IntegrationMethod::BACKWARD_EULER),
              cap.capacitance * cap.prevVoltage / DT, TOL);
}

/** @test Capacitor Trapezoidal Ieq folds in previous current */
TEST(CapacitorCompanionIeqTest, TrapezoidalFormula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  cap.prevVoltage = 0.5;
  cap.current = 1e-3;
  const double DT = 1e-4;
  const double EXPECTED = 2.0 * cap.capacitance * cap.prevVoltage / DT + cap.current;
  EXPECT_NEAR(cap.ieq(DT, IntegrationMethod::TRAPEZOIDAL), EXPECTED, TOL);
}

/** @test Capacitor GEAR2 Ieq uses two-step history (2*Vprev - 0.5*Vprev2) */
TEST(CapacitorCompanionIeqTest, Gear2Formula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  cap.prevVoltage = 0.5;
  cap.prev2Voltage = 0.3;
  const double DT = 1e-4;
  const double EXPECTED =
      (cap.capacitance / DT) * (2.0 * cap.prevVoltage - 0.5 * cap.prev2Voltage);
  EXPECT_NEAR(cap.ieq(DT, IntegrationMethod::GEAR2), EXPECTED, TOL);
}

/** @test Capacitor Ieq is zero when no charge is stored */
TEST(CapacitorCompanionIeqTest, ZeroWhenUncharged) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  EXPECT_DOUBLE_EQ(cap.ieq(1e-3, IntegrationMethod::BACKWARD_EULER), 0.0);
  EXPECT_DOUBLE_EQ(cap.ieq(1e-3, IntegrationMethod::TRAPEZOIDAL), 0.0);
  EXPECT_DOUBLE_EQ(cap.ieq(1e-3, IntegrationMethod::GEAR2), 0.0);
}

/* ----------------------------- CapacitorCompanion update ----------------------------- */

/** @test Capacitor update shifts history (prev2 <- prev <- new) and computes I = C*dV/dt */
TEST(CapacitorCompanionUpdateTest, ShiftsHistoryAndComputesCurrent) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  cap.prevVoltage = 0.2;
  cap.prev2Voltage = 0.1;

  const double NEW_V = 0.5;
  const double DT = 1e-3;
  cap.update(NEW_V, DT);

  EXPECT_NEAR(cap.current, cap.capacitance * (NEW_V - 0.2) / DT, TOL);
  EXPECT_DOUBLE_EQ(cap.prevVoltage, NEW_V);
  EXPECT_DOUBLE_EQ(cap.prev2Voltage, 0.2);
}

/** @test Repeated update with constant voltage drives current to zero (steady state) */
TEST(CapacitorCompanionUpdateTest, ConstantVoltageDrivesCurrentToZero) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  cap.prevVoltage = 0.0;

  const double DT = 1e-3;
  cap.update(1.0, DT); // step up
  EXPECT_GT(std::fabs(cap.current), 0.0);
  cap.update(1.0, DT); // hold
  EXPECT_NEAR(cap.current, 0.0, TOL);
  cap.update(1.0, DT); // hold
  EXPECT_NEAR(cap.current, 0.0, TOL);
}

/* ----------------------------- InductorCompanion Geq ----------------------------- */

/** @test Inductor Backward-Euler Geq is dt/L */
TEST(InductorCompanionGeqTest, BackwardEulerFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3; // 1 mH
  const double DT = 1e-6;
  EXPECT_NEAR(ind.geq(DT, IntegrationMethod::BACKWARD_EULER),
              DT / ind.inductance, TOL);
}

/** @test Inductor Trapezoidal Geq is dt/(2L) */
TEST(InductorCompanionGeqTest, TrapezoidalFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  const double DT = 1e-6;
  EXPECT_NEAR(ind.geq(DT, IntegrationMethod::TRAPEZOIDAL),
              DT / (2.0 * ind.inductance), TOL);
}

/** @test Inductor GEAR2 Geq is 1.5*dt/L */
TEST(InductorCompanionGeqTest, Gear2Formula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  const double DT = 1e-6;
  EXPECT_NEAR(ind.geq(DT, IntegrationMethod::GEAR2),
              1.5 * DT / ind.inductance, TOL);
}

/* ----------------------------- InductorCompanion Ieq ----------------------------- */

/** @test Inductor Backward-Euler Ieq is the previous current */
TEST(InductorCompanionIeqTest, BackwardEulerFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.05; // 50 mA
  EXPECT_DOUBLE_EQ(ind.ieq(1e-6, IntegrationMethod::BACKWARD_EULER),
                   ind.prevCurrent);
}

/** @test Inductor Trapezoidal Ieq folds in previous voltage */
TEST(InductorCompanionIeqTest, TrapezoidalFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.05;
  ind.voltage = 0.1;
  const double DT = 1e-6;
  const double EXPECTED =
      ind.prevCurrent + (DT / (2.0 * ind.inductance)) * ind.voltage;
  EXPECT_NEAR(ind.ieq(DT, IntegrationMethod::TRAPEZOIDAL), EXPECTED, TOL);
}

/** @test Inductor GEAR2 Ieq uses two-step history (2*Iprev - 0.5*Iprev2) */
TEST(InductorCompanionIeqTest, Gear2Formula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.10;
  ind.prev2Current = 0.05;
  EXPECT_NEAR(ind.ieq(1e-6, IntegrationMethod::GEAR2),
              2.0 * ind.prevCurrent - 0.5 * ind.prev2Current, TOL);
}

/* ----------------------------- InductorCompanion update ----------------------------- */

/** @test Inductor update shifts history (prev2 <- prev <- new) and integrates I */
TEST(InductorCompanionUpdateTest, ShiftsHistoryAndIntegratesCurrent) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.0;
  ind.prev2Current = 0.0;

  const double V = 0.1;
  const double DT = 1e-6;
  ind.update(V, DT);

  // I(t) = I(t-dt) + (dt/L)*V(t)
  const double EXPECTED_I = (DT / ind.inductance) * V;
  EXPECT_NEAR(ind.prevCurrent, EXPECTED_I, TOL);
  EXPECT_DOUBLE_EQ(ind.voltage, V);
  EXPECT_DOUBLE_EQ(ind.prev2Current, 0.0);
}

/** @test Inductor zero voltage holds current constant */
TEST(InductorCompanionUpdateTest, ZeroVoltageHoldsCurrent) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.05;

  ind.update(0.0, 1e-6);
  EXPECT_NEAR(ind.prevCurrent, 0.05, TOL);
  EXPECT_NEAR(ind.voltage, 0.0, TOL);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test geq() is deterministic across repeated calls */
TEST(CapacitorCompanionDeterminismTest, GeqIsRepeatable) {
  CapacitorCompanion cap{};
  cap.capacitance = 1.7e-9;
  const double DT = 3.4e-7;
  const double FIRST = cap.geq(DT, IntegrationMethod::TRAPEZOIDAL);
  for (int i = 0; i < 100; ++i) {
    EXPECT_DOUBLE_EQ(cap.geq(DT, IntegrationMethod::TRAPEZOIDAL), FIRST);
  }
}

/** @test geq() is deterministic across repeated calls */
TEST(InductorCompanionDeterminismTest, GeqIsRepeatable) {
  InductorCompanion ind{};
  ind.inductance = 2.5e-3;
  const double DT = 1.1e-5;
  const double FIRST = ind.geq(DT, IntegrationMethod::GEAR2);
  for (int i = 0; i < 100; ++i) {
    EXPECT_DOUBLE_EQ(ind.geq(DT, IntegrationMethod::GEAR2), FIRST);
  }
}
