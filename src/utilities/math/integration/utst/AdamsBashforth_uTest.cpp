/**
 * @file AdamsBashforth_uTest.cpp
 * @brief Unit tests for apex::math::integration::AdamsBashforth.
 *
 * Notes:
 *  - Tests verify initialization, stepping, and error handling.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/AdamsBashforth.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::AdamsBashforth;
using apex::math::integration::AdamsBashforthOptions;
using apex::math::integration::Status;

/**
 * @test InitializeResetsStatsAndTime
 *
 * Verify initialize seeds history once and sets the starting time.
 */
TEST(AdamsBashforthTest, InitializeResetsStatsAndTime) {
  using State = double;
  using Options = AdamsBashforthOptions<State>;
  AdamsBashforth<State, Options> integrator;

  double t0 = 1.23;
  State y0 = 4.56;
  auto f = [](State y, double /*t*/) -> State { return 2.0 * y; };

  uint8_t status = integrator.initialize(f, y0, t0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::SUCCESS));

  // initializeImpl calls f(initialState, t0) once to seed f_hist_
  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 1u);
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
  EXPECT_EQ(stats.stepRejections, 0u);

  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test SingleStepLinearODE
 *
 * For y' = a*y, first AB step (order=opts.order>=1) is
 *   y1 = y0 + dt*a*y0.
 */
TEST(AdamsBashforthTest, SingleStepLinearODE) {
  using State = double;
  using Options = AdamsBashforthOptions<State>;

  double a = 2.0;
  double dt = 0.1;
  State y0 = 1.0;
  double y1Expected = y0 + dt * (a * y0);

  Options opts{};
  opts.order = 3; // order >1 still yields AB1 on first step

  AdamsBashforth<State, Options> integrator;
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

  // stats.functionEvals: 1 (seed) + 1 (f_new) = 2
  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, 2u);
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
}

/**
 * @test InvalidStepSize
 *
 * Passing dt <= 0 returns ERROR_INVALID_STEP and leaves stats/time unchanged.
 */
TEST(AdamsBashforthTest, InvalidStepSize) {
  using State = double;
  using Options = AdamsBashforthOptions<State>;

  AdamsBashforth<State, Options> integrator;
  double t0 = 0.5;
  State y0 = 2.0;
  auto f = [](State y, double /*t*/) -> State { return y; };
  integrator.initialize(f, y0, t0, Options{});

  State state = y0;
  uint8_t status = integrator.step(state, /*dt=*/0.0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));

  const auto& stats = integrator.stats();
  // only the initial seed should have executed
  EXPECT_EQ(stats.functionEvals, 1u);
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test MultipleStepsConstantODE
 *
 * For y' = 0, AB should preserve the initial value exactly.
 */
TEST(AdamsBashforthTest, MultipleStepsConstantODE) {
  using State = double;
  using Options = AdamsBashforthOptions<State>;

  double dt = 0.2;
  int n = 5;
  State y0 = 3.3;

  Options opts{};
  opts.order = 4;
  // f(x,t) = 0 => seed + updates give no change
  auto zero = [](State, double) -> State { return 0.0; };
  AdamsBashforth<State, Options> integrator;
  ASSERT_EQ(integrator.initialize(zero, y0, /*t0=*/2.0, opts),
            static_cast<uint8_t>(Status::SUCCESS));

  State state = y0;
  for (int i = 0; i < n; ++i) {
    ASSERT_EQ(integrator.step(state, dt, opts), static_cast<uint8_t>(Status::SUCCESS));
  }

  EXPECT_DOUBLE_EQ(integrator.time(), 2.0 + n * dt);
  EXPECT_NEAR(state, y0, 1e-15);

  // stats.functionEvals: 1 (seed) + n (one f_new per step)
  const auto& stats = integrator.stats();
  EXPECT_EQ(stats.functionEvals, static_cast<std::size_t>(1 + n));
  EXPECT_EQ(stats.jacobianEvals, 0u);
  EXPECT_EQ(stats.linearSolveCalls, 0u);
}
