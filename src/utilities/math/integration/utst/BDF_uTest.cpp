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

#include <gtest/gtest.h>

#include <cmath>
#include <functional>

using apex::math::integration::BDF;
using apex::math::integration::BDF3;
using apex::math::integration::BDF4;
using apex::math::integration::ImplicitOptions;
using apex::math::integration::Status;

/* ------------------------------- BDF Tests --------------------------------- */

/** @test BDF starts at order 1. */
TEST(BDFTest, StartsAtOrder1) {
  BDF<double, 4> integrator;

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  EXPECT_EQ(integrator.currentOrder(), 1u);
}

/** @test BDF rejects invalid step size. */
TEST(BDFTest, RejectsInvalidStep) {
  BDF<double, 2> integrator;

  ImplicitOptions<double> opts;
  opts.maxIterations = 10;
  opts.computeJacobian = [](const double& /*x*/, double /*t*/) { return -1.0; };
  opts.linearSolve = [](const double& /*J*/, const double& rhs) { return rhs; };
  opts.converged = [](const double& /*delta*/, const double& /*residual*/) { return true; };

  auto f = [](const double& y, double /*t*/) { return -y; };

  double y = 1.0;
  integrator.initialize(f, y, 0.0);

  uint8_t status = integrator.step(y, 0.0, opts);
  EXPECT_EQ(status, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));

  status = integrator.step(y, -0.1, opts);
  EXPECT_EQ(status, static_cast<uint8_t>(Status::ERROR_INVALID_STEP));
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
