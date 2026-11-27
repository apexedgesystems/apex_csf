/**
 * @file BDF_uTest.cpp
 * @brief Unit tests for apex::math::integration::BDF (variable order).
 *
 * Notes:
 *  - Tests verify BDF methods for stiff ODEs.
 *  - Tests verify order ramp-up behavior.
 *  - Tests verify basic functionality.
 */

#include "src/utilities/math/integration/inc/BDF.hpp"

#include "src/utilities/math/integration/utst/support/ConvergenceOrder.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <array>

using apex::math::integration::BDF;
using apex::math::integration::BDF3;
using apex::math::integration::BDF4;
using apex::math::integration::ImplicitOptions;
using apex::math::integration::Status;
using apex::math::integration::test::observedOrder;

namespace {

constexpr auto SUCCESS = static_cast<std::uint8_t>(Status::SUCCESS);

/**
 * Error at t = 1 of y' = cos(t)*y, y(0) = 1, whose solution is exp(sin(t)).
 * The options return the Newton matrix of the order each step uses and
 * iterate to convergence.
 */
template <std::size_t MaxOrder> double generalStartError(int steps) {
  const double dt = 1.0 / steps;
  BDF<double, MaxOrder> integrator;
  ImplicitOptions<double> opts;
  opts.maxIterations = 50;
  opts.computeJacobian = [&integrator, dt](const double&, double t) {
    return integrator.leadingCoefficient() - dt * std::cos(t);
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
  opts.converged = [](const double& delta, const double&) { return std::fabs(delta) < 1e-14; };

  integrator.initialize([](const double& y, double t) { return std::cos(t) * y; }, 1.0, 0.0);
  double y = 1.0;
  for (int i = 0; i < steps; ++i) {
    integrator.step(y, dt, opts);
  }
  return std::fabs(y - std::exp(std::sin(1.0)));
}

/**
 * Error at t = 1 of y' = 8*t^7, y(0) = 0, whose solution is t^8. Its first
 * seven derivatives vanish at the start, so the low-order ramp-up steps are
 * nearly exact and the error left is that of the order-MaxOrder formula.
 */
template <std::size_t MaxOrder> double flatStartError(int steps) {
  const double dt = 1.0 / steps;
  BDF<double, MaxOrder> integrator;
  ImplicitOptions<double> opts;
  // The derivative does not depend on the state: the Newton matrix is alpha_k
  opts.computeJacobian = [&integrator](const double&, double) {
    return integrator.leadingCoefficient();
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };

  integrator.initialize([](const double&, double t) { return 8.0 * std::pow(t, 7); }, 0.0, 0.0);
  double y = 0.0;
  for (int i = 0; i < steps; ++i) {
    integrator.step(y, dt, opts);
  }
  return std::fabs(y - 1.0);
}

/** Options for y' = a*y at step dt, with the Newton matrix of the order in use. */
template <std::size_t MaxOrder>
ImplicitOptions<double> linearOptions(const BDF<double, MaxOrder>& integrator, double a,
                                      double dt) {
  ImplicitOptions<double> opts;
  opts.computeJacobian = [&integrator, a, dt](const double&, double) {
    return integrator.leadingCoefficient() - dt * a;
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
  return opts;
}

} // namespace

/* ------------------------------- BDF Tests --------------------------------- */

/** @test Takes a backward Euler step first, then the two-step formula from the state given */
TEST(BDFTest, FirstTwoStepsAreBdf1ThenBdf2) {
  const double a = -1.0;
  const double dt = 0.1;
  BDF<double, 2> integrator;
  const auto opts = linearOptions(integrator, a, dt);
  integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y1 = y;
  EXPECT_NEAR(y1, 1.0 / (1.0 - a * dt), 1e-15);

  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  EXPECT_NEAR(y, (2.0 * y1 - 0.5 * 1.0) / (1.5 - a * dt), 1e-15);
  EXPECT_EQ(integrator.stats().steps, 2U);
  EXPECT_NEAR(integrator.time(), 2.0 * dt, 1e-15);
}

/** @test Raises the order by one per step up to MaxOrder, the leading coefficient following it */
TEST(BDFTest, RampsOrderToMaxOrder) {
  const std::array<double, 7> alpha = {0.0,         1.0,          3.0 / 2.0,   11.0 / 6.0,
                                       25.0 / 12.0, 137.0 / 60.0, 147.0 / 60.0};
  const double a = -1.0;
  const double dt = 0.1;
  BDF<double, 6> integrator;
  const auto opts = linearOptions(integrator, a, dt);
  integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0);

  double y = 1.0;
  for (std::size_t step = 0; step < 8; ++step) {
    const std::size_t expected = step < 6 ? step + 1 : 6;
    EXPECT_EQ(integrator.currentOrder(), expected) << "before step " << step;
    EXPECT_DOUBLE_EQ(integrator.leadingCoefficient(), alpha[expected]) << "before step " << step;
    ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  }
}

/** @test Converges at second order from a general start, whatever the maximum order */
TEST(BDFTest, ConvergesAtSecondOrderFromAGeneralStart) {
  const std::array<double, 5> orders = {
      observedOrder(generalStartError<2>, 80), observedOrder(generalStartError<3>, 80),
      observedOrder(generalStartError<4>, 80), observedOrder(generalStartError<5>, 80),
      observedOrder(generalStartError<6>, 80)};
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_GT(orders[i], 1.9) << "MaxOrder " << i + 2;
    EXPECT_LT(orders[i], 2.2) << "MaxOrder " << i + 2;
  }
  EXPECT_NEAR(observedOrder(generalStartError<1>, 80), 1.0, 0.1);
}

