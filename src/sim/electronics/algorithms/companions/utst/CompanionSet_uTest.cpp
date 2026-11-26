/**
 * @file CompanionSet_uTest.cpp
 * @brief Unit tests for the CompanionSet aggregate.
 *
 * CompanionSet manages a collection of CapacitorCompanion and
 * InductorCompanion records and exposes bulk stamp / update / reset
 * operations used by the transient solver.
 *
 * Notes:
 *  - Tests use a small dense MnaSystem so we can read back individual
 *    matrix / RHS entries and compare against the per-device formulas
 *    already verified in CompanionModels_uTest.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using sim::electronics::algorithms::companions::CompanionSet;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::transient::IntegrationMethod;

namespace {

constexpr double TOL = 1e-12;

// Read G[row, col] from a flat row-major MNA conductance matrix.
double g(const MnaSystem& mna, std::size_t row, std::size_t col) {
  return mna.conductanceMatrix()[row * mna.netCount() + col];
}

double rhs(const MnaSystem& mna, std::size_t row) { return mna.currentVector()[row]; }

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default-constructed CompanionSet is empty */
TEST(CompanionSetDefaultTest, DefaultIsEmpty) {
  const CompanionSet SET{};
  EXPECT_EQ(SET.capacitorCount(), 0u);
  EXPECT_EQ(SET.inductorCount(), 0u);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test addCapacitor stores parameters and returns sequential index */
TEST(CompanionSetTest, AddCapacitorReturnsSequentialIndex) {
  CompanionSet set;
  EXPECT_EQ(set.addCapacitor(/*posNet=*/1, /*negNet=*/2, /*capacitance=*/1e-6), 0u);
  EXPECT_EQ(set.addCapacitor(/*posNet=*/3, /*negNet=*/0, /*capacitance=*/2e-6), 1u);
  EXPECT_EQ(set.capacitorCount(), 2u);

  const auto& C0 = set.capacitor(0);
  EXPECT_EQ(C0.posNet, 1u);
  EXPECT_EQ(C0.negNet, 2u);
  EXPECT_DOUBLE_EQ(C0.capacitance, 1e-6);
}

/** @test addInductor stores parameters and returns sequential index */
TEST(CompanionSetTest, AddInductorReturnsSequentialIndex) {
  CompanionSet set;
  EXPECT_EQ(set.addInductor(/*posNet=*/1, /*negNet=*/2, /*inductance=*/1e-3), 0u);
  EXPECT_EQ(set.addInductor(/*posNet=*/4, /*negNet=*/0, /*inductance=*/5e-3), 1u);
  EXPECT_EQ(set.inductorCount(), 2u);

  const auto& I1 = set.inductor(1);
  EXPECT_EQ(I1.posNet, 4u);
  EXPECT_EQ(I1.negNet, 0u);
  EXPECT_DOUBLE_EQ(I1.inductance, 5e-3);
}

/* ----------------------------- stampAll ----------------------------- */

/** @test stampAll writes capacitor Geq + Ieq into the MNA system */
TEST(CompanionSetTest, StampAllStampsCapacitorBackwardEuler) {
  CompanionSet set;
  set.addCapacitor(/*posNet=*/1, /*negNet=*/0, /*capacitance=*/1e-6);
  set.capacitor(0).prevVoltage = 0.5;

  MnaSystem mna(/*netCount=*/2);
  const double DT = 1e-3;
  set.stampAll(mna, DT, IntegrationMethod::BACKWARD_EULER);

  const double EXPECTED_G = 1e-6 / DT;
  const double EXPECTED_I = 1e-6 * 0.5 / DT;
  EXPECT_NEAR(g(mna, 1, 1), EXPECTED_G, TOL);
  EXPECT_NEAR(rhs(mna, 1), EXPECTED_I, TOL);
}

/** @test stampAll writes inductor Geq + Ieq into the MNA system */
TEST(CompanionSetTest, StampAllStampsInductorBackwardEuler) {
  CompanionSet set;
  set.addInductor(/*posNet=*/1, /*negNet=*/0, /*inductance=*/1e-3);
  set.inductor(0).prevCurrent = 0.05; // 50 mA

  MnaSystem mna(/*netCount=*/2);
  const double DT = 1e-6;
  set.stampAll(mna, DT, IntegrationMethod::BACKWARD_EULER);

  const double EXPECTED_G = DT / 1e-3;
  EXPECT_NEAR(g(mna, 1, 1), EXPECTED_G, TOL);
  // Inductor stamps current into negNet (0 = ground); pos receives -Ieq.
  EXPECT_NEAR(rhs(mna, 1), -0.05, TOL);
}

/* ----------------------------- stampConductanceAll / stampCurrentAll -----------------------------
 */

/** @test Conductance-only and current-only stamps reconstruct full stamp */
TEST(CompanionSetTest, ConductanceAndCurrentDecompositionMatchesFullStamp) {
  CompanionSet set;
  set.addCapacitor(/*posNet=*/1, /*negNet=*/0, /*capacitance=*/1e-6);
  set.addInductor(/*posNet=*/2, /*negNet=*/0, /*inductance=*/1e-3);
  set.capacitor(0).prevVoltage = 0.7;
  set.inductor(0).prevCurrent = 0.02;

  const double DT = 1e-4;
  MnaSystem fullMna(/*netCount=*/3);
  MnaSystem splitMna(/*netCount=*/3);

  set.stampAll(fullMna, DT, IntegrationMethod::BACKWARD_EULER);
  set.stampConductanceAll(splitMna, DT, IntegrationMethod::BACKWARD_EULER);
  set.stampCurrentAll(splitMna, DT, IntegrationMethod::BACKWARD_EULER);

  for (std::size_t i = 1; i < 3; ++i) {
    EXPECT_NEAR(g(fullMna, i, i), g(splitMna, i, i), TOL);
    EXPECT_NEAR(rhs(fullMna, i), rhs(splitMna, i), TOL);
  }
}

/* ----------------------------- Kept Factors ----------------------------- */

#if COMPAT_HAVE_LAPACKE
/**
 * @test Factors kept across steps, with the right-hand side rebuilt each
 *       step, give the voltages of a system stamped and solved anew.
 *
 * 5 V on net 1, 1 kohm to net 2, 1 kohm and 1 uF from net 2 to ground. The
 * voltage source is stamped before the factorization and again, in the same
 * place, for each right-hand side. Factors are kept while the step length
 * and the conductance factor stay: once for backward Euler and the
 * trapezoidal rule, twice for GEAR2 (its first step, then the rest).
 */
TEST(CompanionSetTest, KeptFactorsWithRebuiltRightHandSideMatchOneShotSolves) {
  using sim::electronics::algorithms::mna::MnaFactorizedWorkspace;
  constexpr double DT = 1e-5;
  const auto STAMP_RESISTORS = [](MnaSystem& mna) {
    mna.addConductance(1, 2, 1e-3);
    mna.addConductance(2, 0, 1e-3);
  };
  const auto STAMP_SOURCES = [](MnaSystem& mna) { mna.addVoltageSource(1, 0, 5.0); };

  struct Case {
    IntegrationMethod method;
    int factorizations;
  };
  for (const Case& CASE :
       {Case{IntegrationMethod::BACKWARD_EULER, 1}, Case{IntegrationMethod::TRAPEZOIDAL, 1},
        Case{IntegrationMethod::GEAR2, 2}}) {
    SCOPED_TRACE(static_cast<int>(CASE.method));
    CompanionSet kept;
    CompanionSet anew;
    kept.addCapacitor(/*posNet=*/2, /*negNet=*/0, /*capacitance=*/1e-6);
    anew.addCapacitor(/*posNet=*/2, /*negNet=*/0, /*capacitance=*/1e-6);

    MnaSystem mna(/*netCount=*/3);
    MnaFactorizedWorkspace factors;
    factors.prepare(/*maxSize=*/4);
    std::vector<double> voltages(3);
    std::vector<double> currents(1);
    double factoredStep = 0.0;
    double factoredFactor = 0.0;
    int factorizations = 0;

    for (int step = 0; step < 6; ++step) {
      const double FACTOR = kept.conductanceFactor(DT, CASE.method);
      if (!factors.isFactorized() || DT != factoredStep || FACTOR != factoredFactor) {
        mna.clear();
        STAMP_RESISTORS(mna);
        kept.stampConductanceAll(mna, DT, CASE.method);
        STAMP_SOURCES(mna);
        ASSERT_TRUE(mna.factorize(factors));
        factoredStep = DT;
        factoredFactor = FACTOR;
        ++factorizations;
      }
      mna.clearRHS();
      STAMP_SOURCES(mna);
      kept.stampCurrentAll(mna, DT, CASE.method);
      ASSERT_TRUE(mna.solveFactorized(factors, voltages.data(), currents.data()));
      kept.updateAll(voltages, DT, CASE.method);

      MnaSystem fresh(/*netCount=*/3);
      STAMP_RESISTORS(fresh);
      STAMP_SOURCES(fresh);
      anew.stampAll(fresh, DT, CASE.method);
      const auto RESULT = fresh.solve();
      ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
      anew.updateAll(RESULT.nodeVoltages, DT, CASE.method);

      EXPECT_NEAR(voltages[1], 5.0, TOL) << "step " << step;
      EXPECT_NEAR(voltages[2], RESULT.nodeVoltages[2], TOL) << "step " << step;
      EXPECT_GT(voltages[2], 0.0) << "step " << step;
    }
    EXPECT_EQ(factorizations, CASE.factorizations);
  }
}
#endif

/* ----------------------------- updateAll ----------------------------- */

/**
 * @test updateAll gives every element its voltage from the solved nets and
 *       the method the step was stamped with.
 *
 * 1 uF from net 2 to net 1 and 1 mH from net 3 to ground, from rest; the
 * nets solve to 0.2 V, 0.7 V and 0.1 V after a step of 1 ms.
 */
TEST(CompanionSetTest, UpdateAllAdvancesEveryElementByTheMethod) {
  struct Case {
    IntegrationMethod method;
    double capacitorAmperes;
    double inductorAmperes;
  };
  // Capacitor at 0.5 V from 0: C * 0.5 / dt, and twice that.
  // Inductor with 0.1 V from rest: (dt / L) * 0.1, and half that.
  // GEAR2's first step is backward Euler's.
  for (const Case& CASE : {Case{IntegrationMethod::BACKWARD_EULER, 5e-4, 0.1},
                           Case{IntegrationMethod::TRAPEZOIDAL, 1e-3, 0.05},
                           Case{IntegrationMethod::GEAR2, 5e-4, 0.1}}) {
    CompanionSet set;
    set.addCapacitor(/*posNet=*/2, /*negNet=*/1, /*capacitance=*/1e-6);
    set.addInductor(/*posNet=*/3, /*negNet=*/0, /*inductance=*/1e-3);

    const std::vector<double> V = {0.0, 0.2, 0.7, 0.1};
    set.updateAll(V, 1e-3, CASE.method);

    EXPECT_DOUBLE_EQ(set.capacitor(0).prevVoltage, 0.5);
    EXPECT_NEAR(set.capacitor(0).current, CASE.capacitorAmperes, 1e-15)
        << "method " << static_cast<int>(CASE.method);
    EXPECT_DOUBLE_EQ(set.inductor(0).voltage, 0.1);
    EXPECT_NEAR(set.inductor(0).prevCurrent, CASE.inductorAmperes, 1e-15)
        << "method " << static_cast<int>(CASE.method);
  }
}

/* ----------------------------- conductanceFactor ----------------------------- */

/**
 * @test The factor on C/dt is 1, 2 and, for GEAR2, 1 on a first step and 3/2
 *       from then on until the step length changes.
 */
TEST(CompanionSetTest, ConductanceFactorFollowsTheMethodAndTheStepBefore) {
  CompanionSet set;
  set.addCapacitor(/*posNet=*/1, /*negNet=*/0, /*capacitance=*/1e-6);
  const std::vector<double> V = {0.0, 1.0};

  EXPECT_DOUBLE_EQ(set.conductanceFactor(1e-3, IntegrationMethod::BACKWARD_EULER), 1.0);
  EXPECT_DOUBLE_EQ(set.conductanceFactor(1e-3, IntegrationMethod::TRAPEZOIDAL), 2.0);
  EXPECT_DOUBLE_EQ(set.conductanceFactor(1e-3, IntegrationMethod::GEAR2), 1.0);

  // After a step of 1 ms: 3/2 for another of 1 ms, 4/3 for one of 0.5 ms.
  set.updateAll(V, 1e-3, IntegrationMethod::GEAR2);
  EXPECT_DOUBLE_EQ(set.conductanceFactor(1e-3, IntegrationMethod::GEAR2), 1.5);
  EXPECT_DOUBLE_EQ(set.conductanceFactor(0.5e-3, IntegrationMethod::GEAR2), 4.0 / 3.0);

  // It is the factor of what is stamped.
  EXPECT_DOUBLE_EQ(set.capacitor(0).geq(0.5e-3, IntegrationMethod::GEAR2),
                   (4.0 / 3.0) * 1e-6 / 0.5e-3);

  // A set of inductors answers by its inductors.
  CompanionSet coils;
  coils.addInductor(/*posNet=*/1, /*negNet=*/0, /*inductance=*/1e-3);
  coils.updateAll(V, 1e-3, IntegrationMethod::GEAR2);
  EXPECT_DOUBLE_EQ(coils.conductanceFactor(0.5e-3, IntegrationMethod::GEAR2), 4.0 / 3.0);
}

/* ----------------------------- reset ----------------------------- */

/** @test reset zeros every companion's history */
TEST(CompanionSetTest, ResetZerosAllHistory) {
  CompanionSet set;
  set.addCapacitor(/*posNet=*/1, /*negNet=*/0, /*capacitance=*/1e-6);
  set.addInductor(/*posNet=*/2, /*negNet=*/0, /*inductance=*/1e-3);
  set.capacitor(0).prevVoltage = 1.0;
  set.capacitor(0).prev2Voltage = 0.5;
  set.capacitor(0).current = 1e-3;
  set.inductor(0).prevCurrent = 0.05;
  set.inductor(0).prev2Current = 0.02;
  set.inductor(0).voltage = 0.1;
  set.capacitor(0).prevStep = 1e-3;
  set.inductor(0).prevStep = 1e-3;

  set.reset();

  EXPECT_DOUBLE_EQ(set.capacitor(0).prevVoltage, 0.0);
  EXPECT_DOUBLE_EQ(set.capacitor(0).prev2Voltage, 0.0);
  EXPECT_DOUBLE_EQ(set.capacitor(0).current, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).prevCurrent, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).prev2Current, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).voltage, 0.0);
  EXPECT_DOUBLE_EQ(set.capacitor(0).prevStep, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).prevStep, 0.0);
}

