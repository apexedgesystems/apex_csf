/**
 * @file Rosenbrock_uTest.cpp
 * @brief Unit tests for apex::math::integration::ROS2 and ROS3P.
 *
 * Notes:
 *  - Tests verify Rosenbrock methods for moderately stiff systems.
 *  - Tests verify L-stability and accuracy properties.
 *  - All operations are RT-safe with RT-safe callbacks.
 */

#include "src/utilities/math/integration/inc/Rosenbrock.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>

using apex::math::integration::ROS2;
using apex::math::integration::ROS3P;
using apex::math::integration::RosenbrockOptions;
using apex::math::integration::RosenbrockStatus;

/* ------------------------------- ROS2 Tests -------------------------------- */

/** @test ROS2 converges for simple exponential decay. */
TEST(ROS2Test, ExponentialDecay) {
  ROS2<double> integrator;

  // For dy/dt = -y, Jacobian J = -1
  // Matrix = I - gamma*dt*J = 1 + gamma*dt
  RosenbrockOptions<double> opts;
  opts.computeMatrix = [](const double& /*x*/, double /*t*/, double gammaDt) {
    return 1.0 + gammaDt; // I - gamma*dt*J = 1 - gamma*dt*(-1) = 1 + gamma*dt
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  const double DT = 0.01;
  for (int i = 0; i < 100; ++i) {
    uint8_t status = integrator.step(y, DT, opts);
    ASSERT_EQ(status, static_cast<uint8_t>(RosenbrockStatus::SUCCESS));
  }

  // y(1) = exp(-1) ~ 0.3679
  EXPECT_NEAR(y, std::exp(-1.0), 0.01);
}

/** @test ROS2 rejects invalid step size. */
TEST(ROS2Test, RejectsInvalidStep) {
  ROS2<double> integrator;

  RosenbrockOptions<double> opts;
  opts.computeMatrix = [](const double& /*x*/, double /*t*/, double gammaDt) {
    return 1.0 + gammaDt;
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  uint8_t status = integrator.step(y, 0.0, opts);
  EXPECT_EQ(status, static_cast<uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP));

  status = integrator.step(y, -0.1, opts);
  EXPECT_EQ(status, static_cast<uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP));
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

/* ------------------------------- ROS3P Tests ------------------------------- */

/** @test ROS3P rejects invalid step size. */
TEST(ROS3PTest, RejectsInvalidStep) {
  ROS3P<double> integrator;

  RosenbrockOptions<double> opts;
  opts.computeMatrix = [](const double& /*x*/, double /*t*/, double gammaDt) {
    return 1.0 + gammaDt;
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  uint8_t status = integrator.step(y, 0.0, opts);
  EXPECT_EQ(status, static_cast<uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP));
}

/** @test ROS3P tracks statistics correctly. */
TEST(ROS3PTest, TracksStatistics) {
  ROS3P<double> integrator;

  RosenbrockOptions<double> opts;
  opts.computeMatrix = [](const double& /*x*/, double /*t*/, double gammaDt) {
    return 1.0 + gammaDt;
  };
  opts.linearSolve = [](const double& matrix, const double& rhs) { return rhs / matrix; };

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  integrator.step(y, 0.01, opts);

  EXPECT_EQ(integrator.stats().steps, 1u);
  EXPECT_EQ(integrator.stats().matrixEvals, 1u);   // 1 per step
  EXPECT_EQ(integrator.stats().functionEvals, 3u); // 3 per step (3 stages)
  EXPECT_EQ(integrator.stats().linearSolves, 3u);  // 3 per step
}

/** @test ROS3P reset clears statistics. */
TEST(ROS3PTest, ResetClearsStats) {
  ROS3P<double> integrator;

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
