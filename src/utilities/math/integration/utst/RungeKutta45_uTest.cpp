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

#include "src/utilities/math/integration/inc/StateVector.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

#include <limits>
#include <utility>

using apex::math::integration::RK45Status;
using apex::math::integration::RungeKutta45;
using apex::math::integration::RungeKutta45Options;
using apex::math::integration::State3;

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

/* --------------------------- Non-Finite Tests ------------------------------ */

/** @test Reports a NaN derivative as ERROR_NON_FINITE with a finite, smaller step suggestion */
TEST(RungeKutta45Test, StepReportsNanDerivative) {
  RungeKutta45<double> integrator;
  const RungeKutta45Options opts;
  const double dt = 0.1;

  const auto result = integrator.step(
      1.0, 0.0, dt, [](const double&, double) { return std::numeric_limits<double>::quiet_NaN(); },
      opts);

  EXPECT_EQ(result.status, RK45Status::ERROR_NON_FINITE);
  EXPECT_DOUBLE_EQ(result.dtNext, dt * opts.maxShrink);
  EXPECT_EQ(integrator.stats().acceptedSteps, 0U);
  EXPECT_EQ(integrator.stats().rejectedSteps, 0U);
}

/** @test Reports a NaN in one component of a vector state, whatever the other components hold */
TEST(RungeKutta45Test, StepReportsNanInOneVectorComponent) {
  for (std::size_t bad = 0; bad < 3; ++bad) {
    RungeKutta45<State3> integrator;
    const RungeKutta45Options opts;

    const auto result = integrator.step(
        State3{1.0, 1.0, 1.0}, 0.0, 0.1,
        [bad](const State3& y, double) {
          State3 dydt = y * -1.0;
          dydt[bad] = std::numeric_limits<double>::quiet_NaN();
          return dydt;
        },
        opts);

    EXPECT_EQ(result.status, RK45Status::ERROR_NON_FINITE) << "component " << bad;
    EXPECT_EQ(integrator.stats().acceptedSteps, 0U) << "component " << bad;
  }
}

/** @test Reports a step that overflows as ERROR_NON_FINITE */
TEST(RungeKutta45Test, StepReportsOverflow) {
  RungeKutta45<double> integrator;
  const RungeKutta45Options opts;

  const auto result = integrator.step(
      1.0, 0.0, 1.0, [](const double&, double) { return std::numeric_limits<double>::max(); },
      opts);

  EXPECT_EQ(result.status, RK45Status::ERROR_NON_FINITE);
  EXPECT_TRUE(std::isfinite(result.dtNext));
}

/** @test Returns from integrate with ERROR_NON_FINITE once the derivative is NaN, state untouched
 */
TEST(RungeKutta45Test, IntegrateStopsOnNanDerivative) {
  RungeKutta45<double> integrator;
  const RungeKutta45Options opts;

  // The derivative is sound up to t = 0.3 and NaN after it.
  const auto f = [](const double& y, double t) {
    return t > 0.3 ? std::numeric_limits<double>::quiet_NaN() : -y;
  };

  double y = 1.0;
  const RK45Status status = integrator.integrate(y, 0.0, 1.0, 0.05, f, opts);

  EXPECT_EQ(status, RK45Status::ERROR_NON_FINITE);
  EXPECT_TRUE(std::isfinite(y));
  EXPECT_LT(y, 1.0);
  EXPECT_GT(y, std::exp(-0.4));
}

/** @test Refuses an interval whose start or end is not finite, without calling the derivative */
TEST(RungeKutta45Test, IntegrateRefusesNonFiniteInterval) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const RungeKutta45Options opts;

  for (const auto& [t0, tEnd] :
       {std::pair{nan, 1.0}, std::pair{0.0, nan}, std::pair{0.0, inf}, std::pair{-inf, 1.0}}) {
    RungeKutta45<double> integrator;
    int calls = 0;
    double y = 1.0;
    const RK45Status status = integrator.integrate(
        y, t0, tEnd, 0.1,
        [&calls](const double& state, double) {
          ++calls;
          return -state;
        },
        opts);

    EXPECT_EQ(status, RK45Status::ERROR_INVALID_STEP) << "t0 " << t0 << " tEnd " << tEnd;
    EXPECT_EQ(calls, 0) << "t0 " << t0 << " tEnd " << tEnd;
    EXPECT_EQ(y, 1.0) << "t0 " << t0 << " tEnd " << tEnd;
  }
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

  // Seven stages per step, each one evaluation
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
