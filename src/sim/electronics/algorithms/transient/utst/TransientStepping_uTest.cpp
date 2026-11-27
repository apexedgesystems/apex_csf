/**
 * @file TransientStepping_uTest.cpp
 * @brief How TransientSolver::run() divides a span into steps, and what its
 *        stepping paths do at their limits.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::MnaSystemSparse;
using sim::electronics::algorithms::transient::TransientConfig;
using sim::electronics::algorithms::transient::TransientResult;
using sim::electronics::algorithms::transient::TransientSolver;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;

namespace {

/** @brief 5 V through 1 kohm into 1 uF: nets 0 ground, 1 source, 2 capacitor. */
void buildRc(TransientSolver& solver) {
  solver.companions().addCapacitor(2, 0, 1e-6);
  solver.setStampCallback([](MnaSystem& mna, double /*seconds*/) {
    mna.addVoltageSource(1, 0, 5.0);
    mna.addConductance(1, 2, 1e-3);
  });
}

/** @brief Run the RC over a span from zero, with history and no operating point. */
TransientResult runRc(double tEnd, double tStep) {
  TransientSolver solver(3);
  buildRc(solver);
  TransientConfig config;
  config.tEnd = tEnd;
  config.tStep = tStep;
  config.dcOpPoint = false;
  return solver.run(config, true);
}

} // namespace

/* ----------------------------- Step Count ----------------------------- */

/**
 * @test A span of a whole number of steps takes that number, and ends on the
 *       end time.
 *
 * 1 us in steps of 1 ns is a thousand steps, and 1 s in steps of 0.1 s is
 * ten, though neither sum of steps lands on the end exactly in binary.
 * 1.3 us in steps of 0.1 us leaves 2e-22 s over after thirteen steps in
 * binary: that is not a fourteenth.
 */
TEST(TransientSteppingTest, WholeSpanTakesItsNumberOfSteps) {
  struct Case {
    double tEnd;
    double tStep;
    std::size_t steps;
  };
  for (const Case& CASE : {Case{1e-6, 1e-9, 1000}, Case{1.0, 0.1, 10}, Case{5e-3, 1e-5, 500},
                           Case{1.3e-6, 1e-7, 13}}) {
    SCOPED_TRACE(CASE.tStep);
    const TransientResult RESULT = runRc(CASE.tEnd, CASE.tStep);
    ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
    EXPECT_EQ(RESULT.stepsTaken, CASE.steps);
    if (RESULT.history.size() != CASE.steps + 1) {
      ADD_FAILURE() << RESULT.history.size() << " samples for " << CASE.steps << " steps";
      continue;
    }
    EXPECT_EQ(RESULT.finalTime, CASE.tEnd);
    EXPECT_EQ(RESULT.history.back().time, CASE.tEnd);

    // Every sample is a step after the one before it: none is a sliver.
    for (std::size_t k = 1; k < RESULT.history.size(); ++k) {
      EXPECT_NEAR(RESULT.history[k].time - RESULT.history[k - 1].time, CASE.tStep,
                  1e-9 * CASE.tStep)
          << "sample " << k;
    }
  }
}

/** @test A span that is not a whole number of steps ends with one shorter step. */
TEST(TransientSteppingTest, PartialSpanEndsWithAShorterStep) {
  const TransientResult RESULT = runRc(0.75e-3, 1e-4);
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
  EXPECT_EQ(RESULT.stepsTaken, 8U);
  ASSERT_EQ(RESULT.history.size(), 9U);
  EXPECT_EQ(RESULT.history.back().time, 0.75e-3);
  EXPECT_NEAR(RESULT.history[7].time, 0.7e-3, 1e-15);
  EXPECT_NEAR(RESULT.history[8].time - RESULT.history[7].time, 0.5e-4, 1e-15);

  // The last step is taken at its own length: the capacitor is where seven
  // steps of 0.1 ms and one of 0.05 ms leave it.
  TransientSolver byHand(3);
  buildRc(byHand);
  TransientState state;
  state.resize(3, 1);
  for (int k = 0; k < 7; ++k) {
    ASSERT_EQ(byHand.step(1e-4, state), TransientStatus::SUCCESS);
  }
  ASSERT_EQ(byHand.step(0.5e-4, state), TransientStatus::SUCCESS);
  EXPECT_NEAR(RESULT.finalState.voltage(2), state.voltage(2), 1e-12);
  EXPECT_GT(std::fabs(RESULT.finalState.voltage(2) - RESULT.history[7].voltage(2)), 1e-3);
}

