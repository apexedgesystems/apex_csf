/**
 * @file Leapfrog_uTest.cpp
 * @brief Unit tests for apex::math::integration::Leapfrog and VelocityVerlet.
 *
 * Notes:
 *  - Tests verify symplectic property (energy conservation).
 *  - Tests verify accuracy for simple harmonic oscillator.
 *  - All operations are RT-safe (zero allocation per step).
 */

#include "src/utilities/math/integration/inc/Leapfrog.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::Leapfrog;
using apex::math::integration::LeapfrogStatus;
using apex::math::integration::VelocityVerlet;

/* ------------------------------ Leapfrog Tests ----------------------------- */

/** @test Leapfrog step with simple harmonic oscillator. */
TEST(LeapfrogTest, SimpleHarmonicOscillator) {
  Leapfrog<double> integrator;

  double x = 1.0;
  double v = 0.0;
  const double DT = 0.01;

  auto accel = [](const double& pos) { return -pos; };

  // Integrate for one period (2*pi seconds)
  const int STEPS = static_cast<int>(2.0 * M_PI / DT);
  for (int i = 0; i < STEPS; ++i) {
    uint8_t status = integrator.step(x, v, DT, accel);
    EXPECT_EQ(status, static_cast<uint8_t>(LeapfrogStatus::SUCCESS));
  }

  // Should return close to initial position (x=1, v=0)
  EXPECT_NEAR(x, 1.0, 0.01);
  EXPECT_NEAR(v, 0.0, 0.01);
}

/** @test Leapfrog conserves energy for harmonic oscillator. */
TEST(LeapfrogTest, EnergyConservation) {
  Leapfrog<double> integrator;

  double x = 1.0;
  double v = 0.0;
  const double DT = 0.01;

  auto accel = [](const double& pos) { return -pos; };

  // Initial energy: E = 0.5*v^2 + 0.5*x^2 = 0.5
  const double INITIAL_ENERGY = 0.5 * v * v + 0.5 * x * x;

  // Integrate for many periods
  for (int i = 0; i < 10000; ++i) {
    integrator.step(x, v, DT, accel);
  }

  const double FINAL_ENERGY = 0.5 * v * v + 0.5 * x * x;

  // Symplectic integrator conserves energy well (bounded drift, no growth)
  EXPECT_NEAR(FINAL_ENERGY, INITIAL_ENERGY, 1e-4);
}

/** @test Leapfrog rejects invalid step size. */
TEST(LeapfrogTest, RejectsInvalidStepSize) {
  Leapfrog<double> integrator;

  double x = 1.0;
  double v = 0.0;

  auto accel = [](const double& pos) { return -pos; };

  // Zero dt
  uint8_t status = integrator.step(x, v, 0.0, accel);
  EXPECT_EQ(status, static_cast<uint8_t>(LeapfrogStatus::ERROR_INVALID_STEP));

  // Negative dt
  status = integrator.step(x, v, -0.1, accel);
  EXPECT_EQ(status, static_cast<uint8_t>(LeapfrogStatus::ERROR_INVALID_STEP));
}

/** @test Leapfrog statistics tracking. */
TEST(LeapfrogTest, StatsTracking) {
  Leapfrog<double> integrator;

  double x = 1.0;
  double v = 0.0;

  auto accel = [](const double& pos) { return -pos; };

  EXPECT_EQ(integrator.stats().steps, 0u);
  EXPECT_EQ(integrator.stats().accelEvals, 0u);

  integrator.step(x, v, 0.01, accel);

  EXPECT_EQ(integrator.stats().steps, 1u);
  EXPECT_EQ(integrator.stats().accelEvals, 2u); // Two evals per step
}

/** @test Leapfrog reset clears statistics. */
TEST(LeapfrogTest, ResetClearsStats) {
  Leapfrog<double> integrator;

  double x = 1.0;
  double v = 0.0;

  auto accel = [](const double& pos) { return -pos; };

  integrator.step(x, v, 0.01, accel);
  integrator.reset(5.0);

  EXPECT_EQ(integrator.stats().steps, 0u);
  EXPECT_DOUBLE_EQ(integrator.time(), 5.0);
}

/* -------------------------- VelocityVerlet Tests --------------------------- */

/** @test VelocityVerlet with velocity-dependent damping. */
TEST(VelocityVerletTest, DampedOscillator) {
  VelocityVerlet<double> integrator;

  double x = 1.0;
  double v = 0.0;
  const double DT = 0.01;
  const double DAMPING = 0.1;

  // Damped harmonic oscillator: a = -x - damping*v
  auto accel = [DAMPING](const double& pos, const double& vel, double /*t*/) {
    return -pos - DAMPING * vel;
  };

  // Integrate for a while
  for (int i = 0; i < 1000; ++i) {
    integrator.step(x, v, DT, accel);
  }

  // Energy should have decreased due to damping
  const double FINAL_ENERGY = 0.5 * v * v + 0.5 * x * x;
  EXPECT_LT(FINAL_ENERGY, 0.5);
}

/** @test VelocityVerlet statistics tracking. */
TEST(VelocityVerletTest, StatsTracking) {
  VelocityVerlet<double> integrator;

  double x = 1.0;
  double v = 0.0;

  auto accel = [](const double& pos, const double& /*vel*/, double /*t*/) { return -pos; };

  integrator.step(x, v, 0.01, accel);

  EXPECT_EQ(integrator.stats().steps, 1u);
  EXPECT_EQ(integrator.stats().accelEvals, 2u);
}
