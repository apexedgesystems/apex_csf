/**
 * @file Integrators_uTest.cpp
 * @brief Unit tests for ForwardEuler + RK4 integrators.
 *
 * Tests use scalar-state derivatives whose analytic solutions are known,
 * so we can verify error scaling vs step size and order accuracy.
 *
 * Test ODEs:
 *   1. dy/dt = -k*y      (exponential decay)         analytic: y(t) = y0 * exp(-k*t)
 *   2. dy/dt = cos(t)    (trig integration)          analytic: y(t) = y0 + sin(t)
 */

#include "src/sim/dynamics/integrators/inc/ForwardEuler.hpp"
#include "src/sim/dynamics/integrators/inc/RK4.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::dynamics::integrators::stepForwardEuler;
using sim::dynamics::integrators::stepRK4;

namespace {

// Minimal scalar state with the operators the integrators expect.
struct Scalar {
  double v = 0.0;

  Scalar operator+(const Scalar& other) const { return {v + other.v}; }
  Scalar operator*(double k) const { return {v * k}; }
};

} // namespace

/* ----------------------------- ForwardEuler ----------------------------- */

/** @test ForwardEuler integrates dy/dt = -k*y close to analytic exp(-k*t). */
TEST(ForwardEulerTest, ExponentialDecayConvergesAsStepShrinks) {
  const double k = 0.5;
  const double y0 = 1.0;
  const double t_final = 2.0;

  auto deriv = [k](double, Scalar y) { return Scalar{-k * y.v}; };

  // Coarse step: Euler accumulates O(dt) error.
  Scalar coarse{y0};
  const double dt_coarse = 0.1;
  for (double t = 0.0; t < t_final; t += dt_coarse) {
    stepForwardEuler(coarse, deriv, t, dt_coarse);
  }

  // Fine step: error should shrink linearly with dt.
  Scalar fine{y0};
  const double dt_fine = 0.001;
  for (double t = 0.0; t < t_final; t += dt_fine) {
    stepForwardEuler(fine, deriv, t, dt_fine);
  }

  const double exact = y0 * std::exp(-k * t_final);
  const double err_coarse = std::abs(coarse.v - exact);
  const double err_fine = std::abs(fine.v - exact);

  EXPECT_LT(err_fine, err_coarse); // refining helps
  EXPECT_LT(err_fine, 0.01);       // fine step is within 1% of analytic
}

/* ----------------------------- RK4 ----------------------------- */

/** @test RK4 integrates dy/dt = -k*y to better-than-Euler accuracy. */
TEST(RK4Test, ExponentialDecayMatchesAnalyticToManyDigits) {
  const double k = 0.5;
  const double y0 = 1.0;
  const double t_final = 2.0;

  auto deriv = [k](double, Scalar y) { return Scalar{-k * y.v}; };

  Scalar y{y0};
  const double dt = 0.01;
  for (double t = 0.0; t < t_final; t += dt) {
    stepRK4(y, deriv, t, dt);
  }

  const double exact = y0 * std::exp(-k * t_final);
  EXPECT_NEAR(y.v, exact, 1e-9); // 4th-order accuracy easily clears 1e-9 here
}

/** @test RK4 error scales as O(dt^4); halving dt should ~16x reduce error. */
TEST(RK4Test, ErrorScalesAsFourthOrder) {
  // Use cos(t) integration so the derivative is non-trivial in t.
  auto deriv = [](double t, Scalar) { return Scalar{std::cos(t)}; };
  const double t_final = 1.0;

  auto integrate_with_dt = [&](double dt) -> double {
    Scalar y{0.0};
    int n = static_cast<int>(t_final / dt);
    for (int i = 0; i < n; ++i) {
      stepRK4(y, deriv, i * dt, dt);
    }
    return std::abs(y.v - std::sin(t_final));
  };

  const double err_dt = integrate_with_dt(0.1);
  const double err_dt2 = integrate_with_dt(0.05);

  // 4th-order: halving dt cuts error by 2^4 = 16x. Check at least 8x.
  EXPECT_LT(err_dt2 * 8.0, err_dt);
}

/**
 * @test RK4 exhibits a measured fourth-order convergence rate.
 *
 * For a smooth ODE the global error is E(dt) ~ C * dt^p. Halving dt should
 * scale the error by 2^p, so log2(E(dt) / E(dt/2)) -> p. This locks the
 * integrator's order to 4 (a weaker "error shrinks" check would also pass
 * for a buggy 2nd- or 3rd-order method).
 */
TEST(RK4Test, MeasuredConvergenceRateIsFour) {
  auto deriv = [](double t, Scalar) { return Scalar{std::cos(t)}; };
  const double t_final = 1.0;

  auto err_for_dt = [&](double dt) -> double {
    Scalar y{0.0};
    const int n = static_cast<int>(std::lround(t_final / dt));
    for (int i = 0; i < n; ++i) {
      stepRK4(y, deriv, i * dt, dt);
    }
    return std::abs(y.v - std::sin(t_final));
  };

  // Refine three times; each halving should give a rate near 4.
  double dt = 0.1;
  double prev = err_for_dt(dt);
  for (int level = 0; level < 3; ++level) {
    dt *= 0.5;
    const double cur = err_for_dt(dt);
    const double rate = std::log2(prev / cur);
    EXPECT_NEAR(rate, 4.0, 0.25) << "RK4 order at dt=" << dt << " was " << rate;
    prev = cur;
  }
}

/** @test ForwardEuler exhibits a measured first-order convergence rate. */
TEST(ForwardEulerTest, MeasuredConvergenceRateIsOne) {
  auto deriv = [](double t, Scalar) { return Scalar{std::cos(t)}; };
  const double t_final = 1.0;

  auto err_for_dt = [&](double dt) -> double {
    Scalar y{0.0};
    const int n = static_cast<int>(std::lround(t_final / dt));
    for (int i = 0; i < n; ++i) {
      stepForwardEuler(y, deriv, i * dt, dt);
    }
    return std::abs(y.v - std::sin(t_final));
  };

  double dt = 0.01;
  double prev = err_for_dt(dt);
  for (int level = 0; level < 3; ++level) {
    dt *= 0.5;
    const double cur = err_for_dt(dt);
    const double rate = std::log2(prev / cur);
    EXPECT_NEAR(rate, 1.0, 0.15) << "Euler order at dt=" << dt << " was " << rate;
    prev = cur;
  }
}

/** @test Both integrators preserve a constant-input identity (zero derivative). */
TEST(IntegratorsTest, ZeroDerivativeIsAnIdentityStep) {
  auto deriv = [](double, Scalar) { return Scalar{0.0}; };
  Scalar y_e{42.0}, y_r{42.0};
  stepForwardEuler(y_e, deriv, 0.0, 0.5);
  stepRK4(y_r, deriv, 0.0, 0.5);
  EXPECT_DOUBLE_EQ(y_e.v, 42.0);
  EXPECT_DOUBLE_EQ(y_r.v, 42.0);
}