/** @test stepCount() is the number of steps run() takes. */
TEST(TransientSteppingTest, StepCountIsWhatRunTakes) {
  TransientConfig config;
  EXPECT_EQ(config.stepCount(), 1000U);
  // 1e-6 / 1e-9 is a hair under a thousand in binary: a thousand whole steps all the same.
  EXPECT_EQ(config.wholeSteps(), 1000U);
  EXPECT_EQ(config.lastStep(), 0.0);

  config.tEnd = 0.7;
  config.tStep = 0.1;
  EXPECT_EQ(config.stepCount(), 7U);
  EXPECT_EQ(runRc(0.7, 0.1).stepsTaken, 7U);

  config.tEnd = 0.75;
  EXPECT_EQ(config.stepCount(), 8U);

  // A span shorter than one step is one shorter step.
  config.tEnd = 0.04;
  EXPECT_EQ(config.stepCount(), 1U);
}

/**
 * @test The whole steps and the last step are read from one count, so a
 *       span that is whole within the tolerance has no last step, at any
 *       count.
 *
 * 0.54 s in steps of 0.1 us is 5,400,000 steps and 6e-10 of one more as the
 * doubles stand; a rest worked out again from a product of 5,400,000 and
 * the step comes to 1.1e-16 s, which is not a step. Around a thousand steps
 * of 1 s, half a billionth of a step either way is whole and ten
 * billionths either way is not. At 2^40 steps a double moves by 1.2e-4 of
 * a step, and one such move either way is still the whole count.
 */
TEST(TransientSteppingTest, WholeAndLastStepAreReadFromOneCount) {
  const auto PLAN = [](double tEnd, double tStep) {
    TransientConfig config;
    config.tEnd = tEnd;
    config.tStep = tStep;
    return config;
  };

  const TransientConfig FINE = PLAN(0.54, 1e-7);
  EXPECT_EQ(FINE.wholeSteps(), 5400000U);
  EXPECT_EQ(FINE.lastStep(), 0.0);
  EXPECT_EQ(FINE.stepCount(), 5400000U);

  // Within the tolerance of a thousand, on either side: a thousand.
  for (const double T_END : {1000.0 + 5e-10, 1000.0 - 5e-10}) {
    SCOPED_TRACE(T_END - 1000.0);
    EXPECT_EQ(PLAN(T_END, 1.0).wholeSteps(), 1000U);
    EXPECT_EQ(PLAN(T_END, 1.0).lastStep(), 0.0);
  }
  // Beyond it above: a thousand and a last step of what is over.
  EXPECT_EQ(PLAN(1000.0 + 1e-8, 1.0).wholeSteps(), 1000U);
  EXPECT_NEAR(PLAN(1000.0 + 1e-8, 1.0).lastStep(), 1e-8, 1e-12);
  EXPECT_EQ(PLAN(1000.0 + 1e-8, 1.0).stepCount(), 1001U);
  // Beyond it below: 999 and a last step a hair short of a whole one.
  EXPECT_EQ(PLAN(1000.0 - 1e-8, 1.0).wholeSteps(), 999U);
  EXPECT_NEAR(PLAN(1000.0 - 1e-8, 1.0).lastStep(), 1.0 - 1e-8, 1e-12);

  // A count where a double no longer resolves a billionth of a step.
  const double MANY = 1099511627776.0; // 2^40
  for (const double T_END : {MANY, std::nextafter(MANY, 2.0 * MANY), std::nextafter(MANY, 0.0)}) {
    SCOPED_TRACE(T_END - MANY);
    EXPECT_EQ(PLAN(T_END, 1.0).wholeSteps(), 1099511627776U);
    EXPECT_EQ(PLAN(T_END, 1.0).lastStep(), 0.0);
  }
  EXPECT_EQ(PLAN(MANY + 0.5, 1.0).wholeSteps(), 1099511627776U);
  EXPECT_EQ(PLAN(MANY + 0.5, 1.0).lastStep(), 0.5);
}

