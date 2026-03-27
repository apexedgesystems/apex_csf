/**
 * @file LegendrePolynomials_uTest.cpp
 * @brief Unit tests for associated Legendre polynomials and normalized variants.
 *
 * Notes:
 *  - Tests verify mathematical properties and known values.
 *  - Tests are platform-agnostic: assert invariants, not exact implementation details.
 */

#include "src/utilities/math/legendre/inc/AssociatedLegendre.hpp"
#include "src/utilities/math/legendre/inc/Factorials.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using apex::math::legendre::associatedLegendreFunction;
using apex::math::legendre::legendrePolynomialFunction;
using apex::math::legendre::normalizedAssociatedLegendreFunction;
using apex::math::legendre::precomputedFactorials;

/* ----------------------------- File Helpers ----------------------------- */

// transforms and reference implementations
inline double t(double x) noexcept { return std::cos(x); }
inline double fx(double x) noexcept { return x; }

inline double p11(double x) noexcept { return std::sin(x); }
inline double p21(double x) noexcept { return 3 * std::sin(x) * std::cos(x); }
inline double p22(double x) noexcept { return 3 * std::pow(std::sin(x), 2); }
inline double p31(double x) noexcept {
  return std::sin(x) * ((15.0 / 2.0) * std::pow(std::cos(x), 2) - (3.0 / 2.0));
}
inline double p32(double x) noexcept { return 15 * std::pow(std::sin(x), 2) * std::cos(x); }
inline double p33(double x) noexcept { return 15 * std::pow(std::sin(x), 3); }

inline double p20(double x) noexcept { return (3.0 / 2.0) * std::pow(x, 2) - (1.0 / 2.0); }
inline double p30(double x) noexcept { return (5.0 / 2.0) * std::pow(x, 3) - (3.0 / 2.0) * x; }
inline double p40(double x) noexcept {
  return (35.0 / 8.0) * std::pow(x, 4) - (15.0 / 4.0) * std::pow(x, 2) + (3.0 / 8.0);
}
inline double p50(double x) noexcept {
  return (63.0 / 8.0) * std::pow(x, 5) - (35.0 / 4.0) * std::pow(x, 3) + (15.0 / 8.0) * x;
}

/* ----------------------------- API Tests ----------------------------- */

/**
 * @test AssociatedLegendreFunction
 * @brief Verifies unnormalized P_n^m(x) for various degrees and orders.
 */
TEST(MathLegendrePolynomials, AssociatedLegendreFunction) {
  const double TOL = 1e-6;
  const double THETA = M_PI / 8.0;

  EXPECT_NEAR(associatedLegendreFunction(0, 0, t, THETA), 1.0, TOL);
  EXPECT_NEAR(associatedLegendreFunction(1, 0, t, THETA), t(THETA), TOL);
  EXPECT_NEAR(associatedLegendreFunction(1, 1, t, THETA), p11(THETA), TOL);
  EXPECT_NEAR(associatedLegendreFunction(2, 1, t, THETA), p21(THETA), TOL);
  EXPECT_NEAR(associatedLegendreFunction(2, 2, t, THETA), p22(THETA), TOL);
  EXPECT_NEAR(associatedLegendreFunction(3, 1, t, THETA), p31(THETA), TOL);
  EXPECT_NEAR(associatedLegendreFunction(3, 2, t, THETA), p32(THETA), TOL);
  EXPECT_NEAR(associatedLegendreFunction(3, 3, t, THETA), p33(THETA), TOL);
}

/**
 * @test LegendrePolynomial
 * @brief Verifies P_n(x) = P_n^0(x) for n = 0...5.
 */
TEST(MathLegendrePolynomials, LegendrePolynomial) {
  const double TOL = 1e-6;
  const double X = 0.5;

  EXPECT_NEAR(legendrePolynomialFunction(0, fx, X), 1.0, TOL);
  EXPECT_NEAR(legendrePolynomialFunction(1, fx, X), fx(X), TOL);
  EXPECT_NEAR(legendrePolynomialFunction(2, fx, X), p20(X), TOL);
  EXPECT_NEAR(legendrePolynomialFunction(3, fx, X), p30(X), TOL);
  EXPECT_NEAR(legendrePolynomialFunction(4, fx, X), p40(X), TOL);
  EXPECT_NEAR(legendrePolynomialFunction(5, fx, X), p50(X), TOL);
}

/**
 * @test NormalizedAssociatedLegendreFunction_mZero (EGM/IERS)
 * @brief For m = 0, verify the fully-normalized Legendre function equals
 *        sqrt(2n+1) multiplied by the unnormalized Legendre polynomial P_n(x).
 */
TEST(MathLegendrePolynomials, NormalizedAssociatedLegendreFunction_mZero) {
  const double TOL = 1e-6;
  const double X = 0.3; // any x in [-1,1]

  for (int n = 0; n <= 5; ++n) {
    double expected = std::sqrt(2.0 * n + 1.0) * legendrePolynomialFunction(n, fx, X);
    EXPECT_NEAR(normalizedAssociatedLegendreFunction(n, 0, fx, X), expected, TOL) << "n=" << n;
  }
}

