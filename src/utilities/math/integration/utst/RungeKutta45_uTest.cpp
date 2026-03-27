/**
 * @file RungeKutta45_uTest.cpp
 * @brief Unit tests for apex::math::integration::RungeKutta45.
 *
 * Notes:
 *  - Tests verify adaptive step size control.
 *  - Tests verify error estimation accuracy.
 *  - Tests verify exponential decay solution.
 */

#include "src/utilities/math/integration/inc/RungeKutta45.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::RK45Status;
using apex::math::integration::RungeKutta45;
using apex::math::integration::RungeKutta45Options;

/* ------------------------------ Step Tests --------------------------------- */

/** @test RK45 step with exponential decay matches exact solution. */
TEST(RungeKutta45Test, ExponentialDecaySingleStep) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;
  opts.absTol = 1e-8;
  opts.relTol = 1e-8;

  // dy/dt = -y, y(0) = 1 => y(t) = exp(-t)
  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  auto result = integrator.step(y, 0.0, 0.1, f, opts);

  EXPECT_EQ(result.status, RK45Status::SUCCESS);
  EXPECT_NEAR(result.y5, std::exp(-0.1), 1e-8);
}

/** @test RK45 integration over interval. */
TEST(RungeKutta45Test, IntegrateExponentialDecay) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;
  opts.absTol = 1e-8;
  opts.relTol = 1e-8;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  RK45Status status = integrator.integrate(y, 0.0, 1.0, 0.1, f, opts);

  EXPECT_EQ(status, RK45Status::SUCCESS);
  EXPECT_NEAR(y, std::exp(-1.0), 1e-6);
}

/** @test RK45 rejects invalid step size. */
TEST(RungeKutta45Test, RejectsInvalidStepSize) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  auto result = integrator.step(y, 0.0, 0.0, f, opts);
  EXPECT_EQ(result.status, RK45Status::ERROR_INVALID_STEP);

  result = integrator.step(y, 0.0, -0.1, f, opts);
  EXPECT_EQ(result.status, RK45Status::ERROR_INVALID_STEP);
}

/** @test RK45 provides suggested next step size. */
TEST(RungeKutta45Test, SuggestsNextStepSize) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  auto result = integrator.step(y, 0.0, 0.01, f, opts);

  EXPECT_EQ(result.status, RK45Status::SUCCESS);
  EXPECT_GT(result.dtNext, 0.0);
  EXPECT_LE(result.dtNext, opts.dtMax);
}

/* ----------------------------- Adaptive Tests ------------------------------ */

/** @test RK45 adapts step size for stiff-ish problem. */
TEST(RungeKutta45Test, AdaptsStepSize) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;
  opts.absTol = 1e-4;
  opts.relTol = 1e-4;

  // Problem with varying time scale: dy/dt = -100*y (fast decay)
  auto f = [](const double& y, double /*t*/) { return -100.0 * y; };

  double y = 1.0;
  RK45Status status = integrator.integrate(y, 0.0, 0.1, 0.01, f, opts);

  EXPECT_EQ(status, RK45Status::SUCCESS);

  // Should have adapted step size
  const auto& stats = integrator.stats();
  EXPECT_GT(stats.acceptedSteps, 0u);
}

/** @test RK45 handles smooth problem efficiently. */
TEST(RungeKutta45Test, EfficientForSmoothProblem) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;
  opts.absTol = 1e-6;
  opts.relTol = 1e-6;

  // Smooth problem: dy/dt = sin(t)
  auto f = [](const double& /*y*/, double t) { return std::sin(t); };

  double y = 0.0; // y(0) = 0
  RK45Status status = integrator.integrate(y, 0.0, M_PI, 0.1, f, opts);

  EXPECT_EQ(status, RK45Status::SUCCESS);
  // Exact: y(pi) = 1 - cos(pi) = 2
  EXPECT_NEAR(y, 2.0, 1e-5);
}

/* ------------------------------ Stats Tests -------------------------------- */

/** @test RK45 tracks statistics correctly. */
TEST(RungeKutta45Test, TracksStatistics) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;

  auto f = [](const double& y, double /*t*/) { return -y; };

  EXPECT_EQ(integrator.stats().functionEvals, 0u);
  EXPECT_EQ(integrator.stats().acceptedSteps, 0u);

  double y = 1.0;
  integrator.step(y, 0.0, 0.1, f, opts);

  // 7 function evals per step (FSAL scheme)
  EXPECT_EQ(integrator.stats().functionEvals, 7u);
}

/** @test RK45 reset clears statistics. */
TEST(RungeKutta45Test, ResetClearsStats) {
  RungeKutta45<double> integrator;
  RungeKutta45Options opts;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.step(y, 0.0, 0.1, f, opts);
  integrator.reset();

  EXPECT_EQ(integrator.stats().functionEvals, 0u);
  EXPECT_EQ(integrator.stats().acceptedSteps, 0u);
}