/** @test A configuration with no countable number of steps is refused. */
TEST(TransientSteppingTest, UncountableSpanIsRefused) {
  for (const double T_STEP : {0.0, -1e-9, std::numeric_limits<double>::quiet_NaN(), 1e-300}) {
    SCOPED_TRACE(T_STEP);
    TransientConfig config;
    config.tStep = T_STEP;
    EXPECT_EQ(config.stepCount(), 0U);
    EXPECT_FALSE(runRc(1e-6, T_STEP).success);
  }
  EXPECT_FALSE(runRc(std::numeric_limits<double>::quiet_NaN(), 1e-9).success);
  EXPECT_FALSE(runRc(-1.0, 1e-9).success);
}

/* ----------------------------- Voltage Sources ----------------------------- */

namespace {

constexpr std::size_t MANY_SOURCES = 17;

/**
 * @brief Seventeen voltage sources, each on its own net with 1 kohm to
 *        ground, and an RC on the first: nets 1 to 17 the sources (k volts on
 *        net k), net 18 the capacitor, charged from net 1 through 1 kohm.
 */
void buildManySources(TransientSolver& solver, std::size_t* stamps = nullptr) {
  solver.companions().addCapacitor(MANY_SOURCES + 1, 0, 1e-6);
  solver.setStampCallback([stamps](MnaSystem& mna, double /*seconds*/) {
    if (stamps != nullptr) {
      ++*stamps;
    }
    for (std::uint32_t k = 1; k <= MANY_SOURCES; ++k) {
      mna.addVoltageSource(k, 0, static_cast<double>(k));
      mna.addConductance(k, 0, 1e-3);
    }
    mna.addConductance(1, MANY_SOURCES + 1, 1e-3);
  });
}

} // namespace

/**
 * @test A circuit with more voltage sources than the solver set room aside
 *       for is stepped all the same, by the plain step and by both ways of
 *       keeping factors.
 *
 * The capacitor charges to the first source's 1 V with a time constant of
 * 1 ms; after twenty of them the run is at the operating point computeDC
 * gives.
 */
TEST(TransientSteppingTest, MoreVoltageSourcesThanTheRoomSetAside) {
  enum class Keeping { NONE, DENSE, DUAL };
  for (const Keeping KEEPING : {Keeping::NONE, Keeping::DENSE, Keeping::DUAL}) {
    SCOPED_TRACE(static_cast<int>(KEEPING));
    std::size_t stamps = 0;
    TransientSolver solver(MANY_SOURCES + 2);
    buildManySources(solver, &stamps);
    solver.setCachedLU(KEEPING == Keeping::DENSE);
    solver.setDualLU(KEEPING == Keeping::DUAL);

    TransientState state;
    state.resize(MANY_SOURCES + 2, 0);
    for (int k = 0; k < 2000; ++k) {
      const TransientStatus STATUS =
          (KEEPING == Keeping::DUAL) ? solver.stepDual(1e-5, 0, state) : solver.step(1e-5, state);
      ASSERT_EQ(STATUS, TransientStatus::SUCCESS) << "step " << k;
    }
    // One stamp a step, and one more at the first: the step that found the
    // room too small stamps again, and a factorization that is kept is
    // stamped for once.
    EXPECT_EQ(stamps, 2001U);

    TransientSolver atRest(MANY_SOURCES + 2);
    buildManySources(atRest);
    TransientState rest;
    ASSERT_EQ(atRest.computeDC(rest), TransientStatus::SUCCESS);
    EXPECT_NEAR(rest.voltage(MANY_SOURCES + 1), 1.0, 1e-12);
    EXPECT_NEAR(state.voltage(MANY_SOURCES + 1), rest.voltage(MANY_SOURCES + 1), 1e-8);
    for (std::size_t k = 1; k <= MANY_SOURCES; ++k) {
      EXPECT_NEAR(state.voltage(k), static_cast<double>(k), 1e-12) << "net " << k;
    }
  }
}

