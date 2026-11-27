/**
 * @file Rosenbrock_uTest.cpp
 * @brief Unit tests for apex::math::integration::ROS2 and ROS3.
 *
 * Notes:
 *  - Accuracy is checked as a measured order against analytic solutions, on
 *    linear, nonlinear and time-dependent problems.
 *  - The options supply the matrix I - gamma*dt*J for the problem's Jacobian.
 */

#include "src/utilities/math/integration/inc/Rosenbrock.hpp"

#include "src/utilities/math/integration/utst/support/ConvergenceOrder.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using apex::math::integration::ROS2;
using apex::math::integration::ROS3;
using apex::math::integration::RosenbrockOptions;
using apex::math::integration::RosenbrockStatus;
using apex::math::integration::test::observedOrder;

namespace {

/** Options for y' = rate*y: the matrix is 1 - gamma*dt*rate. */
RosenbrockOptions<double> decayOptions(double rate) {
  RosenbrockOptions<double> opts;
  opts.computeMatrix = [rate](const double&, double, double gammaDt) {
    return 1.0 - gammaDt * rate;
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
  return opts;
}

/**
 * Error at t = 1 of the forced problem y' = -y + sin(3t), y(0) = 0, whose
 * solution is (sin(3t) - 3*cos(3t) + 3*exp(-t)) / 10, with or without the
 * time derivative df/dt = 3*cos(3t) given to the method.
 */
template <typename Method> double forcedError(int steps, bool withTimeDerivative) {
  Method integrator;
  auto opts = decayOptions(-1.0);
  if (withTimeDerivative) {
    opts.timeDerivative = [](const double&, double t) { return 3.0 * std::cos(3.0 * t); };
  }
  integrator.initialize([](const double& y, double t) { return -y + std::sin(3.0 * t); }, 0.0, 0.0);
  double y = 0.0;
  for (int i = 0; i < steps; ++i) {
    integrator.step(y, 1.0 / steps, opts);
  }
  return std::fabs(y - (std::sin(3.0) - 3.0 * std::cos(3.0) + 3.0 * std::exp(-1.0)) / 10.0);
}

/**
 * Error at t = 1 of the stiff problem y' = -1000*(y - cos(t)) - sin(t),
 * y(0) = 1, whose solution is cos(t), with or without df/dt given.
 */
template <typename Method> double stiffError(int steps, bool withTimeDerivative) {
  Method integrator;
  auto opts = decayOptions(-1000.0);
  if (withTimeDerivative) {
    opts.timeDerivative = [](const double&, double t) {
      return -1000.0 * std::sin(t) - std::cos(t);
    };
  }
  integrator.initialize(
      [](const double& y, double t) { return -1000.0 * (y - std::cos(t)) - std::sin(t); }, 1.0,
      0.0);
  double y = 1.0;
  for (int i = 0; i < steps; ++i) {
    integrator.step(y, 1.0 / steps, opts);
  }
  return std::fabs(y - std::cos(1.0));
}

} // namespace

/* ------------------------------- ROS2 Tests -------------------------------- */

/** @test Converges at second order on a linear and on a nonlinear problem */
TEST(ROS2Test, ConvergesAtSecondOrder) {
  const auto decayError = [](int steps) {
    ROS2<double> integrator;
    const auto opts = decayOptions(-1.0);
    integrator.initialize([](const double& y, double) { return -y; }, 1.0, 0.0);
    double y = 1.0;
    for (int i = 0; i < steps; ++i) {
      integrator.step(y, 1.0 / steps, opts);
    }
    return std::fabs(y - std::exp(-1.0));
  };
  // y' = y*(1 - y), y(0) = 0.1: y(t) = 1 / (1 + 9*exp(-t)); df/dy = 1 - 2y
  const auto logisticError = [](int steps) {
    ROS2<double> integrator;
    RosenbrockOptions<double> opts;
    opts.computeMatrix = [](const double& x, double, double gammaDt) {
      return 1.0 - gammaDt * (1.0 - 2.0 * x);
    };
    opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
    integrator.initialize([](const double& y, double) { return y * (1.0 - y); }, 0.1, 0.0);
    double y = 0.1;
    for (int i = 0; i < steps; ++i) {
      integrator.step(y, 2.0 / steps, opts);
    }
    return std::fabs(y - 1.0 / (1.0 + 9.0 * std::exp(-2.0)));
  };

  for (const double measured : {observedOrder(decayError, 40), observedOrder(logisticError, 40)}) {
    EXPECT_GT(measured, 1.8);
    EXPECT_LT(measured, 2.3);
  }
}

/** @test Keeps second order on a stiff time-dependent problem when given df/dt, first order without
 */
TEST(ROS2Test, StiffTimeDependentProblemNeedsTheTimeDerivative) {
  const double withIt =
      observedOrder([](int steps) { return stiffError<ROS2<double>>(steps, true); }, 20);
  EXPECT_GT(withIt, 1.8);
  EXPECT_LT(withIt, 2.4);

  const double without =
      observedOrder([](int steps) { return stiffError<ROS2<double>>(steps, false); }, 20);
  EXPECT_LT(without, 1.3);

  // Second order on a time-dependent problem that is not stiff, with or without df/dt
  for (const bool given : {true, false}) {
    const double measured =
        observedOrder([given](int steps) { return forcedError<ROS2<double>>(steps, given); }, 40);
    EXPECT_GT(measured, 1.8) << "df/dt given: " << given;
    EXPECT_LT(measured, 2.3) << "df/dt given: " << given;
  }
}

/** @test ROS2 tracks statistics correctly. */
TEST(ROS2Test, TracksStatistics) {
  ROS2<double> integrator;

  RosenbrockOptions<double> opts;
  opts.computeMatrix = [](const double& /*x*/, double /*t*/, double gammaDt) {
    return 1.0 + gammaDt;
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  EXPECT_EQ(integrator.stats().steps, 0u);
  EXPECT_EQ(integrator.stats().matrixEvals, 0u);

  integrator.step(y, 0.01, opts);

  EXPECT_EQ(integrator.stats().steps, 1u);
  EXPECT_EQ(integrator.stats().matrixEvals, 1u);   // 1 per step
  EXPECT_EQ(integrator.stats().functionEvals, 2u); // 2 per step
  EXPECT_EQ(integrator.stats().linearSolves, 2u);  // 2 per step
}

/** @test ROS2 reset clears statistics. */
TEST(ROS2Test, ResetClearsStats) {
  ROS2<double> integrator;

  RosenbrockOptions<double> opts;
  opts.computeMatrix = [](const double& /*x*/, double /*t*/, double gammaDt) {
    return 1.0 + gammaDt;
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);
  integrator.step(y, 0.01, opts);

  integrator.reset(5.0);

  EXPECT_EQ(integrator.stats().steps, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), 5.0);
}

/* -------------------------------- ROS3 Tests ------------------------------- */

/** @test Matches the exponential to the accuracy of a third-order step */
TEST(ROS3Test, SingleStepOfDecay) {
  ROS3<double> integrator;
  const auto opts = decayOptions(-1.0);
  integrator.initialize([](const double& y, double) { return -y; }, 1.0, 0.0);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, 0.01, opts), static_cast<std::uint8_t>(RosenbrockStatus::SUCCESS));

  // The local error of a third-order method is O(dt^4): 1e-8 at dt = 0.01
  EXPECT_NEAR(y, std::exp(-0.01), 1e-8);
  EXPECT_DOUBLE_EQ(integrator.time(), 0.01);
}