/** @test Converges at the order of each formula when the solution is flat at the start */
TEST(BDFTest, EachFormulaConvergesAtItsOrderFromAFlatStart) {
  const std::array<double, 6> orders = {
      observedOrder(flatStartError<1>, 40), observedOrder(flatStartError<2>, 40),
      observedOrder(flatStartError<3>, 40), observedOrder(flatStartError<4>, 40),
      observedOrder(flatStartError<5>, 40), observedOrder(flatStartError<6>, 40)};
  for (std::size_t i = 0; i < 6; ++i) {
    const double order = static_cast<double>(i + 1);
    EXPECT_GT(orders[i], order - 0.3) << "MaxOrder " << i + 1;
    EXPECT_LT(orders[i], order + 0.5) << "MaxOrder " << i + 1;
  }
}

/** @test Leaves state, time, order and history alone when the Newton iteration does not converge */
TEST(BDFTest, NonConvergenceChangesNothing) {
  const double a = -1.0;
  const double dt = 0.1;
  BDF<double, 3> integrator;
  const auto opts = linearOptions(integrator, a, dt);
  integrator.initialize([a](const double& y, double) { return a * y; }, 1.0, 0.0);
  double y = 1.0;
  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  const double y1 = y;

  auto never = opts;
  never.maxIterations = 3;
  never.converged = [](const double&, const double&) { return false; };
  EXPECT_EQ(integrator.step(y, dt, never), static_cast<std::uint8_t>(Status::ERROR_CONVERGENCE));
  EXPECT_EQ(y, y1);
  EXPECT_NEAR(integrator.time(), dt, 1e-15);
  EXPECT_EQ(integrator.currentOrder(), 2U);
  EXPECT_EQ(integrator.stats().steps, 1U);

  ASSERT_EQ(integrator.step(y, dt, opts), SUCCESS);
  EXPECT_NEAR(y, (2.0 * y1 - 0.5 * 1.0) / (1.5 - a * dt), 1e-15);
}

/** @test BDF starts at order 1. */
TEST(BDFTest, StartsAtOrder1) {
  BDF<double, 4> integrator;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  EXPECT_EQ(integrator.currentOrder(), 1u);
}

/** @test BDF tracks time correctly. */
TEST(BDFTest, TracksTime) {
  BDF<double, 2> integrator;

  // Simple convergence: always accept after one iteration
  ImplicitOptions<double> opts;
  opts.maxIterations = 1;
  opts.computeJacobian = [](const double& /*x*/, double /*t*/) { return 0.0; };
  opts.linearSolve = [](const double& /*J*/, const double& rhs) { return rhs; };
  opts.converged = [](const double& /*delta*/, const double& /*residual*/) {
    return true; // Always converge immediately
  };

  auto f = [](const double& /*y*/, double /*t*/) { return 0.0; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  EXPECT_DOUBLE_EQ(integrator.time(), 0.0);

  integrator.step(y, 0.1, opts);
  EXPECT_NEAR(integrator.time(), 0.1, 1e-10);

  integrator.step(y, 0.1, opts);
  EXPECT_NEAR(integrator.time(), 0.2, 1e-10);
}

/** @test BDF reset clears state. */
TEST(BDFTest, ResetClearsState) {
  BDF<double, 4> integrator;

  ImplicitOptions<double> opts;
  opts.maxIterations = 1;
  opts.computeJacobian = [](const double& /*x*/, double /*t*/) { return 0.0; };
  opts.linearSolve = [](const double& /*J*/, const double& rhs) { return rhs; };
  opts.converged = [](const double& /*delta*/, const double& /*residual*/) { return true; };

  auto f = [](const double& /*y*/, double /*t*/) { return 0.0; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  // Take some steps
  for (int i = 0; i < 5; ++i) {
    integrator.step(y, 0.1, opts);
  }

  EXPECT_GT(integrator.currentOrder(), 1u);
  EXPECT_GT(integrator.time(), 0.0);

  integrator.reset(5.0);

  EXPECT_EQ(integrator.currentOrder(), 1u);
  EXPECT_DOUBLE_EQ(integrator.time(), 5.0);
  EXPECT_EQ(integrator.stats().steps, 0u);
}

/* ---------------------------- Type Alias Tests ----------------------------- */

/** @test BDF3 type alias works correctly. */
TEST(BDFTest, BDF3TypeAlias) {
  BDF3<double> integrator;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  EXPECT_EQ(integrator.currentOrder(), 1u);
}

/** @test BDF4 type alias works correctly. */
TEST(BDFTest, BDF4TypeAlias) {
  BDF4<double> integrator;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  EXPECT_EQ(integrator.currentOrder(), 1u);
}
