/**
 * @file RungeKutta4_uTest.cpp
 * @brief Unit tests for apex::math::integration::RungeKutta4.
 *
 * Notes:
 *  - Tests verify initialization, stepping, and error handling.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/RungeKutta4.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::RungeKutta4;
using apex::math::integration::RungeKutta4Options;
using apex::math::integration::Status;

/**
 * @test InitializeResetsStatsAndTime
 *
 * Verify initialize resets statistics and sets the starting time.
 */
TEST(RungeKutta4Test, InitializeResetsStatsAndTime) {
  using State = double;
  using Options = RungeKutta4Options;
  RungeKutta4<State, Options> integrator;

  double t0 = 5.0;
  auto f = [](State, double) -> State { return 0.0; };

  uint8_t status = integrator.initialize(f,
                                         /*initialState=*/42.0, t0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_EQ(integrator.stats().functionEvals, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test SingleStepConstantDerivative
 *
 * A constant derivative f(x,t)=k yields x_(n+1) = x_n + k*dt exactly, and uses 4 evals.
 */
TEST(RungeKutta4Test, SingleStepConstantDerivative) {
  using State = double;
  using Options = RungeKutta4Options;

  RungeKutta4<State, Options> integrator;
  double t0 = 0.0;
  double k = 4.2;
  auto f = [k](State, double) -> State { return k; };

  uint8_t initStatus = integrator.initialize(f,
                                             /*initialState=*/0.0, t0, Options{});
  EXPECT_EQ(initStatus, static_cast<uint8_t>(Status::SUCCESS));

  double state = 0.0;
  double dt = 0.1;
  uint8_t stepStatus = integrator.step(state, dt, Options{});
  EXPECT_EQ(stepStatus, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(state, k * dt);

  EXPECT_EQ(integrator.stats().functionEvals, 4u);
  EXPECT_DOUBLE_EQ(integrator.time(), t0 + dt);
}

/**
 * @test InvalidStepSize
 *
 * Passing dt <= 0 returns ERROR_INVALID_STEP and leaves stats/time unchanged.
 */
TEST(RungeKutta4Test, InvalidStepSize) {
  using State = double;
  using Options = RungeKutta4Options;

  RungeKutta4<State, Options> integrator;
  double t0 = 0.0;
  auto f = [](State, double) -> State { return 1.0; };

  integrator.initialize(f,
                        /*initialState=*/1.0, t0, Options{});

  double state = 1.0;
  uint8_t status = integrator.step(state,
                                   /*dt=*/0.0, Options{});
  EXPECT_EQ(status, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));

  EXPECT_EQ(integrator.stats().functionEvals, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/**
 * @test MultipleStepsPolynomialDerivative
 *
 * For f(x,t)=t^2, the exact solution is x(t)=t^3/3.  RK4 is 4th-order and integrates
 * degree-2 derivatives exactly, so after N fixed steps you get the exact t^3/3.
 */
TEST(RungeKutta4Test, MultipleStepsPolynomialDerivative) {
  using State = double;
  using Options = RungeKutta4Options;

  // set up integrator
  RungeKutta4<State, Options> integrator;
  double t0 = 0.0;
  auto f = [](State, double t) -> State { return t * t; };

  // initialize at x(0)=0
  ASSERT_EQ(integrator.initialize(f, /*initialState=*/0.0, t0, Options{}),
            static_cast<uint8_t>(Status::SUCCESS));

  // take N steps of size dt
  double state = 0.0;
  double dt = 0.1;
  int n = 5;
  for (int i = 0; i < n; ++i) {
    ASSERT_EQ(integrator.step(state, dt, Options{}), static_cast<uint8_t>(Status::SUCCESS));
  }

  // final time
  double tN = t0 + n * dt;

  // expected = (tN^3 - t0^3)/3
  double expected = (tN * tN * tN - t0 * t0 * t0) / 3.0;

  // 4 function-evals per step
  EXPECT_EQ(integrator.stats().functionEvals, static_cast<std::size_t>(n * 4));

  // time should have advanced correctly
  EXPECT_DOUBLE_EQ(integrator.time(), tN);

  // RK4 should be exact here
  EXPECT_NEAR(state, expected, 1e-12);
}