/* ----------------------------- initializeFromDC ----------------------------- */

/** @test initializeFromDC leaves each capacitor at rest at the voltage between its nets */
TEST(CompanionSetTest, InitializeFromDCSeedsCapacitorVoltage) {
  CompanionSet set;
  set.addCapacitor(/*posNet=*/2, /*negNet=*/1, /*capacitance=*/1e-6);
  set.capacitor(0).current = 1e-4;     // what an earlier run left
  set.capacitor(0).prev2Voltage = 0.3; // likewise
  set.capacitor(0).prevStep = 1e-3;    // likewise

  const std::vector<double> V_DC = {0.0, 0.7, 1.5}; // node 0=GND, 1=0.7V, 2=1.5V
  set.initializeFromDC(V_DC);

  // V_pos - V_neg = 1.5 - 0.7 = 0.8, with a flat history and no current.
  EXPECT_DOUBLE_EQ(set.capacitor(0).prevVoltage, 0.8);
  EXPECT_DOUBLE_EQ(set.capacitor(0).prev2Voltage, 0.8);
  EXPECT_DOUBLE_EQ(set.capacitor(0).current, 0.0);
  EXPECT_DOUBLE_EQ(set.capacitor(0).prevStep, 0.0);
}

/** @test initializeFromDC gives each inductor the current its stand-in conductance carried */
TEST(CompanionSetTest, InitializeFromDCSeedsInductorCurrent) {
  CompanionSet set;
  set.addInductor(/*posNet=*/2, /*negNet=*/1, /*inductance=*/1e-3);
  set.inductor(0).prevCurrent = 4.74e-3; // what an earlier run left
  set.inductor(0).voltage = 0.2;

  // 50 uV across a stand-in of 1000 S: 50 mA.
  const std::vector<double> V_DC = {0.0, 1.0, 1.00005};
  set.initializeFromDC(V_DC, 1000.0);
  EXPECT_NEAR(set.inductor(0).prevCurrent, 50e-3, 1e-12);
  EXPECT_NEAR(set.inductor(0).prev2Current, 50e-3, 1e-12);
  EXPECT_DOUBLE_EQ(set.inductor(0).voltage, 0.0);
  EXPECT_DOUBLE_EQ(set.inductor(0).prevStep, 0.0);

  // Without a stand-in conductance the inductor starts without current.
  set.initializeFromDC(V_DC);
  EXPECT_DOUBLE_EQ(set.inductor(0).prevCurrent, 0.0);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test stampAll is deterministic across repeated calls */
TEST(CompanionSetDeterminismTest, StampAllIsRepeatable) {
  CompanionSet set;
  set.addCapacitor(/*posNet=*/1, /*negNet=*/0, /*capacitance=*/2e-9);
  set.capacitor(0).prevVoltage = 0.3;

  const double DT = 1e-6;
  MnaSystem first(/*netCount=*/2);
  set.stampAll(first, DT, IntegrationMethod::TRAPEZOIDAL);

  for (int i = 0; i < 5; ++i) {
    MnaSystem repeat(/*netCount=*/2);
    set.stampAll(repeat, DT, IntegrationMethod::TRAPEZOIDAL);
    EXPECT_DOUBLE_EQ(g(first, 1, 1), g(repeat, 1, 1));
    EXPECT_DOUBLE_EQ(rhs(first, 1), rhs(repeat, 1));
  }
}
