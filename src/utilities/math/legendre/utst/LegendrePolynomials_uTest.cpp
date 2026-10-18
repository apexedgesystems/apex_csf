/**
 * @file LegendrePolynomials_uTest.cpp
 * @brief Unit tests for associated Legendre polynomials and normalized variants.
 *
 * Notes:
 *  - Tests verify mathematical properties and known values.
 *  - Tests are platform-agnostic: assert invariants, not exact implementation details.
 *  - High-degree accuracy is checked against the C++17 special math functions
 *    (std::sph_legendre, std::assoc_legendre), an implementation independent of
 *    this library, and against the triangle API.
 */

#include "src/utilities/math/legendre/inc/AssociatedLegendre.hpp"
#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using apex::math::legendre::associatedLegendre;
using apex::math::legendre::associatedLegendreFunction;
using apex::math::legendre::computeNormalizedPbarTriangle;
using apex::math::legendre::legendrePolynomial;
using apex::math::legendre::legendrePolynomialFunction;
using apex::math::legendre::normalizedAssociatedLegendre;
using apex::math::legendre::normalizedAssociatedLegendreFunction;
using apex::math::legendre::pbarTriangleIndex;
using apex::math::legendre::pbarTriangleSize;

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

        const double ratio = std::tgamma(n - m + 1.0) / std::tgamma(n + m + 1.0);
        double factor = std::sqrt((2.0 * n + 1.0) * 2.0 * ratio);

        EXPECT_NEAR(raw * factor, norm, TOL) << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
}

/* ----------------------------- High Degree ----------------------------- */

namespace {

/// |got - ref| <= rel |ref| + abs. The absolute floor is in normalized units,
/// where values are O(1); it covers true zeros, which a reference evaluated
/// through acos(x) returns as about 1e-14.
::testing::AssertionResult nearRel(double got, double ref, double rel, double abs) {
  if (std::isfinite(got) && std::fabs(got - ref) <= rel * std::fabs(ref) + abs) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "got " << got << " ref " << ref << " diff " << std::fabs(got - ref);
}

/// The ticket's grid: degrees across both failure points of the old closed form
/// (digits lost from n = 22, inf from n = 86) up to the EGM2008 degree.
const std::vector<int>& gridDegrees() {
  static const std::vector<int> NS = {1, 5, 10, 50, 85, 86, 180, 360, 720, 2190};
  return NS;
}

const std::vector<double>& gridArguments() {
  static const std::vector<double> XS = {-0.9, -0.3, 0.0, 0.3, 0.7, 0.999};
  return XS;
}

/// Orders {0, 1, n/3, n/2, n-1, n}, deduplicated and in range.
std::set<int> gridOrders(int n) {
  std::set<int> ms;
  for (int m : {0, 1, n / 3, n / 2, n - 1, n}) {
    if (m >= 0 && m <= n) {
      ms.insert(m);
    }
  }
  return ms;
}

constexpr double REL_TOL = 1e-10;
constexpr double ABS_TOL = 1e-12;
constexpr int GRID_MAX_N = 2190;

} // namespace

/**
 * @test NormalizedHighDegree_MatchesTriangle
 * @brief The scalar walk agrees with the triangle entry at idx(n, m) across the
 *        whole grid, n up to 2190.
 */
