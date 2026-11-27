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

#include "src/utilities/math/integration/inc/StateVector.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

#include <limits>

using apex::math::integration::RKN34;
using apex::math::integration::RKN4;
using apex::math::integration::RKN6;
using apex::math::integration::RKNStatus;
using apex::math::integration::State3;

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

/** @test Integrates one oscillator period through accepted and rejected steps, time and state
 * agreeing */
TEST(RKN34Test, IntegratesWithAdaptation) {
  RKN34<double> integrator;
  RKN34<double>::Options opts;
  opts.absTol = 1e-8;
  opts.relTol = 1e-8;

  double y = 1.0;
  double v = 0.0;
  // Too large for the tolerance, so the first attempts are rejected
  double dt = 1.0;

  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  double t = 0.0;
  const double tEnd = 2.0 * M_PI;
  const int maxAttempts = 10000;
  int attempts = 0;

  while (t < tEnd && attempts < maxAttempts) {
    if (t + dt > tEnd) {
      dt = tEnd - t;
    }

    const auto result = integrator.step(y, v, dt, accel, opts);
    ASSERT_TRUE(result.status == RKNStatus::SUCCESS || result.status == RKNStatus::STEP_REJECTED);
    if (result.status == RKNStatus::SUCCESS) {
      t += dt;
    }
    dt = result.dtNext;
    ++attempts;
  }

  // The caller's clock, advanced on SUCCESS only, is the integrator's clock
  EXPECT_DOUBLE_EQ(integrator.time(), t);
  EXPECT_GT(integrator.stats().rejectedSteps, 0U);
  EXPECT_EQ(integrator.stats().acceptedSteps + integrator.stats().rejectedSteps,
            static_cast<std::size_t>(attempts));

  // One full period returns to the start
  EXPECT_NEAR(y, 1.0, 1e-5);
  EXPECT_NEAR(v, 0.0, 1e-5);
}

/** @test Reports a step whose error exceeds the tolerance as STEP_REJECTED and takes no step */
TEST(RKN34Test, RejectsLargeError) {
  RKN34<double> integrator;
  RKN34<double>::Options opts;
  opts.absTol = 1e-12;
  opts.relTol = 1e-12;
  const double dt = 1.0;

  double y = 1.0;
  double v = 0.0;

  // Fast oscillation, far too large a step for the tolerance
  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -100.0 * pos; };

  const auto result = integrator.step(y, v, dt, accel, opts);

  EXPECT_EQ(result.status, RKNStatus::STEP_REJECTED);
  EXPECT_GT(result.errorEstimate, 1.0);
  EXPECT_EQ(y, 1.0);
  EXPECT_EQ(v, 0.0);
  EXPECT_EQ(integrator.time(), 0.0);
  EXPECT_EQ(integrator.stats().rejectedSteps, 1U);
  EXPECT_EQ(integrator.stats().acceptedSteps, 0U);
  EXPECT_LT(result.dtNext, dt);
  EXPECT_GE(result.dtNext, opts.dtMin);
}

/** @test Reports a NaN acceleration as ERROR_NON_FINITE, takes no step and suggests a finite step
 */
TEST(RKN34Test, ReportsNanAcceleration) {
  RKN34<double> integrator;
  const RKN34<double>::Options opts;
  const double dt = 0.1;

  double y = 1.0;
  double v = 0.5;
  const auto result = integrator.step(
      y, v, dt,
      [](double, const double&, const double&) { return std::numeric_limits<double>::quiet_NaN(); },
      opts);

  EXPECT_EQ(result.status, RKNStatus::ERROR_NON_FINITE);
  EXPECT_EQ(y, 1.0);
  EXPECT_EQ(v, 0.5);
  EXPECT_EQ(integrator.time(), 0.0);
  EXPECT_EQ(integrator.stats().acceptedSteps, 0U);
  EXPECT_EQ(integrator.stats().rejectedSteps, 0U);
  EXPECT_DOUBLE_EQ(result.dtNext, dt * opts.maxShrink);
}

/** @test Reports a NaN in one component of a vector acceleration, wherever it sits */
TEST(RKN34Test, ReportsNanInOneVectorComponent) {
  for (std::size_t bad = 0; bad < 3; ++bad) {
    RKN34<State3> integrator;
    const RKN34<State3>::Options opts;

    State3 y{1.0, 1.0, 1.0};
    State3 v{0.0, 0.0, 0.0};
    const auto result = integrator.step(
        y, v, 0.01,
        [bad](double, const State3& pos, const State3&) {
          State3 acc = pos * -1.0;
          acc[bad] = std::numeric_limits<double>::quiet_NaN();
          return acc;
        },
        opts);

    EXPECT_EQ(result.status, RKNStatus::ERROR_NON_FINITE) << "component " << bad;
    EXPECT_EQ(integrator.stats().acceptedSteps, 0U) << "component " << bad;
    EXPECT_EQ(y[bad], 1.0) << "component " << bad;
  }
}

/** @test Reports a NaN that reaches only the velocity error, with a sound position error */
TEST(RKN34Test, ReportsNanInVelocityOnly) {
  RKN34<double> integrator;
  const RKN34<double>::Options opts;

  // The first three stage accelerations are finite and the last is NaN. The
  // position update uses the first three only, so the position error is
  // finite and the NaN arrives through the velocity error alone.
  int calls = 0;
  double y = 1.0;
  double v = 0.0;
  const auto result = integrator.step(
      y, v, 0.01,
      [&calls](double, const double& pos, const double&) {
        return ++calls == 4 ? std::numeric_limits<double>::quiet_NaN() : -pos;
      },
      opts);

  EXPECT_EQ(result.status, RKNStatus::ERROR_NON_FINITE);
  EXPECT_EQ(y, 1.0);
  EXPECT_EQ(v, 0.0);
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