/** @test run() on that circuit succeeds. */
TEST(TransientSteppingTest, RunWithMoreVoltageSourcesThanTheRoomSetAside) {
  TransientSolver solver(MANY_SOURCES + 2);
  buildManySources(solver);
  TransientConfig config;
  config.tEnd = 1e-3;
  config.tStep = 1e-5;
  const TransientResult RESULT = solver.run(config, false);
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
  EXPECT_EQ(RESULT.stepsTaken, 100U);
  EXPECT_NEAR(RESULT.finalState.voltage(MANY_SOURCES + 1), 1.0, 1e-9);
}

/* ----------------------------- Two-State Factors ----------------------------- */

namespace {

/** @brief The RC with a load on the capacitor that a flag switches in. */
void buildLoadedRc(TransientSolver& solver, const bool* loaded, std::size_t* stamps) {
  solver.companions().addCapacitor(2, 0, 1e-6);
  solver.setStampCallback([loaded, stamps](MnaSystem& mna, double /*seconds*/) {
    ++*stamps;
    mna.addVoltageSource(1, 0, 5.0);
    mna.addConductance(1, 2, 1e-3);
    if (*loaded) {
      mna.addConductance(2, 0, 1e-3);
    }
  });
}

/** @brief A solver stepped by stepDual beside one stepped plainly, on the same circuit. */
struct DualBesidePlain {
  bool loaded = false;
  std::size_t dualStamps = 0;  ///< Stamp calls of the two-state solver.
  std::size_t plainStamps = 0; ///< Stamp calls of the plain solver.
  TransientSolver dual{3};
  TransientSolver plain{3};
  TransientState dualState;
  TransientState plainState;

  DualBesidePlain() {
    buildLoadedRc(dual, &loaded, &dualStamps);
    buildLoadedRc(plain, &loaded, &plainStamps);
    dual.setDualLU(true);
    dualState.resize(3, 1);
    plainState.resize(3, 1);
  }

  /** @brief One step in each solver, in the given state; the two capacitor voltages equal. */
  void step(double dt, int state) {
    ASSERT_EQ(dual.stepDual(dt, state, dualState), TransientStatus::SUCCESS);
    ASSERT_EQ(plain.step(dt, plainState), TransientStatus::SUCCESS);
    ASSERT_NEAR(dualState.voltage(2), plainState.voltage(2), 1e-12)
        << "state " << state << ", step of " << dt << " s, at " << plainState.time << " s";
  }
};

} // namespace

/**
 * @test Each state's kept factors serve the step length they were built for.
 *
 * Both states stamp the same circuit. After steps of 10 us in each, steps of
 * 100 us follow: the first state factorizes for them, and the second has its
 * own factors of 10 us still.
 */
TEST(TransientSteppingTest, EachDualStateKeepsItsOwnStepLength) {
  DualBesidePlain pair;
  for (int k = 0; k < 4; ++k) {
    pair.step(10e-6, k % 2);
    if (::testing::Test::HasFatalFailure()) {
      return;
    }
  }
  for (int k = 0; k < 6; ++k) {
    pair.step(100e-6, k % 2);
    if (::testing::Test::HasFatalFailure()) {
      return;
    }
  }
  // A state stamps twice when it factorizes and once when its factors
  // serve: each state factorized once for 10 us and once for 100 us, over
  // ten steps.
  EXPECT_EQ(pair.dualStamps, 14U);
}