TEST(MathLegendrePolynomials, NormalizedHighDegree_MatchesTriangle) {
  std::vector<double> tri(pbarTriangleSize(GRID_MAX_N));
  for (double x : gridArguments()) {
    ASSERT_TRUE(computeNormalizedPbarTriangle(GRID_MAX_N, x, tri.data(), tri.size()));
    for (int n : gridDegrees()) {
      for (int m : gridOrders(n)) {
        EXPECT_TRUE(nearRel(normalizedAssociatedLegendre(n, m, x), tri[pbarTriangleIndex(n, m)],
                            REL_TOL, ABS_TOL))
            << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
}

/**
 * @test NormalizedHighDegree_BoundedBySqrt2nPlus1
 * @brief Every grid value is finite and within |Pbar_n^m| <= sqrt(2n+1), the
 *        bound the fully normalized functions satisfy (sqrt(2n+1) times the
 *        Schmidt semi-normalized bound of 1).
 */
TEST(MathLegendrePolynomials, NormalizedHighDegree_BoundedBySqrt2nPlus1) {
  for (double x : gridArguments()) {
    for (int n : gridDegrees()) {
      for (int m : gridOrders(n)) {
        const double V = normalizedAssociatedLegendre(n, m, x);
        ASSERT_TRUE(std::isfinite(V)) << "n=" << n << " m=" << m << " x=" << x;
        EXPECT_LE(std::fabs(V), std::sqrt(2.0 * n + 1.0) * (1.0 + 1e-12))
            << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
}

/**
 * @test NormalizedHighDegree_MatchesStdSphLegendre
 * @brief Against std::sph_legendre rescaled to the geodesy convention,
 *        Pbar_n^m(x) = (-1)^m sqrt(4 pi (2 - delta_0m)) sph_legendre(n, m, acos x),
 *        to 1e-10 relative across the grid. The closed form this replaced was
 *        off by more than 1e-9 from n = 22 and returned inf from n = 86.
 */
TEST(MathLegendrePolynomials, NormalizedHighDegree_MatchesStdSphLegendre) {
#if defined(__cpp_lib_math_special_functions)
  for (double x : gridArguments()) {
    const double THETA = std::acos(x);
    for (int n : gridDegrees()) {
      for (int m : gridOrders(n)) {
        const double SIGN = (m % 2 == 0) ? 1.0 : -1.0;
        const double SCALE = std::sqrt(4.0 * M_PI * ((m == 0) ? 1.0 : 2.0));
        const double REF =
            SIGN * SCALE *
            std::sph_legendre(static_cast<unsigned>(n), static_cast<unsigned>(m), THETA);
        EXPECT_TRUE(nearRel(normalizedAssociatedLegendre(n, m, x), REF, REL_TOL, ABS_TOL))
            << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
#else
  GTEST_SKIP() << "C++17 special math functions unavailable in this standard library";
#endif
}

/**
 * @test UnnormalizedHighDegree_MatchesStdAssocLegendre
 * @brief Against std::assoc_legendre (no Condon-Shortley phase, the same
 *        convention) for every grid degree below 128, where the standard
 *        defines it. Compared in normalized units, so the absolute floor means
 *        the same thing as in the normalized tests.
 */
TEST(MathLegendrePolynomials, UnnormalizedHighDegree_MatchesStdAssocLegendre) {
#if defined(__cpp_lib_math_special_functions)
  for (double x : gridArguments()) {
    for (int n : gridDegrees()) {
      if (n >= 128) {
        continue;
      }
      for (int m : gridOrders(n)) {
        const double LOG_N = 0.5 * (std::log((m == 0 ? 1.0 : 2.0) * (2.0 * n + 1.0)) +
                                    std::lgamma(n - m + 1.0) - std::lgamma(n + m + 1.0));
        const double NORM = std::exp(LOG_N);
        const double REF =
            std::assoc_legendre(static_cast<unsigned>(n), static_cast<unsigned>(m), x);
        EXPECT_TRUE(nearRel(NORM * associatedLegendre(n, m, x), NORM * REF, REL_TOL, ABS_TOL))
            << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
#else
  GTEST_SKIP() << "C++17 special math functions unavailable in this standard library";
#endif
}

/**
 * @test UnnormalizedRange_OverflowsOnlyWithTheValue
 * @brief P_n^n(0) = (2n-1)!!: finite and exact-to-rounding at n = 150, inf at
 *        n = 151 where (301)!! exceeds DBL_MAX. The normalized value at the same
 *        point stays finite.
 */
TEST(MathLegendrePolynomials, UnnormalizedRange_OverflowsOnlyWithTheValue) {
  double doubleFactorial = 1.0;
  for (int k = 1; k <= 150; ++k) {
    doubleFactorial *= 2.0 * k - 1.0;
  }
  ASSERT_TRUE(std::isfinite(doubleFactorial));
  EXPECT_TRUE(nearRel(associatedLegendre(150, 150, 0.0), doubleFactorial, 1e-13, 0.0));
  EXPECT_TRUE(std::isinf(associatedLegendre(151, 151, 0.0)));
  EXPECT_TRUE(std::isfinite(normalizedAssociatedLegendre(151, 151, 0.0)));
}

/* ----------------------------- Transform API ----------------------------- */

/**
 * @test TransformClampsTheTransformedValue
 * @brief With fX = cos and colatitude input, every theta in [0, pi] evaluates
 *        at cos(theta). The clamp applies to fX(x), not to x: clamping the
 *        input first evaluated every theta above 1 rad at cos(1).
 */
TEST(MathLegendrePolynomials, TransformClampsTheTransformedValue) {
  const auto COS = [](double theta) { return std::cos(theta); };
  for (double theta : {0.5, 1.0, 1.2, 2.0, 3.0}) {
    for (int n : {2, 7, 40}) {
      for (int m : {0, 1, n}) {
        EXPECT_EQ(normalizedAssociatedLegendreFunction(n, m, COS, theta),
                  normalizedAssociatedLegendre(n, m, std::cos(theta)))
            << "n=" << n << " m=" << m << " theta=" << theta;
        EXPECT_EQ(associatedLegendreFunction(n, m, COS, theta),
                  associatedLegendre(n, m, std::cos(theta)))
            << "n=" << n << " m=" << m << " theta=" << theta;
      }
    }
  }
  // A transform that leaves [-1, 1] is clamped after it is applied.
  const auto TWICE = [](double v) { return 2.0 * v; };
  EXPECT_EQ(normalizedAssociatedLegendreFunction(3, 1, TWICE, 0.8),
            normalizedAssociatedLegendre(3, 1, 1.0));
}

/* ----------------------------- Contract ----------------------------- */

/**
 * @test SimpleApiContract
 * @brief The simple API is noexcept; invalid orders and a NaN argument return NaN.
 */
TEST(MathLegendrePolynomials, SimpleApiContract) {
  static_assert(noexcept(normalizedAssociatedLegendre(1, 1, 0.0)));
  static_assert(noexcept(associatedLegendre(1, 1, 0.0)));
  static_assert(noexcept(legendrePolynomial(1, 0.0)));

  for (auto [n, m] : {std::pair{-1, 0}, std::pair{2, -1}, std::pair{2, 3}}) {
    EXPECT_TRUE(std::isnan(normalizedAssociatedLegendre(n, m, 0.3))) << n << "," << m;
    EXPECT_TRUE(std::isnan(associatedLegendre(n, m, 0.3))) << n << "," << m;
  }
  EXPECT_TRUE(std::isnan(legendrePolynomial(-1, 0.3)));

  const double NAN_X = std::numeric_limits<double>::quiet_NaN();
  for (int m : {0, 1, 3}) {
    EXPECT_TRUE(std::isnan(normalizedAssociatedLegendre(3, m, NAN_X))) << "m=" << m;
    EXPECT_TRUE(std::isnan(associatedLegendre(3, m, NAN_X))) << "m=" << m;
  }
}
