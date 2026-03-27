/**
 * @file SDIRK2_uTest.cpp
 * @brief Unit tests for apex::math::integration::SDIRK2.
 *
 * Notes:
 *  - Tests verify initialization, stepping, and error handling.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/SDIRK2.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::SDIRK2;
using apex::math::integration::SDIRK2Options;
using apex::math::integration::Status;

/**
 * @test InitializeResetsStatsAndTime
 *
 * Verify initialize resets statistics and sets the starting time.
 */
TEST(SDIRK2Test, InitializeResetsStatsAndTime) {
  using State = double;
  using Options = SDIRK2Options<State>;
  SDIRK2<State, Options> integrator;

  double t0 = 1.0;
  auto f = [](State, double) -> State { return 0.0; };

  uint8_t status = integrator.initialize(f,
                                         /*initialState=*/0.0, t0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::SUCCESS));

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 0u);
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
  EXPECT_EQ(stats.stepRejections, 0u);

  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test InvalidStepSize
 *
 * Passing dt <= 0 returns ERROR_INVALID_STEP and leaves stats/time unchanged.
 */
TEST(SDIRK2Test, InvalidStepSize) {
  using State = double;
  using Options = SDIRK2Options<State>;

  SDIRK2<State, Options> integrator;
  double t0 = 2.0;
  auto f = [](State, double) -> State { return 1.0; };
  integrator.initialize(f,
                        /*initialState=*/0.0, t0, Options{});

  State state = 0.0;
  uint8_t status = integrator.step(state,
                                   /*dt=*/0.0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 0u);
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
  EXPECT_EQ(stats.stepRejections, 1u);

  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test ConstantDerivativePreserved
 *
 * For y' = C (constant), SDIRK2 yields y1 = y0 + dt*C,
 * and stats reflect two stage solves.
 */
TEST(SDIRK2Test, ConstantDerivativePreserved) {
  using State = double;
  using Options = SDIRK2Options<State>;

  double c = 5.0;
  double dt = 0.2;
  State y0 = 1.0;
  double y1Exp = y0 + dt * c;

  Options opts{};
  opts.computeJacobian = [](const State&, double) { return 1.0; };
  opts.linearSolve = [](const State& J, const State& rhs) { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  SDIRK2<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[c](State, double) { return c; },
                /*initialState=*/y0,
                /*t0=*/0.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  uint8_t st = integrator.step(state, dt, opts);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));

  EXPECT_DOUBLE_EQ(state, y1Exp);
  EXPECT_DOUBLE_EQ(integrator.time(), dt);

  const auto& stats = integrator.stats();
  // stage1: 1 f, 1 J, 1 solve
  // stage2: 2 f, 1 J, 1 solve
  // combine: 2 f
  EXPECT_EQ(stats.functionEvals, 1 + 2 + 2);
  EXPECT_EQ(stats.jacobianEvals, 2u);
  EXPECT_EQ(stats.linearSolveCalls, 2u);
  EXPECT_EQ(stats.stepRejections, 0u);
}

/**
 * @test MultipleStepsConstantDerivative
 *
 * Repeated steps of y' = C should advance by n*dt*C exactly.
 */
TEST(SDIRK2Test, MultipleStepsConstantDerivative) {
  using State = double;
  using Options = SDIRK2Options<State>;

  double c = 3.0;
  double dt = 0.1;
  int n = 5;
  State y0 = 2.0;
  double yN = y0 + n * dt * c;

  Options opts{};
  opts.computeJacobian = [](const State&, double) { return 1.0; };
  opts.linearSolve = [](const State& J, const State& rhs) { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  SDIRK2<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[c](State, double) { return c; }, y0,
                /*t0=*/0.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  for (int i = 0; i < n; ++i) {
    ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));
  }

  EXPECT_DOUBLE_EQ(state, yN);
  EXPECT_DOUBLE_EQ(integrator.time(), n * dt);
}

/**
 * @test SingleStepLinearODE
 *
 * For y' = a*y, SDIRK2 should achieve second-order accuracy:
 *   y1 ~= y0 + dt*a*y0 + (1/2)*dt^2*a^2*y0.
 */
TEST(SDIRK2Test, SingleStepLinearODE) {
  using State = double;
  using Options = SDIRK2Options<State>;

  double a = 1.5;
  double dt = 0.15;
  State y0 = 1.0;
  double y1E = y0 + dt * a * y0 + 0.5 * dt * dt * a * a * y0;

  Options opts{};
  opts.computeJacobian = [a](const State&, double) { return a; };
  opts.linearSolve = [](const State& J, const State& rhs) { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  SDIRK2<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[a](State y, double) { return a * y; }, y0,
                /*t0=*/0.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));

  // Relaxed to O(dt^3) error ~= 8e-3
  EXPECT_NEAR(state, y1E, 1e-2);
  EXPECT_DOUBLE_EQ(integrator.time(), dt);
  EXPECT_EQ(integrator.stats().stepRejections, 0u);
}
