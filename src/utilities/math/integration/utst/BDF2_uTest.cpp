/**
 * @file BDF2_uTest.cpp
 * @brief Unit tests for apex::math::integration::BDF2.
 *
 * Notes:
 *  - Step values are checked against the formulas solved by hand for a linear
 *    problem, and the order against an analytic solution.
 *  - The options supply the method's Newton matrix, I - (2/3)*dt*df/dx, on
 *    every step, the first included.
 */

#include "src/utilities/math/integration/inc/BDF2.hpp"

#include "src/utilities/math/integration/utst/support/ConvergenceOrder.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using apex::math::integration::BDF2;
using apex::math::integration::BDF2Options;
using apex::math::integration::Status;
using apex::math::integration::test::observedOrder;

namespace {

constexpr auto SUCCESS = static_cast<std::uint8_t>(Status::SUCCESS);
constexpr double TWO_THIRDS = 2.0 / 3.0;

/** Options for y' = a*y at step dt. One Newton iteration solves a linear problem exactly. */
BDF2Options<double> linearOptions(double a, double dt) {
  BDF2Options<double> opts;
  opts.computeJacobian = [a, dt](const double&, double) { return 1.0 - TWO_THIRDS * dt * a; };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
  return opts;
}

/** The start rule solved for y' = a*y: x1 = x0 + dt*((1/3)*a*x0 + (2/3)*a*x1). */
double startRule(double x0, double a, double dt) {
  return x0 * (1.0 + a * dt / 3.0) / (1.0 - TWO_THIRDS * a * dt);
}

/** The two-step formula solved for y' = a*y. */
double twoStepRule(double xN, double xPrev, double a, double dt) {
  return ((4.0 / 3.0) * xN - (1.0 / 3.0) * xPrev) / (1.0 - TWO_THIRDS * a * dt);
}

/** Error at t = 1 of y' = cos(t)*y, y(0) = 1, whose solution is exp(sin(t)). */
double errorAtOne(int steps) {
  const double dt = 1.0 / steps;
  BDF2Options<double> opts;
  opts.maxIterations = 50;
  opts.computeJacobian = [dt](const double&, double t) {
    return 1.0 - TWO_THIRDS * dt * std::cos(t);
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
  opts.converged = [](const double& delta, const double&) { return std::fabs(delta) < 1e-14; };

  BDF2<double> integrator;
  integrator.initialize([](const double& y, double t) { return std::cos(t) * y; }, 1.0, 0.0, opts);
  double y = 1.0;
  for (int i = 0; i < steps; ++i) {
    integrator.step(y, dt, opts);
  }
  return std::fabs(y - std::exp(std::sin(1.0)));
}

} // namespace

/* ----------------------------- Initialization ----------------------------- */

/** @test Sets the starting time and evaluates nothing */
TEST(BDF2Test, InitializeSetsTimeAndEvaluatesNothing) {
  BDF2<double> integrator;
  const double t0 = 3.14;

  ASSERT_EQ(integrator.initialize([](const double&, double) { return 0.0; }, 0.0, t0,
                                  BDF2Options<double>{}),
            SUCCESS);

  EXPECT_EQ(integrator.stats().functionEvals, 0U);
  EXPECT_EQ(integrator.stats().jacobianEvals, 0U);
  EXPECT_EQ(integrator.stats().linearSolveCalls, 0U);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/* -------------------------------- Stepping -------------------------------- */

/** @test Takes the first step with the one-step start rule */
TEST(BDF2Test, FirstStepIsTheStartRule) {
  const double a = -1.0;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  BDF2<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y, startRule(1.0, a, dt), 1e-15);
  EXPECT_DOUBLE_EQ(integrator.time(), dt);
  // The derivative at the start, then one Newton iteration
  EXPECT_EQ(integrator.stats().functionEvals, 2U);
  EXPECT_EQ(integrator.stats().jacobianEvals, 1U);
  EXPECT_EQ(integrator.stats().linearSolveCalls, 1U);
}

/** @test Takes later steps with the two-step formula, one evaluation per Newton iteration */
TEST(BDF2Test, LaterStepsAreTheTwoStepFormula) {
  const double a = -1.0;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  BDF2<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y1 = y;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y2 = y;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y2, twoStepRule(y1, 1.0, a, dt), 1e-15);
  EXPECT_NEAR(y, twoStepRule(y2, y1, a, dt), 1e-15);
  EXPECT_EQ(integrator.stats().functionEvals, 4U);
}

/** @test Steps from the state it is given, with the state of the step before as history */
TEST(BDF2Test, StepsFromTheStateItIsGiven) {
  const double a = -1.0;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  BDF2<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  // The caller replaces the state between steps
  y = 5.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y, twoStepRule(5.0, 1.0, a, dt), 1e-14);
}

/** @test Starts again from the start rule after a second initialize */
TEST(BDF2Test, InitializeForgetsHistory) {
  const double a = -1.0;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  const auto f = [a](const double& y, double) { return a * y; };
  BDF2<double> integrator;
  ASSERT_EQ(integrator.initialize(f, 1.0, 0.0, opts), SUCCESS);
  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  ASSERT_EQ(integrator.initialize(f, 2.0, 0.0, opts), SUCCESS);
  y = 2.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y, startRule(2.0, a, dt), 1e-15);
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Leaves state, time and history alone when the Newton iteration does not converge */
TEST(BDF2Test, NonConvergenceChangesNothing) {
  const double a = -1.0;
  const double dt = 0.1;
  auto opts = linearOptions(a, dt);
  BDF2<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  auto never = opts;
  never.maxIterations = 3;
  never.converged = [](const double&, const double&) { return false; };

  double y = 1.0;
  EXPECT_EQ(integrator.step(y, dt, never), static_cast<std::uint8_t>(Status::ERROR_CONVERGENCE));
  EXPECT_EQ(y, 1.0);
  EXPECT_EQ(integrator.time(), 0.0);

  // No history was recorded: the next step is still the start rule
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  EXPECT_NEAR(y, startRule(1.0, a, dt), 1e-15);
}

/* ------------------------------- Convergence ------------------------------ */

/** @test Converges at second order on a derivative that depends on state and time */
TEST(BDF2Test, ConvergesAtSecondOrder) {
  const double measured = observedOrder(errorAtOne, 80);
  EXPECT_GT(measured, 1.9);
  EXPECT_LT(measured, 2.2);
}

/** @test Decays from the first step on a stiff problem at a step far above its time constant */
TEST(BDF2Test, StartStepIsStableOnAStiffProblem) {
  const double a = -1000.0;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  BDF2<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  double previous = 1.0;
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
    EXPECT_LT(std::fabs(y), previous) << "step " << i;
    previous = std::fabs(y);
  }
  EXPECT_LT(std::fabs(y), 1e-6);
}