/** @test invalidateCache() drops both states' factors: a changed circuit is solved as it is. */
TEST(TransientSteppingTest, InvalidateCacheDropsTheDualFactors) {
  DualBesidePlain pair;
  for (int k = 0; k < 4; ++k) {
    pair.step(10e-6, k % 2);
    if (::testing::Test::HasFatalFailure()) {
      return;
    }
  }
  pair.loaded = true;
  pair.dual.invalidateCache();
  for (int k = 0; k < 200; ++k) {
    pair.step(10e-6, k % 2);
    if (::testing::Test::HasFatalFailure()) {
      return;
    }
  }
  // Half the supply across the equal resistors, the time constant halved.
  EXPECT_NEAR(pair.dualState.voltage(2), 2.5, 0.1);
}

/* ----------------------------- Newton Convergence ----------------------------- */

namespace {

/**
 * @brief A nonlinear step with no fixed point: net 1 has 1 S to ground and a
 *        source of 0.5 A that turns against whatever sign the iterate has,
 *        so the solution is 0.5 V and -0.5 V by turns.
 */
void buildNoFixedPoint(TransientSolver& solver, std::size_t* stampCalls) {
  solver.setSparse(true);
  solver.setAlwaysReanalyze(true);
  solver.setStatefulStampCallbackSparse(
      [stampCalls](MnaSystemSparse& mna, double /*seconds*/, const std::vector<double>& iterate) {
        ++*stampCalls;
        mna.addConductance(1, 0, 1.0);
        mna.addCurrent(1, 0, iterate[1] > 0.0 ? -0.5 : 0.5);
      });
}

/**
 * @brief A nonlinear step that closes on its solution by halves: 1 S to
 *        ground and a source of half the iterate's voltage plus 1 A, so each
 *        iterate is 1 + v/2 and the fixed point is 2 V.
 */
void buildContraction(TransientSolver& solver) {
  solver.setSparse(true);
  solver.setAlwaysReanalyze(true);
  solver.setStatefulStampCallbackSparse(
      [](MnaSystemSparse& mna, double /*seconds*/, const std::vector<double>& iterate) {
        mna.addConductance(1, 0, 1.0);
        mna.addCurrent(1, 0, 1.0 + 0.5 * iterate[1]);
      });
}

/** @brief Iterations the contraction takes from 0 V until an iterate moves by no more than `volts`.
 */
std::size_t contractionIterations(double volts) {
  double v = 0.0;
  for (std::size_t k = 1;; ++k) {
    const double NEXT = 1.0 + 0.5 * v;
    if (std::fabs(NEXT - v) <= volts) {
      return k;
    }
    v = NEXT;
  }
}

} // namespace

/**
 * @test A Newton step that reaches its iteration limit without converging is
 *       counted, and by default accepted.
 */
TEST(TransientSteppingTest, UnconvergedNewtonStepIsCounted) {
  std::size_t stampCalls = 0;
  TransientSolver solver(2);
  buildNoFixedPoint(solver, &stampCalls);
  TransientState state;
  state.resize(2, 0);

  EXPECT_EQ(solver.newtonIterationCount(), 0U);
  EXPECT_EQ(solver.unconvergedStepCount(), 0U);
  ASSERT_EQ(solver.step(1e-6, state), TransientStatus::SUCCESS);
  EXPECT_EQ(stampCalls, 10U);
  EXPECT_EQ(solver.newtonIterationCount(), 10U);
  EXPECT_EQ(solver.unconvergedStepCount(), 1U);
  EXPECT_DOUBLE_EQ(solver.time(), 1e-6);

  solver.reset();
  EXPECT_EQ(solver.newtonIterationCount(), 0U);
  EXPECT_EQ(solver.unconvergedStepCount(), 0U);
}

/**
 * @test With convergence required, a step that does not converge is not
 *       taken: ERROR_CONVERGENCE, with time, node voltages and companions as
 *       they were.
 */