/** @test Converges at third order on a linear problem */
TEST(ROS3Test, ConvergesAtThirdOrderOnDecay) {
  const auto errorAtOne = [](int steps) {
    ROS3<double> integrator;
    const auto opts = decayOptions(-1.0);
    integrator.initialize([](const double& y, double) { return -y; }, 1.0, 0.0);
    double y = 1.0;
    for (int i = 0; i < steps; ++i) {
      integrator.step(y, 1.0 / steps, opts);
    }
    return std::fabs(y - std::exp(-1.0));
  };

  const double measured = observedOrder(errorAtOne, 20);
  EXPECT_GT(measured, 2.8);
  EXPECT_LT(measured, 3.3);
}

/** @test Converges at third order on a nonlinear problem, with the Jacobian taken at each step */
TEST(ROS3Test, ConvergesAtThirdOrderOnLogisticGrowth) {
  // y' = y*(1 - y), y(0) = 0.1: y(t) = 1 / (1 + 9*exp(-t)); df/dy = 1 - 2y
  const auto errorAtTwo = [](int steps) {
    ROS3<double> integrator;
    RosenbrockOptions<double> opts;
    opts.computeMatrix = [](const double& x, double, double gammaDt) {
      return 1.0 - gammaDt * (1.0 - 2.0 * x);
    };
    opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };
    integrator.initialize([](const double& y, double) { return y * (1.0 - y); }, 0.1, 0.0);
    double y = 0.1;
    for (int i = 0; i < steps; ++i) {
      integrator.step(y, 2.0 / steps, opts);
    }
    return std::fabs(y - 1.0 / (1.0 + 9.0 * std::exp(-2.0)));
  };

  const double measured = observedOrder(errorAtTwo, 20);
  EXPECT_GT(measured, 2.8);
  EXPECT_LT(measured, 3.3);
}

