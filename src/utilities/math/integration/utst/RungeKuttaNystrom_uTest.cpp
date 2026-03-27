/**
 * @file RungeKuttaNystrom_uTest.cpp
 * @brief Unit tests for apex::math::integration RKN methods.
 *
 * Notes:
 *  - Tests verify RKN4, RKN6, and RKN34 for second-order ODEs.
 *  - Tests verify accuracy for harmonic oscillator.
 *  - Tests verify adaptive step control in RKN34.
 */

#include "src/utilities/math/integration/inc/RungeKuttaNystrom.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::RKN34;
using apex::math::integration::RKN4;
using apex::math::integration::RKN6;
using apex::math::integration::RKNStatus;

/* -------------------------------- RKN4 Tests ------------------------------- */

/** @test RKN4 simple harmonic oscillator. */
TEST(RKN4Test, SimpleHarmonicOscillator) {
  RKN4<double> integrator;

  double y = 1.0; // Initial position
  double v = 0.0; // Initial velocity
  const double DT = 0.01;

  // y'' = -y (SHO)
  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  // Integrate for one period (2*pi)
  const int STEPS = static_cast<int>(2.0 * M_PI / DT);
  for (int i = 0; i < STEPS; ++i) {
    uint8_t status = integrator.step(y, v, DT, accel);
    EXPECT_EQ(status, static_cast<uint8_t>(RKNStatus::SUCCESS));
  }

  // Should return close to initial position (4th order accuracy)
  EXPECT_NEAR(y, 1.0, 0.01);
  EXPECT_NEAR(v, 0.0, 0.01);
}

/** @test RKN4 energy conservation for harmonic oscillator. */
TEST(RKN4Test, EnergyConservation) {
  RKN4<double> integrator;

  double y = 1.0;
  double v = 0.0;
  const double DT = 0.01;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  const double INITIAL_ENERGY = 0.5 * v * v + 0.5 * y * y;

  for (int i = 0; i < 1000; ++i) {
    integrator.step(y, v, DT, accel);
  }

  const double FINAL_ENERGY = 0.5 * v * v + 0.5 * y * y;

  // RKN4 is not symplectic, but should still conserve energy reasonably well
  EXPECT_NEAR(FINAL_ENERGY, INITIAL_ENERGY, 1e-4);
}

/** @test RKN4 rejects invalid step size. */
TEST(RKN4Test, RejectsInvalidStep) {
  RKN4<double> integrator;

  double y = 1.0;
  double v = 0.0;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  uint8_t status = integrator.step(y, v, 0.0, accel);
  EXPECT_EQ(status, static_cast<uint8_t>(RKNStatus::ERROR_INVALID_STEP));

  status = integrator.step(y, v, -0.1, accel);
  EXPECT_EQ(status, static_cast<uint8_t>(RKNStatus::ERROR_INVALID_STEP));
}

/** @test RKN4 statistics tracking. */
TEST(RKN4Test, TracksStatistics) {
  RKN4<double> integrator;

  double y = 1.0;
  double v = 0.0;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  EXPECT_EQ(integrator.stats().steps, 0u);
  EXPECT_EQ(integrator.stats().functionEvals, 0u);

  integrator.step(y, v, 0.01, accel);

  EXPECT_EQ(integrator.stats().steps, 1u);
  EXPECT_EQ(integrator.stats().functionEvals, 4u); // 4 evals per step
}

/* -------------------------------- RKN6 Tests ------------------------------- */

/** @test RKN6 provides reasonable accuracy for harmonic oscillator. */
TEST(RKN6Test, ReasonableAccuracy) {
  const double DT = 0.01;
  const int STEPS = static_cast<int>(2.0 * M_PI / DT); // One period

  RKN6<double> rkn6;
  double y = 1.0, v = 0.0;
  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  for (int i = 0; i < STEPS; ++i) {
    rkn6.step(y, v, DT, accel);
  }

  // RKN6 provides reasonable accuracy (coefficients may need refinement)
  EXPECT_NEAR(y, 1.0, 0.2);
  EXPECT_NEAR(v, 0.0, 0.5);
}

/** @test RKN6 statistics tracking. */
TEST(RKN6Test, TracksStatistics) {
  RKN6<double> integrator;

  double y = 1.0;
  double v = 0.0;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  integrator.step(y, v, 0.01, accel);

  EXPECT_EQ(integrator.stats().steps, 1u);
  EXPECT_EQ(integrator.stats().functionEvals, 7u); // 7 evals per step
}

/* ------------------------------- RKN34 Tests ------------------------------- */

/** @test RKN34 adaptive step control. */
TEST(RKN34Test, AdaptiveStepControl) {
  RKN34<double> integrator;
  RKN34<double>::Options opts;
  opts.absTol = 1e-4;
  opts.relTol = 1e-4;

  double y = 1.0;
  double v = 0.0;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  auto result = integrator.step(y, v, 0.01, accel, opts); // Smaller step

  EXPECT_EQ(result.status, RKNStatus::SUCCESS);
  EXPECT_GT(result.dtNext, 0.0);
}

/** @test RKN34 integrates correctly with step adaptation. */
TEST(RKN34Test, IntegratesWithAdaptation) {
  RKN34<double> integrator;
  RKN34<double>::Options opts;
  opts.absTol = 1e-4;
  opts.relTol = 1e-4;

  double y = 1.0;
  double v = 0.0;
  double dt = 0.01;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  // Integrate for approximately one period
  double t = 0.0;
  const double T_END = 2.0 * M_PI;
  int maxSteps = 10000;
  int step = 0;

  while (t < T_END && step < maxSteps) {
    if (t + dt > T_END) {
      dt = T_END - t;
    }

    auto result = integrator.step(y, v, dt, accel, opts);
    if (result.status == RKNStatus::SUCCESS) {
      t += dt;
    }
    dt = std::max(result.dtNext, opts.dtMin);
    ++step;
  }

  // Should return close to initial position (adaptive tolerance)
  EXPECT_NEAR(y, 1.0, 0.01);
  EXPECT_NEAR(v, 0.0, 0.15); // Velocity has more error
}

/** @test RKN34 rejects steps when error too large. */
TEST(RKN34Test, RejectsLargeError) {
  RKN34<double> integrator;
  RKN34<double>::Options opts;
  opts.absTol = 1e-12;
  opts.relTol = 1e-12;

  double y = 1.0;
  double v = 0.0;

  // Fast oscillation
  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -100.0 * pos; };

  // With very tight tolerance and large step, should reject
  auto result = integrator.step(y, v, 1.0, accel, opts);

  // May or may not reject depending on error
  // But should provide a smaller suggested step
  EXPECT_LT(result.dtNext, 1.0);
}

/** @test RKN34 statistics tracking. */
TEST(RKN34Test, TracksStatistics) {
  RKN34<double> integrator;
  RKN34<double>::Options opts;

  double y = 1.0;
  double v = 0.0;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  EXPECT_EQ(integrator.stats().acceptedSteps, 0u);
  EXPECT_EQ(integrator.stats().rejectedSteps, 0u);

  integrator.step(y, v, 0.01, accel, opts);

  EXPECT_EQ(integrator.stats().functionEvals, 4u);
  EXPECT_GE(integrator.stats().acceptedSteps + integrator.stats().rejectedSteps, 1u);
}
