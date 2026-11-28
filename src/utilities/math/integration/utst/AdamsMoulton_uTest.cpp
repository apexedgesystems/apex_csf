/**
 * @file AdamsMoulton_uTest.cpp
 * @brief Unit tests for apex::math::integration::AdamsMoulton.
 *
 * Notes:
 *  - Step values are checked against the formulas solved by hand for a linear
 *    problem, and the order against an analytic solution.
 *  - The options supply the corrector's Newton matrix, I - (5/12)*dt*df/dx.
 */

#include "src/utilities/math/integration/inc/AdamsMoulton.hpp"

#include "src/utilities/math/integration/utst/support/ConvergenceOrder.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using apex::math::integration::AdamsMoulton;
using apex::math::integration::AdamsMoultonOptions;
using apex::math::integration::Status;
using apex::math::integration::test::observedOrder;

namespace {

constexpr auto SUCCESS = static_cast<std::uint8_t>(Status::SUCCESS);
constexpr double FIVE_TWELFTHS = 5.0 / 12.0;

/** Options for y' = a*y at step dt. One Newton iteration solves a linear problem exactly. */
AdamsMoultonOptions<double> linearOptions(double a, double dt) {
  AdamsMoultonOptions<double> opts;
  opts.computeJacobian = [a, dt](const double&, double) { return 1.0 - FIVE_TWELFTHS * dt * a; };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
  return opts;
}

/** One classical Runge-Kutta step of y' = a*y multiplies y by this. */
double rungeKuttaGrowth(double z) {
  return 1.0 + z + z * z / 2.0 + z * z * z / 6.0 + z * z * z * z / 24.0;
}

/** The corrector solved for y' = a*y: x1 = xN + (dt/12)*(5*a*x1 + 8*a*xN - a*xPrev). */
double corrector(double xN, double xPrev, double a, double dt) {
  return (xN + (dt / 12.0) * (8.0 * a * xN - a * xPrev)) / (1.0 - FIVE_TWELFTHS * a * dt);
}

/** Error at t = 1 of y' = cos(t)*y, y(0) = 1, whose solution is exp(sin(t)). */
double errorAtOne(int steps) {
  const double dt = 1.0 / steps;
  AdamsMoultonOptions<double> opts;
  opts.maxIterations = 50;
  opts.computeJacobian = [dt](const double&, double t) {
    return 1.0 - FIVE_TWELFTHS * dt * std::cos(t);
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
  opts.converged = [](const double& delta, const double&) { return std::fabs(delta) < 1e-14; };

  AdamsMoulton<double> integrator;
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
TEST(AdamsMoultonTest, InitializeSetsTimeAndEvaluatesNothing) {
  AdamsMoulton<double> integrator;
  const double t0 = 2.71;

  ASSERT_EQ(integrator.initialize([](const double&, double) { return 0.0; }, 1.0, t0,
                                  AdamsMoultonOptions<double>{}),
            SUCCESS);

  EXPECT_EQ(integrator.stats().functionEvals, 0U);
  EXPECT_EQ(integrator.stats().jacobianEvals, 0U);
  EXPECT_EQ(integrator.stats().linearSolveCalls, 0U);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/* -------------------------------- Stepping -------------------------------- */

/** @test Takes the first step as a Runge-Kutta step: four evaluations and no solve */
TEST(AdamsMoultonTest, FirstStepIsRungeKutta) {
  const double a = -0.7;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  AdamsMoulton<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y, rungeKuttaGrowth(a * dt), 1e-15);
  EXPECT_DOUBLE_EQ(integrator.time(), dt);
  EXPECT_EQ(integrator.stats().functionEvals, 4U);
  EXPECT_EQ(integrator.stats().jacobianEvals, 0U);
  EXPECT_EQ(integrator.stats().linearSolveCalls, 0U);
}

/** @test Takes later steps with the corrector over the last two derivatives */
TEST(AdamsMoultonTest, LaterStepsAreTheCorrector) {
  const double a = -0.7;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  AdamsMoulton<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y1 = y;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y2 = y;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y2, corrector(y1, 1.0, a, dt), 1e-15);
  EXPECT_NEAR(y, corrector(y2, y1, a, dt), 1e-15);
  // Four for the start step, then the derivative at the state and one Newton iteration per step
  EXPECT_EQ(integrator.stats().functionEvals, 8U);
  EXPECT_EQ(integrator.stats().jacobianEvals, 2U);
  EXPECT_EQ(integrator.stats().linearSolveCalls, 2U);
}

/** @test Steps from the state it is given, with the derivative of the step before as history */
TEST(AdamsMoultonTest, StepsFromTheStateItIsGiven) {
  const double a = -0.7;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  AdamsMoulton<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  // The caller replaces the state between steps
  y = 5.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y, corrector(5.0, 1.0, a, dt), 1e-14);
}

/** @test Starts again with a Runge-Kutta step after a second initialize */
TEST(AdamsMoultonTest, InitializeForgetsHistory) {
  const double a = -0.7;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  const auto f = [a](const double& y, double) { return a * y; };
  AdamsMoulton<double> integrator;
  ASSERT_EQ(integrator.initialize(f, 1.0, 0.0, opts), SUCCESS);
  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  ASSERT_EQ(integrator.initialize(f, 2.0, 0.0, opts), SUCCESS);
  y = 2.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  EXPECT_NEAR(y, 2.0 * rungeKuttaGrowth(a * dt), 1e-15);
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Leaves state, time and history alone when the Newton iteration does not converge */
TEST(AdamsMoultonTest, NonConvergenceChangesNothing) {
  const double a = -0.7;
  const double dt = 0.1;
  const auto opts = linearOptions(a, dt);
  AdamsMoulton<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);
  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y1 = y;

  auto never = opts;
  never.maxIterations = 3;
  never.converged = [](const double&, const double&) { return false; };
  EXPECT_EQ(integrator.step(y, dt, never), static_cast<std::uint8_t>(Status::ERROR_CONVERGENCE));
  EXPECT_EQ(y, y1);
  EXPECT_DOUBLE_EQ(integrator.time(), dt);

  // The history is still the first step's: the retry gives the corrector's value
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  EXPECT_NEAR(y, corrector(y1, 1.0, a, dt), 1e-15);
}

/* ------------------------------- Convergence ------------------------------ */

/** @test Converges at third order on a derivative that depends on state and time */
TEST(AdamsMoultonTest, ConvergesAtThirdOrder) {
  const double measured = observedOrder(errorAtOne, 80);
  EXPECT_GT(measured, 2.8);
  EXPECT_LT(measured, 3.3);
}
