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
using sim::electronics::algorithms::companions::Gear2Weights;
using sim::electronics::algorithms::companions::gear2Weights;
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
  EXPECT_NEAR(cap.geq(DT, IntegrationMethod::BACKWARD_EULER), cap.capacitance / DT, TOL);
}

/** @test Capacitor Trapezoidal Geq is 2C/dt */
TEST(CapacitorCompanionGeqTest, TrapezoidalFormula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  const double DT = 1e-3;
  EXPECT_NEAR(cap.geq(DT, IntegrationMethod::TRAPEZOIDAL), 2.0 * cap.capacitance / DT, TOL);
}

/** @test Capacitor GEAR2 Geq is 1.5*C/dt after a step of the same length, C/dt on a first step */
TEST(CapacitorCompanionGeqTest, Gear2Formula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  const double DT = 1e-3;
  EXPECT_NEAR(cap.geq(DT, IntegrationMethod::GEAR2), cap.capacitance / DT, TOL);
  cap.prevStep = DT;
  EXPECT_NEAR(cap.geq(DT, IntegrationMethod::GEAR2), 1.5 * cap.capacitance / DT, TOL);
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

/**
 * @test Capacitor GEAR2 Ieq uses two steps of history (2*Vprev - 0.5*Vprev2)
 *       after a step of the same length, and the last value alone on a first step.
 */
TEST(CapacitorCompanionIeqTest, Gear2Formula) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  cap.prevVoltage = 0.5;
  cap.prev2Voltage = 0.3;
  const double DT = 1e-4;
  EXPECT_NEAR(cap.ieq(DT, IntegrationMethod::GEAR2), cap.capacitance * 0.5 / DT, TOL);
  cap.prevStep = DT;
  const double EXPECTED = (cap.capacitance / DT) * (2.0 * cap.prevVoltage - 0.5 * cap.prev2Voltage);
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

/**
 * @test Capacitor update takes the current each method's formula gives and
 *       shifts the history.
 *
 * 1 uF at 0.1 V, then 0.2 V carrying 0.1 mA, then 0.5 V, in steps of 1 ms.
 */
TEST(CapacitorCompanionUpdateTest, CurrentIsTheMethodsOwnAndHistoryShifts) {
  struct Case {
    IntegrationMethod method;
    double amperes;
  };
  // Backward Euler: C * (0.5 - 0.2) / dt.
  // Trapezoidal:    2C * (0.5 - 0.2) / dt - 0.1 mA.
  // GEAR2:          C * (1.5 * 0.5 - 2 * 0.2 + 0.5 * 0.1) / dt.
  for (const Case& CASE :
       {Case{IntegrationMethod::BACKWARD_EULER, 3e-4}, Case{IntegrationMethod::TRAPEZOIDAL, 5e-4},
        Case{IntegrationMethod::GEAR2, 4e-4}}) {
    CapacitorCompanion cap{};
    cap.capacitance = 1e-6;
    cap.prev2Voltage = 0.1;
    cap.prevVoltage = 0.2;
    cap.current = 1e-4;
    cap.prevStep = 1e-3;

    cap.update(0.5, 1e-3, CASE.method);

    EXPECT_NEAR(cap.current, CASE.amperes, 1e-15) << "method " << static_cast<int>(CASE.method);
    EXPECT_DOUBLE_EQ(cap.prevVoltage, 0.5);
    EXPECT_DOUBLE_EQ(cap.prev2Voltage, 0.2);
    EXPECT_DOUBLE_EQ(cap.prevStep, 1e-3);
  }
}

/** @test A voltage stepped up and then held carries no current once the history is flat */
TEST(CapacitorCompanionUpdateTest, HeldVoltageCarriesNoCurrent) {
  // Backward Euler forgets after one held step, GEAR2 after two.
  struct Case {
    IntegrationMethod method;
    int holds;
  };
  for (const Case& CASE :
       {Case{IntegrationMethod::BACKWARD_EULER, 1}, Case{IntegrationMethod::GEAR2, 2}}) {
    CapacitorCompanion cap{};
    cap.capacitance = 1e-6;

    cap.update(1.0, 1e-3, CASE.method);
    EXPECT_GT(cap.current, 0.0);
    for (int hold = 0; hold < CASE.holds; ++hold) {
      cap.update(1.0, 1e-3, CASE.method);
    }
    EXPECT_NEAR(cap.current, 0.0, 1e-18) << "method " << static_cast<int>(CASE.method);
  }
}

/** @test setInitialVoltage leaves a flat history, no current and no step recorded */
TEST(CapacitorCompanionUpdateTest, InitialVoltageIsAStateOfRest) {
  CapacitorCompanion cap{};
  cap.capacitance = 1e-6;
  cap.update(3.0, 1e-3, IntegrationMethod::TRAPEZOIDAL);

  cap.setInitialVoltage(5.0);
  EXPECT_DOUBLE_EQ(cap.prevVoltage, 5.0);
  EXPECT_DOUBLE_EQ(cap.prev2Voltage, 5.0);
  EXPECT_DOUBLE_EQ(cap.current, 0.0);
  EXPECT_DOUBLE_EQ(cap.prevStep, 0.0);

  // Held there, no method sees a current.
  for (const IntegrationMethod METHOD :
       {IntegrationMethod::BACKWARD_EULER, IntegrationMethod::TRAPEZOIDAL,
        IntegrationMethod::GEAR2}) {
    CapacitorCompanion held = cap;
    held.update(5.0, 1e-3, METHOD);
    // Rounding of two terms of 10 mA.
    EXPECT_NEAR(held.current, 0.0, 1e-15) << "method " << static_cast<int>(METHOD);
  }
}

/* ----------------------------- Gear2Weights ----------------------------- */

/** @test Equal steps give 3/2, 2 and 1/2 */
TEST(Gear2WeightsTest, EqualStepsAreTheClassicWeights) {
  const Gear2Weights W = gear2Weights(1e-3, 1e-3);
  EXPECT_DOUBLE_EQ(W.a0, 1.5);
  EXPECT_DOUBLE_EQ(W.a1, 2.0);
  EXPECT_DOUBLE_EQ(W.a2, 0.5);
}

/** @test With no step before it a step takes backward Euler's weights: 1, 1 and 0 */
TEST(Gear2WeightsTest, FirstStepIsBackwardEuler) {
  const Gear2Weights W = gear2Weights(1e-3, 0.0);
  EXPECT_DOUBLE_EQ(W.a0, 1.0);
  EXPECT_DOUBLE_EQ(W.a1, 1.0);
  EXPECT_DOUBLE_EQ(W.a2, 0.0);
}

/**
 * @test An element's first GEAR2 step is its backward-Euler step, value for
 *       value, and its second is Gear's formula on the two values it then has.
 */
TEST(Gear2WeightsTest, ElementsStartAsBackwardEulerAndGoOnAsGear) {
  constexpr double DT = 1e-4;
  CapacitorCompanion gear{};
  gear.capacitance = 1e-6;
  gear.setInitialVoltage(2.0);
  CapacitorCompanion euler = gear;
  EXPECT_DOUBLE_EQ(gear.geq(DT, IntegrationMethod::GEAR2),
                   euler.geq(DT, IntegrationMethod::BACKWARD_EULER));
  EXPECT_DOUBLE_EQ(gear.ieq(DT, IntegrationMethod::GEAR2),
                   euler.ieq(DT, IntegrationMethod::BACKWARD_EULER));
  gear.update(1.5, DT, IntegrationMethod::GEAR2);
  euler.update(1.5, DT, IntegrationMethod::BACKWARD_EULER);
  EXPECT_DOUBLE_EQ(gear.current, euler.current);
  // Second step: 3C/(2dt), and (C/dt) * (2 * 1.5 - 0.5 * 2.0).
  EXPECT_NEAR(gear.geq(DT, IntegrationMethod::GEAR2), 1.5e-2, TOL);
  EXPECT_NEAR(gear.ieq(DT, IntegrationMethod::GEAR2), 2.0e-2, TOL);

  InductorCompanion coil{};
  coil.inductance = 1e-3;
  coil.setInitialCurrent(0.05);
  InductorCompanion coilEuler = coil;
  EXPECT_DOUBLE_EQ(coil.geq(DT, IntegrationMethod::GEAR2),
                   coilEuler.geq(DT, IntegrationMethod::BACKWARD_EULER));
  EXPECT_DOUBLE_EQ(coil.ieq(DT, IntegrationMethod::GEAR2),
                   coilEuler.ieq(DT, IntegrationMethod::BACKWARD_EULER));
  coil.update(0.1, DT, IntegrationMethod::GEAR2);
  coilEuler.update(0.1, DT, IntegrationMethod::BACKWARD_EULER);
  EXPECT_DOUBLE_EQ(coil.prevCurrent, coilEuler.prevCurrent);
  // Second step: 2dt/(3L), and (4 * 0.06 - 0.05) / 3.
  EXPECT_NEAR(coil.geq(DT, IntegrationMethod::GEAR2), 2.0e-4 / 3.0e-3, TOL);
  EXPECT_NEAR(coil.ieq(DT, IntegrationMethod::GEAR2), 0.19 / 3.0, TOL);
}

/** @test Whatever the two steps, the weights differentiate a quadratic exactly */
TEST(Gear2WeightsTest, QuadraticIsDifferentiatedExactlyForAnyStepRatio) {
  // y = 3 t^2 - 2 t + 1 has slope -2 at t = 0.
  const auto Y = [](double t) { return 3.0 * t * t - 2.0 * t + 1.0; };
  const double DT = 0.01;
  for (const double BEFORE : {0.01, 0.02, 0.005, 0.1, 0.001}) {
    const Gear2Weights W = gear2Weights(DT, BEFORE);
    const double SLOPE = (W.a0 * Y(0.0) - W.a1 * Y(-DT) + W.a2 * Y(-DT - BEFORE)) / DT;
    EXPECT_NEAR(SLOPE, -2.0, 1e-12) << "step before " << BEFORE;
  }
}

/** @test A step vanishing beside the one before it tends to backward Euler */
TEST(Gear2WeightsTest, VanishingStepTendsToBackwardEuler) {
  const Gear2Weights W = gear2Weights(1e-21, 1e-7);
  EXPECT_NEAR(W.a0, 1.0, 1e-12);
  EXPECT_NEAR(W.a1, 1.0, 1e-12);
  EXPECT_NEAR(W.a2, 0.0, 1e-12);
}

/* ----------------------------- InductorCompanion Geq ----------------------------- */

/** @test Inductor Backward-Euler Geq is dt/L */
TEST(InductorCompanionGeqTest, BackwardEulerFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3; // 1 mH
  const double DT = 1e-6;
  EXPECT_NEAR(ind.geq(DT, IntegrationMethod::BACKWARD_EULER), DT / ind.inductance, TOL);
}

/** @test Inductor Trapezoidal Geq is dt/(2L) */
TEST(InductorCompanionGeqTest, TrapezoidalFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  const double DT = 1e-6;
  EXPECT_NEAR(ind.geq(DT, IntegrationMethod::TRAPEZOIDAL), DT / (2.0 * ind.inductance), TOL);
}

/**
 * @test Inductor GEAR2 Geq is 2*dt/(3*L) after a step of the same length: 1 mH
 *       and 1 us give 2/3 mS. On a first step it is dt/L, 1 mS.
 */
TEST(InductorCompanionGeqTest, Gear2Formula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  EXPECT_NEAR(ind.geq(1e-6, IntegrationMethod::GEAR2), 1.0e-3, TOL);
  ind.prevStep = 1e-6;
  EXPECT_NEAR(ind.geq(1e-6, IntegrationMethod::GEAR2), 2.0e-3 / 3.0, TOL);
}

/* ----------------------------- InductorCompanion Ieq ----------------------------- */

/** @test Inductor Backward-Euler Ieq is the previous current */
TEST(InductorCompanionIeqTest, BackwardEulerFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.05; // 50 mA
  EXPECT_DOUBLE_EQ(ind.ieq(1e-6, IntegrationMethod::BACKWARD_EULER), ind.prevCurrent);
}

/** @test Inductor Trapezoidal Ieq folds in previous voltage */
TEST(InductorCompanionIeqTest, TrapezoidalFormula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.05;
  ind.voltage = 0.1;
  const double DT = 1e-6;
  const double EXPECTED = ind.prevCurrent + (DT / (2.0 * ind.inductance)) * ind.voltage;
  EXPECT_NEAR(ind.ieq(DT, IntegrationMethod::TRAPEZOIDAL), EXPECTED, TOL);
}

/**
 * @test Inductor GEAR2 Ieq is (4*Iprev - Iprev2) / 3 after a step of the same
 *       length: 0.10 A and 0.05 A give 0.35 / 3 A. On a first step it is Iprev.
 */
TEST(InductorCompanionIeqTest, Gear2Formula) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.prevCurrent = 0.10;
  ind.prev2Current = 0.05;
  EXPECT_NEAR(ind.ieq(1e-6, IntegrationMethod::GEAR2), 0.10, TOL);
  ind.prevStep = 1e-6;
  EXPECT_NEAR(ind.ieq(1e-6, IntegrationMethod::GEAR2), 0.35 / 3.0, TOL);
}