TEST(TransientSteppingTest, RequiredConvergenceRefusesTheStep) {
  std::size_t stampCalls = 0;
  TransientSolver solver(3);
  buildNoFixedPoint(solver, &stampCalls);
  solver.companions().addCapacitor(2, 0, 1e-6);
  solver.companions().capacitor(0).setInitialVoltage(0.25);
  solver.setRequireConvergence(true);
  TransientState state;
  state.resize(3, 0);
  state.nodeVoltages[1] = 0.125;

  EXPECT_EQ(solver.step(1e-6, state), TransientStatus::ERROR_CONVERGENCE);
  EXPECT_EQ(solver.unconvergedStepCount(), 1U);
  EXPECT_EQ(solver.time(), 0.0);
  EXPECT_EQ(state.voltage(1), 0.0) << "the voltages the solver held before the step";
  EXPECT_EQ(solver.companions().capacitor(0).prevVoltage, 0.25);
  EXPECT_EQ(solver.companions().capacitor(0).prevStep, 0.0);

  // run() ends at such a step and says why.
  TransientConfig config;
  config.tEnd = 1e-5;
  config.tStep = 1e-6;
  config.dcOpPoint = false;
  const TransientResult RESULT = solver.run(config, false);
  EXPECT_FALSE(RESULT.success);
  EXPECT_NE(RESULT.errorMessage.find("did not converge"), std::string::npos) << RESULT.errorMessage;
  EXPECT_EQ(RESULT.stepsTaken, 0U);
  EXPECT_EQ(RESULT.unconvergedSteps, 1U);
}

/** @test The iteration limit and the two tolerances are settings of the solver. */
TEST(TransientSteppingTest, NewtonLimitAndTolerancesAreSettings) {
  // The limit: three iterations of the step with no fixed point.
  {
    std::size_t stampCalls = 0;
    TransientSolver solver(2);
    buildNoFixedPoint(solver, &stampCalls);
    solver.setNewtonIterationLimit(3);
    TransientState state;
    state.resize(2, 0);
    ASSERT_EQ(solver.step(1e-6, state), TransientStatus::SUCCESS);
    EXPECT_EQ(stampCalls, 3U);
    EXPECT_EQ(solver.unconvergedStepCount(), 1U);
  }

  // The absolute tolerance alone: the contraction stops where its halving
  // step falls under it.
  for (const double VOLTS : {1e-2, 1e-6}) {
    SCOPED_TRACE(VOLTS);
    TransientSolver solver(2);
    buildContraction(solver);
    solver.setNewtonIterationLimit(60);
    solver.setNewtonTolerances(VOLTS, 0.0);
    TransientState state;
    state.resize(2, 0);
    ASSERT_EQ(solver.step(1e-6, state), TransientStatus::SUCCESS);
    EXPECT_EQ(solver.newtonIterationCount(), contractionIterations(VOLTS));
    EXPECT_EQ(solver.unconvergedStepCount(), 0U);
    EXPECT_NEAR(state.voltage(1), 2.0, 2.0 * VOLTS);
  }

  // The relative tolerance alone: 10 % of about 2 V is reached sooner than
  // one part in a million.
  std::size_t loose = 0;
  std::size_t tight = 0;
  for (const double RELATIVE : {1e-1, 1e-6}) {
    TransientSolver solver(2);
    buildContraction(solver);
    solver.setNewtonIterationLimit(60);
    solver.setNewtonTolerances(0.0, RELATIVE);
    TransientState state;
    state.resize(2, 0);
    ASSERT_EQ(solver.step(1e-6, state), TransientStatus::SUCCESS);
    EXPECT_EQ(solver.unconvergedStepCount(), 0U);
    (RELATIVE > 1e-3 ? loose : tight) = solver.newtonIterationCount();
  }
  EXPECT_LT(loose, tight);
  EXPECT_GT(loose, 0U);
}