/** @test Converges at third order on a time-dependent problem when given df/dt, and below it
 * without */
TEST(ROS3Test, TimeDependentProblemNeedsTheTimeDerivative) {
  const double withIt =
      observedOrder([](int steps) { return forcedError<ROS3<double>>(steps, true); }, 40);
  EXPECT_GT(withIt, 2.8);
  EXPECT_LT(withIt, 3.3);

  const double without =
      observedOrder([](int steps) { return forcedError<ROS3<double>>(steps, false); }, 40);
  EXPECT_LT(without, 2.4);
}

/** @test Damps a stiff component to almost nothing in one step far above its time constant */
TEST(ROS3Test, DampsAStiffComponentInOneStep) {
  const double rate = -1.0e6;
  ROS3<double> integrator;
  const auto opts = decayOptions(rate);
  integrator.initialize([rate](const double& y, double) { return rate * y; }, 1.0, 0.0);

  double y = 1.0;
  ASSERT_EQ(integrator.step(y, 0.1, opts), static_cast<std::uint8_t>(RosenbrockStatus::SUCCESS));

  EXPECT_LT(std::fabs(y), 1e-4);
}

/** @test Counts one matrix, two derivative evaluations and three solves per step */
TEST(ROS3Test, TracksStatistics) {
  ROS3<double> integrator;
  const auto opts = decayOptions(-1.0);
  integrator.initialize([](const double& y, double) { return -y; }, 1.0, 0.0);

  double y = 1.0;
  integrator.step(y, 0.01, opts);
  integrator.step(y, 0.01, opts);

  EXPECT_EQ(integrator.stats().steps, 2U);
  EXPECT_EQ(integrator.stats().matrixEvals, 2U);
  EXPECT_EQ(integrator.stats().functionEvals, 4U);
  EXPECT_EQ(integrator.stats().linearSolves, 6U);
}

/** @test Calls the time derivative once per step, at the start of the step */
TEST(ROS3Test, EvaluatesTheTimeDerivativeOncePerStep) {
  ROS3<double> integrator;
  auto opts = decayOptions(-1.0);
  int calls = 0;
  double seenAt = -1.0;
  opts.timeDerivative = [&calls, &seenAt](const double&, double t) {
    ++calls;
    seenAt = t;
    return 0.0;
  };
  integrator.initialize([](const double& y, double) { return -y; }, 1.0, 2.0);

  double y = 1.0;
  integrator.step(y, 0.01, opts);
  EXPECT_EQ(calls, 1);
  EXPECT_DOUBLE_EQ(seenAt, 2.0);
  integrator.step(y, 0.01, opts);
  EXPECT_EQ(calls, 2);
  EXPECT_DOUBLE_EQ(seenAt, 2.01);
}

/** @test Clears the statistics and sets the time on reset */
TEST(ROS3Test, ResetClearsStats) {
  ROS3<double> integrator;
  const auto opts = decayOptions(-1.0);
  integrator.initialize([](const double& y, double) { return -y; }, 1.0, 0.0);
  double y = 1.0;
  integrator.step(y, 0.01, opts);

  integrator.reset(5.0);

  EXPECT_EQ(integrator.stats().steps, 0U);
  EXPECT_EQ(integrator.stats().functionEvals, 0U);
  EXPECT_DOUBLE_EQ(integrator.time(), 5.0);
}
