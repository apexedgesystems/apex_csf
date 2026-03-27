/**
 * @file TrapezoidalRule_uTest.cpp
 * @brief Unit tests for apex::math::integration::TrapezoidalRule.
 *
 * Notes:
 *  - Tests verify initialization, stepping, and error handling.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/TrapezoidalRule.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::Status;
using apex::math::integration::TrapezoidalRule;
using apex::math::integration::TrapezoidalRuleOptions;

/**
 * @test InitializeResetsStatsAndTime
 *
 * Verify initialize resets statistics and sets the starting time.
 */
TEST(TrapezoidalRuleTest, InitializeResetsStatsAndTime) {
  using State = double;
  using Options = TrapezoidalRuleOptions<State>;
  TrapezoidalRule<State, Options> integrator;

  double t0 = 5.0;
  auto f = [](State, double) -> State { return 0.0; };

  uint8_t status = integrator.initialize(f,
                                         /*initialState=*/42.0, t0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::SUCCESS));

  const auto& s = integrator.stats();
  EXPECT_EQ(s.functionEvals, 0u);
  EXPECT_EQ(s.jacobianEvals, 0u);
  EXPECT_EQ(s.linearSolveCalls, 0u);
  EXPECT_EQ(s.stepRejections, 0u);

  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test SingleStepLinearODE
 *
 * For y' = a*y, the trapezoidal update is
 *   y1 = y0 * (1 + a*dt/2) / (1 - a*dt/2).
 */
TEST(TrapezoidalRuleTest, SingleStepLinearODE) {
  using State = double;
  using Options = TrapezoidalRuleOptions<State>;

  double a = 2.0;
  double dt = 0.1;
  double y0 = 1.0;
  double y1Expected = y0 * (1.0 + a * dt / 2.0) / (1.0 - a * dt / 2.0);

  // Newton callbacks
  Options opts{};
  opts.computeJacobian = [dt, a](const State&, double) -> State {
    // J = I - (dt/2)*df/dy = 1 - (dt/2)*a
    return 1.0 - (dt * 0.5) * a;
  };
  opts.linearSolve = [](const State& J, const State& rhs) -> State {
    // d = rhs / J
    return rhs / J;
  };
  // single iteration
  opts.converged = [](const State&, const State&) { return true; };

  TrapezoidalRule<State, Options> integrator;
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
  EXPECT_EQ(stats.functionEvals, 2u);
  EXPECT_EQ(stats.jacobianEvals, 1u);
  EXPECT_EQ(stats.linearSolveCalls, 1u);
}

/**
 * @test InvalidStepSize
 *
 * Passing dt <= 0 returns ERROR_INVALID_STEP and leaves stats/time unchanged.
 */
TEST(TrapezoidalRuleTest, InvalidStepSize) {
  using State = double;
  using Options = TrapezoidalRuleOptions<State>;

  TrapezoidalRule<State, Options> integrator;
  double t0 = 0.0;
  auto f = [](State, double) -> State { return 1.0; };

  integrator.initialize(f,
                        /*initialState=*/1.0, t0, Options{});
  State state = 1.0;

  uint8_t status = integrator.step(state,
                                   /*dt=*/0.0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));

  const auto& s = integrator.stats();
  EXPECT_EQ(s.functionEvals, 0u);
  EXPECT_EQ(s.jacobianEvals, 0u);
  EXPECT_EQ(s.linearSolveCalls, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test MultipleStepsAccumulated
 *
 * For y' = y, trapezoidal gives
 *   factor = (1 + dt/2) / (1 - dt/2),
 * so after n steps y = y0 * factor^n.
 */
TEST(TrapezoidalRuleTest, MultipleStepsAccumulated) {
  using State = double;
  using Options = TrapezoidalRuleOptions<State>;

  double dt = 0.2;
  int n = 3;
  double y0 = 1.0;
  double factor = (1.0 + dt / 2.0) / (1.0 - dt / 2.0);

  // compute expected by repeated multiplication
  double expected = y0;
  for (int i = 0; i < n; ++i) {
    expected *= factor;
  }

  Options opts{};
  opts.computeJacobian = [dt](const State&, double) { return 1.0 - (dt * 0.5); };
  opts.linearSolve = [](const State& J, const State& rhs) { return rhs / J; };
  opts.converged = [](const State&, const State&) { return true; };

  TrapezoidalRule<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(
                /*f=*/[](State y, double) { return y; },
                /*initialState=*/y0,
                /*t0=*/1.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  for (int i = 0; i < n; ++i) {
    ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));
  }

  EXPECT_DOUBLE_EQ(integrator.time(), 1.0 + n * dt);
  EXPECT_NEAR(state, expected, 1e-12);

  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, static_cast<std::size_t>(2 * n));
  EXPECT_EQ(stats.jacobianEvals, static_cast<std::size_t>(n));
  EXPECT_EQ(stats.linearSolveCalls, static_cast<std::size_t>(n));
}