/* ----------------------------- InductorCompanion update ----------------------------- */

/**
 * @test Inductor update takes the current each method's formula gives and
 *       shifts the history.
 *
 * 1 mH carrying 0.05 A, then 0.10 A with 0.2 V across it, then 0.1 V, in
 * steps of 1 us.
 */
TEST(InductorCompanionUpdateTest, CurrentIsTheMethodsOwnAndHistoryShifts) {
  struct Case {
    IntegrationMethod method;
    double amperes;
  };
  // Backward Euler: 0.10 + (dt / L) * 0.1.
  // Trapezoidal:    0.10 + (dt / 2L) * (0.1 + 0.2).
  // GEAR2:          (4 * 0.10 - 0.05) / 3 + (2 dt / 3L) * 0.1.
  for (const Case& CASE : {Case{IntegrationMethod::BACKWARD_EULER, 0.1001},
                           Case{IntegrationMethod::TRAPEZOIDAL, 0.10015},
                           Case{IntegrationMethod::GEAR2, 0.35 / 3.0 + 2e-4 / 3.0}}) {
    InductorCompanion ind{};
    ind.inductance = 1e-3;
    ind.prev2Current = 0.05;
    ind.prevCurrent = 0.10;
    ind.voltage = 0.2;
    ind.prevStep = 1e-6;

    ind.update(0.1, 1e-6, CASE.method);

    EXPECT_NEAR(ind.prevCurrent, CASE.amperes, 1e-15) << "method " << static_cast<int>(CASE.method);
    EXPECT_DOUBLE_EQ(ind.prev2Current, 0.10);
    EXPECT_DOUBLE_EQ(ind.voltage, 0.1);
    EXPECT_DOUBLE_EQ(ind.prevStep, 1e-6);
  }
}

