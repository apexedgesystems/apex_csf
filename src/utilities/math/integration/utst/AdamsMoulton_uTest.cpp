/**
 * @file AdamsMoulton_uTest.cpp
 * @brief Unit tests for apex::math::integration::AdamsMoulton.
 *
 * Notes:
 *  - Tests verify initialization, stepping, and error handling.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/AdamsMoulton.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::AdamsMoulton;
using apex::math::integration::AdamsMoultonOptions;
using apex::math::integration::Status;

/**
 * @test InitializeResetsStatsAndTime
 *
 * Verify initialize resets statistics and sets the starting time.
 */
TEST(AdamsMoultonTest, InitializeResetsStatsAndTime) {
  using State = double;
  using Options = AdamsMoultonOptions<State>;
  AdamsMoulton<State, Options> integrator;

  double t0 = 3.14;
  State y0 = 2.71;
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
 * For y' = a*y, the 2-step Adams-Moulton update with
 * initial history y_prev=y0 is
 *
 *   y1 = y0 * (1 + 7/12*a*dt) / (1 - 5/12*a*dt).
 */
TEST(AdamsMoultonTest, SingleStepLinearODE) {
  using State = double;
  using Options = AdamsMoultonOptions<State>;

  double a = 2.0;
  double dt = 0.1;
  State y0 = 1.0;

  double num = 1.0 + (7.0 / 12.0) * a * dt;
  double denom = 1.0 - (5.0 / 12.0) * a * dt;
  double y1Exp = y0 * num / denom;

  Options opts{};
  opts.computeJacobian = [a, dt](const State&, double) -> State {
    // J = I - (dt/12)*5*a
    return 1.0 - (5.0 / 12.0) * a * dt;
  };
  opts.linearSolve = [](const State& J, const State& rhs) -> State { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  AdamsMoulton<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[a](State y, double) { return a * y; },
                /*initialState=*/y0,
                /*t0=*/0.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  uint8_t st = integrator.step(state, dt, opts);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));

  EXPECT_DOUBLE_EQ(state, y1Exp);
  EXPECT_DOUBLE_EQ(integrator.time(), dt);

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 3u);
  EXPECT_EQ(stats.jacobianEvals, 1u);
  EXPECT_EQ(stats.linearSolveCalls, 1u);
}

/**
 * @test TwoStepLinearODE
 *
 * Verify second Adams-Moulton step for y' = a*y:
 *
 *   y2 = [y1 + (dt/12)(8a*y1 - a*y0)] / (1 - 5/12*a*dt).
 */
TEST(AdamsMoultonTest, TwoStepLinearODE) {
  using State = double;
  using Options = AdamsMoultonOptions<State>;

  double a = 1.5;
  double dt = 0.2;
  State y0 = 1.0;

  // first-step exact
  double num1 = 1.0 + (7.0 / 12.0) * a * dt;
  double den1 = 1.0 - (5.0 / 12.0) * a * dt;
  double y1 = y0 * num1 / den1;

  // second-step expected
  double num2 = y1 + (dt / 12.0) * (8.0 * a * y1 - a * y0);
  double den2 = 1.0 - (5.0 / 12.0) * a * dt;
  double y2Exp = num2 / den2;

  Options opts{};
  opts.computeJacobian = [a, dt](const State&, double) -> State {
    return 1.0 - (5.0 / 12.0) * a * dt;
  };
  opts.linearSolve = [](const State& J, const State& rhs) -> State { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  AdamsMoulton<State, Options> integrator;
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
  EXPECT_DOUBLE_EQ(state, y2Exp);

  EXPECT_DOUBLE_EQ(integrator.time(), 0.5 + 2 * dt);

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 6u);
  EXPECT_EQ(stats.jacobianEvals, 2u);
  EXPECT_EQ(stats.linearSolveCalls, 2u);
}

/**
 * @test InvalidStepSize
 *
 * Passing dt <= 0 returns ERROR_INVALID_STEP and leaves stats/time unchanged.
 */
TEST(AdamsMoultonTest, InvalidStepSize) {
  using State = double;
  using Options = AdamsMoultonOptions<State>;

  AdamsMoulton<State, Options> integrator;
  double t0 = 1.0;
  State y0 = 4.0;
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
 * For y' = 0, Adams-Moulton should preserve the initial value exactly.
 */
TEST(AdamsMoultonTest, MultipleStepsConstantODE) {
  using State = double;
  using Options = AdamsMoultonOptions<State>;

  double dt = 0.15;
  int n = 5;
  State y0 = 7.0;

  Options opts{};
  // f(x,t) = 0 => df/dx = 0 => J = I
  opts.computeJacobian = [](const State&, double) { return 1.0; };
  opts.linearSolve = [](const State& J, const State& rhs) { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  AdamsMoulton<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[](State, double) { return 0.0; },
                /*initialState=*/y0,
                /*t0=*/2.5, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  for (int i = 0; i < n; ++i) {
    ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));
  }

  EXPECT_DOUBLE_EQ(integrator.time(), 2.5 + n * dt);
  EXPECT_NEAR(state, y0, 1e-15);

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, static_cast<std::size_t>(3 * n));
  EXPECT_EQ(stats.jacobianEvals, static_cast<std::size_t>(n));
  EXPECT_EQ(stats.linearSolveCalls, static_cast<std::size_t>(n));
}