/**
 * @test NormalizedAssociatedLegendreFunction_n1m1 (EGM/IERS)
 * @brief For n=1, m=1: factor = sqrt( (2n+1)*2 * (n-m)!/(n+m)! ) = sqrt(3).
 */
TEST(MathLegendrePolynomials, NormalizedAssociatedLegendreFunction_n1m1) {
  const double TOL = 1e-6;
  const double THETA = M_PI / 8.0;
  double expected = p11(THETA) * std::sqrt(3.0);
  EXPECT_NEAR(normalizedAssociatedLegendreFunction(1, 1, t, THETA), expected, TOL);
}

/**
 * @test NormalizedAssociatedLegendreFunction_n2m1m2 (EGM/IERS)
 * @brief For n=2: m=1 -> sqrt(10/6)=sqrt(5/3), m=2 -> sqrt(10/24)=sqrt(5/12).
 */
TEST(MathLegendrePolynomials, NormalizedAssociatedLegendreFunction_n2m1m2) {
  const double TOL = 1e-6;
  const double THETA = M_PI / 8.0;

  double expected21 = p21(THETA) * std::sqrt(5.0 / 3.0);
  EXPECT_NEAR(normalizedAssociatedLegendreFunction(2, 1, t, THETA), expected21, TOL);

  double expected22 = p22(THETA) * std::sqrt(5.0 / 12.0);
  EXPECT_NEAR(normalizedAssociatedLegendreFunction(2, 2, t, THETA), expected22, TOL);
}

/**
 * @test NormalizedAssociatedLegendreFunction_Endpoints (EGM/IERS)
 * @brief At x=+/-1: m=0 -> sqrt(2n+1) * P_n(+/-1) = sqrt(2n+1) * (+/-1)^n; m>0 -> 0.
 */
TEST(MathLegendrePolynomials, NormalizedAssociatedLegendreFunction_Endpoints) {
  const double TOL = 1e-12;

  // m = 0: depends on parity of n
  for (int n = 0; n <= 6; ++n) {
    double base = std::sqrt(2.0 * n + 1.0);
    double expectedPos = base * 1.0;                // P_n(+1) = +1
    double expectedNeg = base * ((n % 2 == 0) ? 1.0 // P_n(-1) = (-1)^n
                                              : -1.0);

    EXPECT_NEAR(normalizedAssociatedLegendreFunction(n, 0, fx, +1.0), expectedPos, TOL)
        << "n=" << n << " at +1";
    EXPECT_NEAR(normalizedAssociatedLegendreFunction(n, 0, fx, -1.0), expectedNeg, TOL)
        << "n=" << n << " at -1";
  }

  // m > 0: should be zero at x = +/-1
  for (int m = 1; m <= 3; ++m) {
    EXPECT_NEAR(normalizedAssociatedLegendreFunction(m, m, fx, +1.0), 0.0, TOL);
    EXPECT_NEAR(normalizedAssociatedLegendreFunction(m, m, fx, -1.0), 0.0, TOL);
  }
}

/**
 * @brief Crude trapezoidal integrator over [-1,1].
 */
static double integrateTrap(std::function<double(double)> f, double a, double b, int steps = 2000) {
  double h = (b - a) / steps;
  double sum = 0.5 * (f(a) + f(b));
  for (int i = 1; i < steps; ++i) {
    sum += f(a + i * h);
  }
  return sum * h;
}

/**
 * @test Orthogonality_Pn
 * @brief Sanity-check integral_{-1}^1 P_n(x) P_m(x) dx approx 0 for n != m (unnormalized P_n).
 */
TEST(MathLegendrePolynomials, Orthogonality_Pn) {
  const double TOL = 1e-3;
  for (int n = 0; n <= 4; ++n) {
    for (int m = 0; m <= 4; ++m) {
      if (n == m)
        continue;
      auto f = [&](double x) {
        return legendrePolynomialFunction(n, fx, x) * legendrePolynomialFunction(m, fx, x);
      };
      double integral = integrateTrap(f, -1.0, +1.0);
      EXPECT_NEAR(integral, 0.0, TOL) << "integral P" << n << "*P" << m << " dx = " << integral;
    }
  }
}

/**
 * @test RoundTrip_Normalization (EGM/IERS)
 * @brief For m > 0, verify normalized == unnormalized * sqrt( (2n+1)*2 * (n-m)!/(n+m)! ).
 */
TEST(MathLegendrePolynomials, RoundTrip_Normalization) {
  const double TOL = 1e-6;
  std::vector<double> xs = {-0.5, 0.0, 0.3, 1.0};

  for (int n = 0; n <= 4; ++n) {
    for (int m = 1; m <= n; ++m) {
      for (double x : xs) {
        double raw = associatedLegendreFunction(n, m, fx, x);
        double norm = normalizedAssociatedLegendreFunction(n, m, fx, x);

        auto tbl = precomputedFactorials(n + m);
        double ratio = tbl[n - m] / tbl[n + m];
        double factor = std::sqrt((2.0 * n + 1.0) * 2.0 * ratio);

        EXPECT_NEAR(raw * factor, norm, TOL) << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
}