/** @test setInitialCurrent leaves a flat history, no voltage and no step recorded */
TEST(InductorCompanionUpdateTest, InitialCurrentIsAStateOfRest) {
  InductorCompanion ind{};
  ind.inductance = 1e-3;
  ind.update(2.0, 1e-6, IntegrationMethod::TRAPEZOIDAL);

  ind.setInitialCurrent(0.05);
  EXPECT_DOUBLE_EQ(ind.prevCurrent, 0.05);
  EXPECT_DOUBLE_EQ(ind.prev2Current, 0.05);
  EXPECT_DOUBLE_EQ(ind.voltage, 0.0);
  EXPECT_DOUBLE_EQ(ind.prevStep, 0.0);
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

/* ----------------------------- Steady Current ----------------------------- */

/** @test An inductor with a steady 0.1 A and no voltage across it keeps 0.1 A under every method */
TEST(InductorCompanionTest, SteadyCurrentIsItsOwnEquivalentSource) {
  for (const IntegrationMethod METHOD :
       {IntegrationMethod::BACKWARD_EULER, IntegrationMethod::TRAPEZOIDAL,
        IntegrationMethod::GEAR2}) {
    InductorCompanion ind;
    ind.inductance = 1e-3;
    ind.prevCurrent = 0.1;
    ind.prev2Current = 0.1;
    ind.voltage = 0.0;
    EXPECT_NEAR(ind.ieq(1e-6, METHOD), 0.1, 1e-15) << "method " << static_cast<int>(METHOD);

    ind.update(0.0, 1e-6, METHOD);
    EXPECT_NEAR(ind.prevCurrent, 0.1, 1e-15) << "method " << static_cast<int>(METHOD);
  }
}
