/**
 * @file TransientStepping_uTest.cpp
 * @brief How TransientSolver::run() divides a span into steps, and what its
 *        stepping paths do at their limits.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

using sim::electronics::algorithms::mna::MnaSystem;
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
 */
TEST(TransientSteppingTest, WholeSpanTakesItsNumberOfSteps) {
  struct Case {
    double tEnd;
    double tStep;
    std::size_t steps;
  };
  for (const Case& CASE : {Case{1e-6, 1e-9, 1000}, Case{1.0, 0.1, 10}, Case{5e-3, 1e-5, 500}}) {
    SCOPED_TRACE(CASE.tStep);
    const TransientResult RESULT = runRc(CASE.tEnd, CASE.tStep);
    ASSERT_TRUE(RESULT.success) << RESULT.errorMessage;
    EXPECT_EQ(RESULT.stepsTaken, CASE.steps);
    ASSERT_EQ(RESULT.history.size(), CASE.steps + 1);
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
void buildManySources(TransientSolver& solver) {
  solver.companions().addCapacitor(MANY_SOURCES + 1, 0, 1e-6);
  solver.setStampCallback([](MnaSystem& mna, double /*seconds*/) {
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
    TransientSolver solver(MANY_SOURCES + 2);
    buildManySources(solver);
    solver.setCachedLU(KEEPING == Keeping::DENSE);
    solver.setDualLU(KEEPING == Keeping::DUAL);

    TransientState state;
    state.resize(MANY_SOURCES + 2, 0);
    for (int k = 0; k < 2000; ++k) {
      const TransientStatus STATUS =
          (KEEPING == Keeping::DUAL) ? solver.stepDual(1e-5, 0, state) : solver.step(1e-5, state);
      ASSERT_EQ(STATUS, TransientStatus::SUCCESS) << "step " << k;
    }

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