/** @test run() reports the Newton iterations and the unconverged steps it took. */
TEST(TransientSteppingTest, RunReportsNewtonIterations) {
  std::size_t stampCalls = 0;
  TransientSolver solver(2);
  buildNoFixedPoint(solver, &stampCalls);
  TransientConfig config;
  config.tEnd = 4e-6;
  config.tStep = 1e-6;
  config.dcOpPoint = false;
  const TransientResult RESULT = solver.run(config, false);
  ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
  EXPECT_EQ(RESULT.stepsTaken, 4U);
  EXPECT_EQ(RESULT.nonlinearIters, 40U);
  EXPECT_EQ(RESULT.unconvergedSteps, 4U);

  // A second run reports its own, not the solver's running totals.
  const TransientResult AGAIN = solver.run(config, false);
  EXPECT_EQ(AGAIN.nonlinearIters, 40U);
  EXPECT_EQ(AGAIN.unconvergedSteps, 4U);
}

/**
 * @test A refused step leaves the solver's previous voltages and the node
 *       voltages as they were accepted, also where a pre-batch callback
 *       wrote to prevVoltages() on the way, and the step can be tried again.
 */
TEST(TransientSteppingTest, RefusedStepUndoesWhatThePreBatchCallbackWrote) {
  std::size_t stampCalls = 0;
  TransientSolver solver(2);
  buildNoFixedPoint(solver, &stampCalls);
  solver.setRequireConvergence(true);
  solver.setNrPreBatchCallback([&solver](double /*dt*/) { solver.prevVoltages()[1] = 0.125; });
  TransientState state;
  state.resize(2, 0);

  for (int attempt = 0; attempt < 2; ++attempt) {
    SCOPED_TRACE(attempt);
    EXPECT_EQ(solver.step(1e-6, state), TransientStatus::ERROR_CONVERGENCE);
    EXPECT_EQ(solver.prevVoltages()[1], 0.0);
    EXPECT_EQ(state.voltage(1), 0.0);
    EXPECT_EQ(solver.time(), 0.0);
  }
  EXPECT_EQ(solver.unconvergedStepCount(), 2U);

  // The same step accepted: the callback's write is what the solver goes on from.
  solver.setRequireConvergence(false);
  EXPECT_EQ(solver.step(1e-6, state), TransientStatus::SUCCESS);
  EXPECT_DOUBLE_EQ(solver.time(), 1e-6);
  EXPECT_NE(solver.prevVoltages()[1], 0.0);
}

/**
 * @test Two-state factors built with one voltage source are not used for a
 *       circuit with two: the step is taken through a new factorization.
 *
 * Net 3 hangs on the capacitor's net through 1 kohm; a flag puts 2 V on it.
 * invalidateCache() is not called.
 */
TEST(TransientSteppingTest, DualFactorsOfAnotherSourceCountAreNotUsed) {
  bool second = false;
  const auto BUILD = [&second](TransientSolver& solver) {
    solver.companions().addCapacitor(2, 0, 1e-6);
    solver.setStampCallback([&second](MnaSystem& mna, double /*seconds*/) {
      mna.addVoltageSource(1, 0, 5.0);
      mna.addConductance(1, 2, 1e-3);
      mna.addConductance(3, 2, 1e-3);
      if (second) {
        mna.addVoltageSource(3, 0, 2.0);
      }
    });
  };
  TransientSolver dual(4);
  TransientSolver plain(4);
  BUILD(dual);
  BUILD(plain);
  dual.setDualLU(true);
  TransientState dualState;
  TransientState plainState;
  dualState.resize(4, 2);
  plainState.resize(4, 2);

  for (int k = 0; k < 8; ++k) {
    second = k >= 4;
    ASSERT_EQ(dual.stepDual(10e-6, k % 2, dualState), TransientStatus::SUCCESS) << "step " << k;
    ASSERT_EQ(plain.step(10e-6, plainState), TransientStatus::SUCCESS) << "step " << k;
    ASSERT_NEAR(dualState.voltage(2), plainState.voltage(2), 1e-12) << "step " << k;
    ASSERT_NEAR(dualState.voltage(3), plainState.voltage(3), 1e-12) << "step " << k;
  }
  EXPECT_NEAR(dualState.voltage(3), 2.0, 1e-12);
}
