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
#include "src/utilities/math/legendre/utst/PbarReference.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
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
using apex::math::legendre::makeScalarTables;
using apex::math::legendre::normalizedAssociatedLegendre;
using apex::math::legendre::normalizedAssociatedLegendreFunction;
using apex::math::legendre::pbarTriangleIndex;
using apex::math::legendre::pbarTriangleSize;
using apex::math::legendre::SCALAR_POW_SEED_MIN_ORDER;
using apex::math::legendre::ScalarTables;

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

/// Degrees from 1 to 2190, the EGM2008 degree, dense at the low end.
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
 *        to 1e-10 relative across the grid.
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

/* ----------------------------- Cached API ----------------------------- */

namespace {

/// Tables for the whole grid, built once for every cached test.
const ScalarTables& gridTables() {
  static const ScalarTables TABLES = makeScalarTables(GRID_MAX_N);
  return TABLES;
}

} // namespace

/**
 * @test CachedHighDegree_MatchesStdSphLegendre
 * @brief The cached walk meets the same 1e-10 bound against rescaled
 *        std::sph_legendre across the grid as the stateless walk.
 */
TEST(MathLegendrePolynomials, CachedHighDegree_MatchesStdSphLegendre) {
#if defined(__cpp_lib_math_special_functions)
  const ScalarTables& TABLES = gridTables();
  for (double x : gridArguments()) {
    const double THETA = std::acos(x);
    for (int n : gridDegrees()) {
      for (int m : gridOrders(n)) {
        const double SIGN = (m % 2 == 0) ? 1.0 : -1.0;
        const double SCALE = std::sqrt(4.0 * M_PI * ((m == 0) ? 1.0 : 2.0));
        const double REF =
            SIGN * SCALE *
            std::sph_legendre(static_cast<unsigned>(n), static_cast<unsigned>(m), THETA);
        EXPECT_TRUE(nearRel(normalizedAssociatedLegendre(n, m, x, TABLES), REF, REL_TOL, ABS_TOL))
            << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
#else
  GTEST_SKIP() << "C++17 special math functions unavailable in this standard library";
#endif
}

/**
 * @test CachedHighDegree_MatchesStateless
 * @brief Cached and stateless walks agree across the grid; they differ only in
 *        how the coefficients are rounded.
 */
TEST(MathLegendrePolynomials, CachedHighDegree_MatchesStateless) {
  const ScalarTables& TABLES = gridTables();
  for (double x : gridArguments()) {
    for (int n : gridDegrees()) {
      for (int m : gridOrders(n)) {
        EXPECT_TRUE(nearRel(normalizedAssociatedLegendre(n, m, x, TABLES),
                            normalizedAssociatedLegendre(n, m, x), REL_TOL, ABS_TOL))
            << "n=" << n << " m=" << m << " x=" << x;
      }
    }
  }
}

/**
 * @test CachedDiagonalSeed_BothBranches
 * @brief Below SCALAR_POW_SEED_MIN_ORDER the diagonal is the stateless product
 *        exactly (same table product, same multiply loop); at and above it the
 *        pow seed agrees with the loop to rounding.
 */
TEST(MathLegendrePolynomials, CachedDiagonalSeed_BothBranches) {
  const ScalarTables& TABLES = gridTables();
  for (double x : gridArguments()) {
    for (int m : {0, 1, 2, SCALAR_POW_SEED_MIN_ORDER - 1}) {
      EXPECT_EQ(normalizedAssociatedLegendre(m, m, x, TABLES),
                normalizedAssociatedLegendre(m, m, x))
          << "m=" << m << " x=" << x;
    }
    for (int m :
         {SCALAR_POW_SEED_MIN_ORDER, SCALAR_POW_SEED_MIN_ORDER + 1, 100, 1000, GRID_MAX_N}) {
      EXPECT_TRUE(nearRel(normalizedAssociatedLegendre(m, m, x, TABLES),
                          normalizedAssociatedLegendre(m, m, x), 1e-11, ABS_TOL))
          << "m=" << m << " x=" << x;
    }
  }
}

/**
 * @test CachedTables_LayoutAndRange
 * @brief Table sizes follow the documented layout, every (n, m) of a small
 *        table matches the stateless walk, and degrees beyond the tables,
 *        invalid orders and empty tables return NaN.
 */
TEST(MathLegendrePolynomials, CachedTables_LayoutAndRange) {
  static_assert(
      noexcept(normalizedAssociatedLegendre(1, 1, 0.0, std::declval<const ScalarTables&>())));

  const ScalarTables T10 = makeScalarTables(10);
  EXPECT_EQ(T10.maxDegree, 10);
  EXPECT_EQ(T10.diag.size(), 11U);
  EXPECT_EQ(T10.off.size(), 11U);
  EXPECT_EQ(T10.ab.size(), 2U * 45U); // (N-1)N/2 pairs
  for (int n = 0; n <= 10; ++n) {
    for (int m = 0; m <= n; ++m) {
      EXPECT_TRUE(nearRel(normalizedAssociatedLegendre(n, m, -0.4, T10),
                          normalizedAssociatedLegendre(n, m, -0.4), 1e-14, 1e-15))
          << "n=" << n << " m=" << m;
    }
  }
  EXPECT_TRUE(std::isnan(normalizedAssociatedLegendre(11, 0, 0.3, T10)));
  EXPECT_TRUE(std::isnan(normalizedAssociatedLegendre(3, 4, 0.3, T10)));
  EXPECT_TRUE(std::isnan(normalizedAssociatedLegendre(-1, 0, 0.3, T10)));
  EXPECT_TRUE(std::isnan(
      normalizedAssociatedLegendre(3, 1, std::numeric_limits<double>::quiet_NaN(), T10)));

  // Degrees 0 and 1 hold no recurrence pairs.
  const ScalarTables T0 = makeScalarTables(0);
  EXPECT_TRUE(T0.ab.empty());
  EXPECT_EQ(normalizedAssociatedLegendre(0, 0, 0.3, T0), 1.0);
  const ScalarTables T1 = makeScalarTables(1);
  EXPECT_TRUE(T1.ab.empty());
  EXPECT_EQ(normalizedAssociatedLegendre(1, 0, 0.3, T1), normalizedAssociatedLegendre(1, 0, 0.3));
  EXPECT_EQ(normalizedAssociatedLegendre(1, 1, 0.3, T1), normalizedAssociatedLegendre(1, 1, 0.3));

  const ScalarTables EMPTY = makeScalarTables(-1);
  EXPECT_EQ(EMPTY.maxDegree, -1);
  EXPECT_TRUE(EMPTY.diag.empty() && EMPTY.off.empty() && EMPTY.ab.empty());
  EXPECT_TRUE(std::isnan(normalizedAssociatedLegendre(0, 0, 0.3, EMPTY)));
  EXPECT_TRUE(std::isnan(normalizedAssociatedLegendre(0, 0, 0.3, ScalarTables{})));
}

/**
 * @test NormalizedNearPole_KeepsPrecision
 * @brief Within about 2.6 degrees of either pole, the stateless and cached
 *        scalar walks agree with an extended-precision reference to 4e-13 of
 *        each column's scale.
 */
TEST(MathLegendrePolynomials, NormalizedNearPole_KeepsPrecision) {
  constexpr int N = 160;
  const ScalarTables TABLES = makeScalarTables(N);
  for (double x : legendre_test::NEAR_POLE_ARGS) {
    const auto STATELESS = legendre_test::columnScaledError(
        N, x, [&](int n, int m) { return normalizedAssociatedLegendre(n, m, x); });
    const auto CACHED = legendre_test::columnScaledError(
        N, x, [&](int n, int m) { return normalizedAssociatedLegendre(n, m, x, TABLES); });
    EXPECT_GE(STATELESS.columns, 50) << "x=" << x;
    EXPECT_EQ(STATELESS.nonfinite, 0) << "x=" << x;
    EXPECT_EQ(CACHED.nonfinite, 0) << "x=" << x;
    EXPECT_LE(STATELESS.worst, 4e-13) << "stateless x=" << x << " m=" << STATELESS.worstM;
    EXPECT_LE(CACHED.worst, 4e-13) << "cached x=" << x << " m=" << CACHED.worstM;
  }
}

/* ----------------------------- Unnormalized Range ----------------------------- */

namespace {

/// Accuracy scale for P_n^m at one argument: the largest reference magnitude
/// among degrees n-1, n, n+1 in the same column. The zeros of consecutive
/// degrees interleave, so this is the local amplitude even where P_n^m itself
/// passes through zero, where a relative bound would measure the zero.
long double unnormalizedLocalScale(int n, int m, double x) {
  long double scale = 0.0L;
  for (int k = std::max(m, n - 1); k <= n + 1; ++k) {
    scale = std::fmax(scale, std::fabs(legendre_test::unnormalizedReference(k, m, x)));
  }
  return scale;
}

} // namespace

/**
 * @test UnnormalizedRange_IntermediatesStayRepresentable
 * @brief Values whose intermediates overflow double but whose results do not
 *        are finite and within 1e-12 of the local amplitude of the long double
 *        recurrence; three of them also match independently computed values to
 *        1e-12 relative (the fourth sits at a root of its two-term closed form,
 *        where the value is sixteen digits below its terms). An odd-parity value
 *        at x = 0 is exactly zero, and a true overflow is a signed infinity.
 */
TEST(MathLegendrePolynomials, UnnormalizedRange_IntermediatesStayRepresentable) {
  struct Case {
    int n, m;
    double x;
    double independent; // two-term Rodrigues evaluation, rounded to double
    bool relative;      // independent value comparable at 1e-12 relative
  };
  const Case FINITE[] = {
      {152, 150, 0.05, -1.1353410821681464e308, true},
      {152, 150, 0.05744849896214261, 1.6444996327746805e292, false},
      {180, 140, 0.3, -9.783177957359671e306, true},
      {200, 100, 0.3, -8.359055842138339e226, true},
  };
  for (const Case& C : FINITE) {
    const double GOT = associatedLegendre(C.n, C.m, C.x);
    ASSERT_TRUE(std::isfinite(GOT)) << "n=" << C.n << " m=" << C.m << " x=" << C.x;
    const long double REF = legendre_test::unnormalizedReference(C.n, C.m, C.x);
    EXPECT_LE(std::fabs(static_cast<long double>(GOT) - REF),
              1e-12L * unnormalizedLocalScale(C.n, C.m, C.x))
        << "n=" << C.n << " m=" << C.m << " x=" << C.x << " got " << GOT;
    if (C.relative) {
      EXPECT_TRUE(nearRel(GOT, C.independent, 1e-12, 0.0)) << "n=" << C.n << " m=" << C.m;
    }
  }
  EXPECT_EQ(associatedLegendre(152, 151, 0.0), 0.0); // odd n - m at x = 0
  EXPECT_EQ(associatedLegendre(2190, 0, 1.0), 1.0);
  EXPECT_EQ(associatedLegendre(2190, 1, 1.0), 0.0);
  const double OVER = associatedLegendre(153, 151, 0.0); // true value below -DBL_MAX
  EXPECT_TRUE(std::isinf(OVER) && OVER < 0.0) << OVER;
  // True overflows reached in the walk rather than the seed: the renormalisation of the
  // recurrence itself is what keeps the sign instead of an inf - inf NaN.
  {
    const double V = associatedLegendre(900, 210, 0.0);
    const long double REF = legendre_test::unnormalizedReference(900, 210, 0.0);
    EXPECT_TRUE(std::isinf(V) && ((V > 0.0) == (REF > 0.0L))) << "n=900 m=210: " << V;
  }
  {
    const double V = associatedLegendre(900, 320, 0.0);
    const long double REF = legendre_test::unnormalizedReference(900, 320, 0.0);
    EXPECT_TRUE(std::isinf(V) && ((V > 0.0) == (REF > 0.0L))) << "n=900 m=320: " << V;
  }
  // A representable result whose walk climbs past 2^512 from a scaled seed.
  {
    const double V = associatedLegendre(1200, 100, 0.9);
    const long double REF = legendre_test::unnormalizedReference(1200, 100, 0.9);
    ASSERT_TRUE(std::isfinite(V)) << V;
    EXPECT_LE(std::fabs(static_cast<long double>(V) - REF),
              1e-10L * unnormalizedLocalScale(1200, 100, 0.9));
  }
}

/**
 * @test UnnormalizedTinyArgument_EvenValuesOrdinaryOddValuesTiny
 * @brief A tiny nonzero argument makes every odd-offset value proportional to x
 *        while the even-offset values stay ordinary; the pair must be carried
 *        through the recurrence together. Degrees 6 and 8 at the smallest
 *        subnormal are -0.3125 and 0.2734375, the same as at zero; every
 *        (n, m) with n <= 60 at six tiny arguments of both signs is finite,
 *        matches the long double recurrence to 1e-12 where that is a normal
 *        double, and is no larger than DBL_MIN where the reference is below it.
 *        At a subnormal argument the odd-offset values begin as subnormals and
 *        keep only that precision (about 34 bits here), so they are held to
 *        1e-9. The polynomial and identity-transform wrappers agree with the
 *        direct call.
 */
TEST(MathLegendrePolynomials, UnnormalizedTinyArgument_EvenValuesOrdinaryOddValuesTiny) {
  constexpr double TRUE_MIN = std::numeric_limits<double>::denorm_min();
  constexpr double DMIN = std::numeric_limits<double>::min();
  EXPECT_DOUBLE_EQ(associatedLegendre(6, 0, TRUE_MIN), -0.3125);
  EXPECT_DOUBLE_EQ(associatedLegendre(8, 0, TRUE_MIN), 0.2734375);
  EXPECT_DOUBLE_EQ(associatedLegendre(6, 0, -TRUE_MIN), -0.3125);
  EXPECT_DOUBLE_EQ(legendrePolynomial(8, TRUE_MIN), 0.2734375);
  EXPECT_DOUBLE_EQ(legendrePolynomialFunction(8, fx, TRUE_MIN), 0.2734375);
  EXPECT_DOUBLE_EQ(associatedLegendreFunction(6, 0, fx, -TRUE_MIN), -0.3125);

  int compared = 0, tiny = 0;
  for (double x : {TRUE_MIN, -TRUE_MIN, DMIN, -1e-300, 1e-200, 0x1p-600}) {
    for (int n = 0; n <= 60; ++n) {
      for (int m = 0; m <= n; ++m) {
        const double GOT = associatedLegendre(n, m, x);
        ASSERT_TRUE(std::isfinite(GOT)) << "n=" << n << " m=" << m << " x=" << x << " got " << GOT;
        const long double REF = legendre_test::unnormalizedReference(n, m, x);
        if (std::fabs(REF) >= static_cast<long double>(DMIN)) {
          const bool ODD_OF_SUBNORMAL = ((n - m) % 2 != 0) && std::fabs(x) < DMIN;
          const long double TOL = ODD_OF_SUBNORMAL ? 1e-9L : 1e-12L;
          EXPECT_LE(std::fabs(static_cast<long double>(GOT) - REF), TOL * std::fabs(REF))
              << "n=" << n << " m=" << m << " x=" << x << " got " << GOT;
          ++compared;
        } else {
          EXPECT_LE(std::fabs(GOT), DMIN) << "n=" << n << " m=" << m << " x=" << x;
          ++tiny;
        }
      }
    }
  }
  EXPECT_GT(compared, 8000) << "tiny=" << tiny;
  EXPECT_GT(tiny, 100) << "compared=" << compared; // only the subnormal arguments reach this branch
}

/**
 * @test UnnormalizedSweep_FiniteExactlyWhenRepresentable
 * @brief Over every (n, m) with n <= 300 and seven arguments, against the long
 *        double recurrence: a zero reference gives exactly zero; a reference
 *        above DBL_MAX gives an infinity of the same sign; a reference below
 *        DBL_MIN gives no larger magnitude and no NaN; everything else is within
 *        1e-10 of the column's local amplitude, the documented accuracy class:
 *        the forward recurrence's double rounding reaches about 4e-11 relative
 *        on small-magnitude entries at x = 0.999 (both normalized and
 *        unnormalized walks, against an exact rational evaluation); the long
 *        double reference agrees with that evaluation to about 1e-14 relative
 *        at the sampled points, enough for this tolerance.
 */
TEST(MathLegendrePolynomials, UnnormalizedSweep_FiniteExactlyWhenRepresentable) {
  constexpr int N = 300;
  constexpr double DMAX = std::numeric_limits<double>::max();
  constexpr double DMIN = std::numeric_limits<double>::min();
  int overflowed = 0, zeros = 0, compared = 0;
  for (double x : {-0.9, -0.3, 0.0, 0.3, 0.7, 0.999, 1.0}) {
    for (int m = 0; m <= N; ++m) {
      // One pass per column keeps the local-scale neighbours at hand.
      long double refPrev = 0.0L;                                      // n - 1
      long double ref = legendre_test::unnormalizedReference(m, m, x); // n
      for (int n = m; n <= N; ++n) {
        const long double REF_NEXT =
            (n + 1 <= N) ? legendre_test::unnormalizedReference(n + 1, m, x) : 0.0L;
        const double GOT = associatedLegendre(n, m, x);
        ASSERT_FALSE(std::isnan(GOT)) << "n=" << n << " m=" << m << " x=" << x;
        if (ref == 0.0L) {
          EXPECT_EQ(GOT, 0.0) << "n=" << n << " m=" << m << " x=" << x;
          ++zeros;
        } else if (std::fabs(ref) > static_cast<long double>(DMAX)) {
          EXPECT_TRUE(std::isinf(GOT) && ((GOT > 0.0) == (ref > 0.0L)))
              << "n=" << n << " m=" << m << " x=" << x << " got " << GOT;
          ++overflowed;
        } else if (std::fabs(ref) < static_cast<long double>(DMIN)) {
          EXPECT_LE(std::fabs(GOT), DMIN) << "n=" << n << " m=" << m << " x=" << x;
        } else {
          const long double SCALE =
              std::fmax(std::fabs(ref), std::fmax(std::fabs(refPrev), std::fabs(REF_NEXT)));
          EXPECT_LE(std::fabs(static_cast<long double>(GOT) - ref), 1e-10L * SCALE)
              << "n=" << n << " m=" << m << " x=" << x << " got " << GOT << " ref "
              << static_cast<double>(ref);
          ++compared;
        }
        refPrev = ref;
        ref = REF_NEXT;
      }
    }
  }
  EXPECT_GT(compared, 100000);
  EXPECT_GT(overflowed, 1000);
  EXPECT_GT(zeros, 1000);
}

/* ----------------------------- Transform API ----------------------------- */

/**
 * @test TransformClampsTheTransformedValue
 * @brief With fX = cos and colatitude input, every theta in [0, pi] evaluates
 *        at cos(theta): the transform is applied first and its result is
 *        clamped to [-1, 1].
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
