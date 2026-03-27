/**
 * @file BDF2_uTest.cpp
 * @brief Unit tests for apex::math::integration::BDF2.
 *
 * Notes:
 *  - Tests verify initialization, stepping, and error handling.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/BDF2.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::BDF2;
using apex::math::integration::BDF2Options;
using apex::math::integration::Status;

/**
 * @test InitializeResetsStatsAndTime
 *
 * Verify initialize resets statistics and sets the starting time.
 */
TEST(BDF2Test, InitializeResetsStatsAndTime) {
  using State = double;
  using Options = BDF2Options<State>;
  BDF2<State, Options> integrator;

  double t0 = 7.5;
  State y0 = 42.0;
  auto f = [](State, double) -> State { return 0.0; };

  uint8_t status = integrator.initialize(f, y0, t0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::SUCCESS));

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 0u);
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
  EXPECT_EQ(stats.stepRejections, 0u);

  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test SingleStepLinearODE
 *
 * For y' = a*y, one BDF2 step with seed y0=y_prev gives
 *   y1 = y0 / [1 - (2/3)*a*dt].
 */
TEST(BDF2Test, SingleStepLinearODE) {
  using State = double;
  using Options = BDF2Options<State>;

  double a = 2.0;
  double dt = 0.1;
  State y0 = 1.0;
  double y1Expected = y0 / (1.0 - (2.0 / 3.0) * a * dt);

  Options opts{};
  opts.computeJacobian = [a, dt](const State&, double) -> State {
    // J = I - (2/3)*dt * a
    return 1.0 - ((2.0 / 3.0) * dt) * a;
  };
  opts.linearSolve = [](const State& J, const State& rhs) -> State { return rhs / J; };
  // force single-iteration Newton solve
  opts.converged = [](const State&, const State&) { return true; };

  BDF2<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[a](State y, double) { return a * y; },
                /*initialState=*/y0,
                /*t0=*/0.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  uint8_t st = integrator.step(state, dt, opts);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));

  EXPECT_DOUBLE_EQ(state, y1Expected);
  EXPECT_DOUBLE_EQ(integrator.time(), dt);

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 1u);
  EXPECT_EQ(stats.jacobianEvals, 1u);
  EXPECT_EQ(stats.linearSolveCalls, 1u);
}

/**
 * @test TwoStepLinearODE
 *
 * Verify second BDF2 step for y' = a*y:
 *   y2 = [(4/3)y1 - (1/3)y0] / [1 - (2/3)*a*dt].
 */
TEST(BDF2Test, TwoStepLinearODE) {
  using State = double;
  using Options = BDF2Options<State>;

  double a = 1.5;
  double dt = 0.2;
  State y0 = 1.0;

  // expected y1
  double y1 = y0 / (1.0 - (2.0 / 3.0) * a * dt);
  // expected y2
  double numerator = (4.0 / 3.0) * y1 - (1.0 / 3.0) * y0;
  double y2Expected = numerator / (1.0 - (2.0 / 3.0) * a * dt);

  Options opts{};
  opts.computeJacobian = [a, dt](const State&, double) -> State {
    return 1.0 - ((2.0 / 3.0) * dt) * a;
  };
  opts.linearSolve = [](const State& J, const State& rhs) -> State { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  BDF2<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[a](State y, double) { return a * y; },
                /*initialState=*/y0,
                /*t0=*/0.5, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  // first step
  ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(state, y1);
  // second step
  ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(state, y2Expected);

  // time should have advanced twice
  EXPECT_DOUBLE_EQ(integrator.time(), 0.5 + 2 * dt);

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 2u);
  EXPECT_EQ(stats.jacobianEvals, 2u);
  EXPECT_EQ(stats.linearSolveCalls, 2u);
}

/**
 * @test InvalidStepSize
 *
 * Passing dt <= 0 returns ERROR_INVALID_STEP and leaves stats/time unchanged.
 */
TEST(BDF2Test, InvalidStepSize) {
  using State = double;
  using Options = BDF2Options<State>;

  BDF2<State, Options> integrator;
  double t0 = 1.0;
  State y0 = 3.0;
  auto f = [](State, double) -> State { return 1.0; };
  integrator.initialize(f, y0, t0, Options{});

  State state = y0;
  uint8_t status = integrator.step(state, /*dt=*/0.0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 0u);
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test MultipleStepsConstantODE
 *
 * For y' = 0, BDF2 should preserve the initial value exactly.
 */
TEST(BDF2Test, MultipleStepsConstantODE) {
  using State = double;
  using Options = BDF2Options<State>;

  double dt = 0.25;
  int n = 4;
  State y0 = 5.0;

  Options opts{};
  // f(x,t) = 0 => df/dx = 0 => J = I
  opts.computeJacobian = [](const State&, double) { return 1.0; };
  opts.linearSolve = [](const State& J, const State& rhs) { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  BDF2<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[](State, double) { return 0.0; },
                /*initialState=*/y0,
                /*t0=*/2.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  for (int i = 0; i < n; ++i) {
    ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));
  }

  EXPECT_DOUBLE_EQ(integrator.time(), 2.0 + n * dt);
  EXPECT_NEAR(state, y0, 1e-15);

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, static_cast<std::size_t>(n));
  EXPECT_EQ(stats.jacobianEvals, static_cast<std::size_t>(n));
  EXPECT_EQ(stats.linearSolveCalls, static_cast<std::size_t>(n));
}
