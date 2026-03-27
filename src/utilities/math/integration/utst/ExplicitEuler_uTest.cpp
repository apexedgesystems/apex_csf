/**
 * @file ExplicitEuler_uTest.cpp
 * @brief Unit tests for apex::math::integration::ExplicitEuler.
 *
 * Notes:
 *  - Tests verify initialization, stepping, and error handling.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/ExplicitEuler.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::EulerOptions;
using apex::math::integration::ExplicitEuler;
using apex::math::integration::Status;

/* ----------------------------- Initialization ----------------------------- */

/** @test Initialize resets statistics and sets starting time. */
TEST(ExplicitEulerTest, InitializeResetsStatsAndTime) {
  ExplicitEuler<double, EulerOptions> integrator;

  const double T0 = 5.0;
  auto f = [](double, double) -> double { return 0.0; };

  const uint8_t STATUS = integrator.initialize(f, 42.0, T0, EulerOptions{});
  EXPECT_EQ(STATUS, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_EQ(integrator.stats().functionEvals, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), T0);
}

/* -------------------------------- Stepping -------------------------------- */

/** @test Constant derivative f(x,t)=k yields x1 = x0 + k*dt exactly. */
TEST(ExplicitEulerTest, SingleStepConstantDerivative) {
  ExplicitEuler<double, EulerOptions> integrator;
  const double T0 = 0.0;
  const double K = 4.2;
  auto f = [K](double, double) -> double { return K; };

  const uint8_t INIT_STATUS = integrator.initialize(f, 0.0, T0, EulerOptions{});
  EXPECT_EQ(INIT_STATUS, static_cast<uint8_t>(Status::SUCCESS));

  double state = 0.0;
  const double DT = 0.1;
  const uint8_t STEP_STATUS = integrator.step(state, DT, EulerOptions{});
  EXPECT_EQ(STEP_STATUS, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(state, K * DT);

  EXPECT_EQ(integrator.stats().functionEvals, 1u);
  EXPECT_DOUBLE_EQ(integrator.time(), T0 + DT);
}

/** @test Multiple steps accumulate correctly for y' = y. */
TEST(ExplicitEulerTest, MultipleStepsAccumulated) {
  ExplicitEuler<double, EulerOptions> integrator;
  const double T0 = 1.0;
  auto f = [](double y, double) -> double { return y; };

  const uint8_t INIT_STATUS = integrator.initialize(f, 1.0, T0, EulerOptions{});
  EXPECT_EQ(INIT_STATUS, static_cast<uint8_t>(Status::SUCCESS));

  double state = 1.0;
  const double DT = 0.2;
  const int N = 3;
  for (int i = 0; i < N; ++i) {
    const uint8_t STATUS = integrator.step(state, DT, EulerOptions{});
    EXPECT_EQ(STATUS, static_cast<uint8_t>(Status::SUCCESS));
  }

  EXPECT_EQ(integrator.stats().functionEvals, static_cast<std::size_t>(N));
  EXPECT_DOUBLE_EQ(integrator.time(), T0 + N * DT);

  const double EXPECTED = std::pow(1.0 + DT, N);
  EXPECT_NEAR(state, EXPECTED, 1e-12);
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Invalid step size (dt <= 0) returns ERROR_INVALID_STEP. */
TEST(ExplicitEulerTest, InvalidStepSize) {
  ExplicitEuler<double, EulerOptions> integrator;
  const double T0 = 0.0;
  auto f = [](double y, double t) -> double { return y + t; };

  integrator.initialize(f, 1.0, T0, EulerOptions{});
  double state = 1.0;

  const uint8_t STATUS = integrator.step(state, 0.0, EulerOptions{});
  EXPECT_EQ(STATUS, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));

  EXPECT_EQ(integrator.stats().functionEvals, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), T0);
}
