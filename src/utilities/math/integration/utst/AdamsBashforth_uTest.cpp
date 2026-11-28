/**
 * @file AdamsBashforth_uTest.cpp
 * @brief Unit tests for apex::math::integration::AdamsBashforth.
 *
 * Notes:
 *  - Step values are checked against the formula written out by hand and
 *    against analytic solutions, never against another integrator here.
 *  - The measured order is the proof that the history and the start steps
 *    are right: a wrong history entry shows as first order.
 */

#include "src/utilities/math/integration/inc/AdamsBashforth.hpp"

#include "src/utilities/math/integration/utst/support/ConvergenceOrder.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

using apex::math::integration::AdamsBashforth;
using apex::math::integration::AdamsBashforthOptions;
using apex::math::integration::Status;
using apex::math::integration::test::observedOrder;

namespace {

constexpr auto SUCCESS = static_cast<std::uint8_t>(Status::SUCCESS);

/** One classical Runge-Kutta step of y' = a*y multiplies y by this. */
double rungeKuttaGrowth(double z) {
  return 1.0 + z + z * z / 2.0 + z * z * z / 6.0 + z * z * z * z / 24.0;
}

/**
 * Error at t = 1 of y' = cos(t)*y, y(0) = 1, whose solution is exp(sin(t)).
 * The derivative depends on both the state and the time.
 */
double errorAtOne(std::size_t order, int steps) {
  AdamsBashforthOptions<double> opts;
  opts.order = order;
  AdamsBashforth<double> integrator;
  integrator.initialize([](const double& y, double t) { return std::cos(t) * y; }, 1.0, 0.0, opts);

  double y = 1.0;
  const double dt = 1.0 / steps;
  for (int i = 0; i < steps; ++i) {
    integrator.step(y, dt, opts);
  }
  return std::fabs(y - std::exp(std::sin(1.0)));
}

} // namespace

/* ----------------------------- Initialization ----------------------------- */

/** @test Evaluates the derivative once at the start and sets the starting time */
TEST(AdamsBashforthTest, InitializeSeedsOneDerivative) {
  AdamsBashforth<double> integrator;
  const double t0 = 1.23;

  ASSERT_EQ(integrator.initialize([](const double& y, double) { return 2.0 * y; }, 4.56, t0,
                                  AdamsBashforthOptions<double>{}),
            SUCCESS);

  EXPECT_EQ(integrator.stats().functionEvals, 1U);
  EXPECT_EQ(integrator.stats().stepRejections, 0U);
  EXPECT_DOUBLE_EQ(integrator.time(), t0);
}

/* -------------------------------- Stepping -------------------------------- */

/** @test Takes forward Euler steps at order one, one evaluation each */
TEST(AdamsBashforthTest, OrderOneIsForwardEuler) {
  const double a = 2.0;
  const double dt = 0.1;
  AdamsBashforthOptions<double> opts;
  opts.order = 1;
  AdamsBashforth<double> integrator;
  ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  EXPECT_DOUBLE_EQ(y, 1.0 + dt * a);
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  EXPECT_DOUBLE_EQ(y, (1.0 + dt * a) * (1.0 + dt * a));

  EXPECT_DOUBLE_EQ(integrator.time(), 2.0 * dt);
  EXPECT_EQ(integrator.stats().functionEvals, 3U);
}

/** @test Combines the newest derivative with the one seeded at the start on the second step */
TEST(AdamsBashforthTest, SecondStepUsesTheSeededDerivative) {
  const double dt = 0.1;
  AdamsBashforthOptions<double> opts;
  opts.order = 2;
  AdamsBashforth<double> integrator;
  ASSERT_EQ(integrator.initialize([](const double& y, double) { return -y; }, 1.0, 0.0, opts),
            SUCCESS);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y1 = y;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);

  // AB2 from y1: the derivative there is -y1, and the one before it is the
  // derivative at the initial state, -1
  EXPECT_NEAR(y, y1 + dt * (1.5 * -y1 - 0.5 * -1.0), 1e-15);
}

/** @test Takes its first order-1 steps as Runge-Kutta steps and switches formula after them */
TEST(AdamsBashforthTest, StartStepsAreRungeKutta) {
  const double a = -0.7;
  const double dt = 0.1;
  const double growth = rungeKuttaGrowth(a * dt);

  for (const std::size_t order : {std::size_t{2}, std::size_t{3}, std::size_t{4}}) {
    AdamsBashforthOptions<double> opts;
    opts.order = order;
    AdamsBashforth<double> integrator;
    ASSERT_EQ(integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0, opts),
              SUCCESS);

    double y = 1.0;
    double expected = 1.0;
    for (std::size_t i = 1; i < order; ++i) {
      ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
      expected *= growth;
      EXPECT_NEAR(y, expected, 1e-15) << "order " << order << " start step " << i;
    }
    // One seed evaluation, then four per start step
    EXPECT_EQ(integrator.stats().functionEvals, 1U + 4U * (order - 1U)) << "order " << order;

    // The next step is the multistep formula: one evaluation, and not a Runge-Kutta value
    ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
    EXPECT_EQ(integrator.stats().functionEvals, 2U + 4U * (order - 1U)) << "order " << order;
    EXPECT_GT(std::fabs(y - expected * growth), 1e-9) << "order " << order;
  }
}

/** @test Clamps an order outside 1..4 to the nearest supported order */
TEST(AdamsBashforthTest, ClampsOrder) {
  const double dt = 0.1;
  AdamsBashforthOptions<double> zero;
  zero.order = 0;
  AdamsBashforth<double> lowest;
  ASSERT_EQ(lowest.initialize([](const double& y, double) { return -y; }, 1.0, 0.0, zero), SUCCESS);
  double y = 1.0;
  ASSERT_EQ(lowest.step(y, dt, zero), SUCCESS);
  EXPECT_DOUBLE_EQ(y, 1.0 - dt);

  // Order 9 behaves as order 4: three start steps before the first multistep step
  AdamsBashforthOptions<double> nine;
  nine.order = 9;
  AdamsBashforth<double> highest;
  ASSERT_EQ(highest.initialize([](const double& state, double) { return -state; }, 1.0, 0.0, nine),
            SUCCESS);
  y = 1.0;
  for (int i = 0; i < 4; ++i) {
    ASSERT_EQ(highest.step(y, dt, nine), SUCCESS);
  }
  EXPECT_EQ(highest.stats().functionEvals, 1U + 4U * 3U + 1U);
}

/* ------------------------------- Convergence ------------------------------ */

/** @test Converges at the order asked for, on a derivative that depends on state and time */
TEST(AdamsBashforthTest, ConvergesAtRequestedOrder) {
  for (const std::size_t order : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4}}) {
    const double measured =
        observedOrder([order](int steps) { return errorAtOne(order, steps); }, 80);
    EXPECT_GT(measured, static_cast<double>(order) - 0.2) << "order " << order;
    EXPECT_LT(measured, static_cast<double>(order) + 0.5) << "order " << order;
  }
}
