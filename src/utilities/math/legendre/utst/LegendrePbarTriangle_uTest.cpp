/**
 * @file LegendrePbarTriangle_uTest.cpp
 * @brief Unit tests for fully normalized Legendre triangle computation (CPU).
 *
 * Notes:
 *  - Tests verify triangle sizes, consistency with scalar functions, and boundary conditions.
 *  - Tests are platform-agnostic: assert invariants, not exact implementation details.
 */

#include "src/utilities/math/legendre/inc/AssociatedLegendre.hpp"
#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using apex::math::legendre::computeNormalizedPbarTriangle;
using apex::math::legendre::computeNormalizedPbarTriangleVector;
using apex::math::legendre::legendrePolynomialFunction;
using apex::math::legendre::normalizedAssociatedLegendreFunction;
using apex::math::legendre::pbarTriangleIndex;
using apex::math::legendre::pbarTriangleSize;

/* ----------------------------- File Helpers ----------------------------- */

// transforms and reference implementations
static inline double fx(double x) noexcept { return x; }

/* ----------------------------- API Tests ----------------------------- */

/**
 * @test PbarTriangle_Size
 * @brief Confirms pbarTriangleSize(N) == (N+1)(N+2)/2 for a few N.
 */
TEST(MathLegendrePolynomials, PbarTriangle_Size) {
  for (int n = 0; n <= 10; ++n) {
    const std::size_t EXPECT = static_cast<std::size_t>((n + 1) * (n + 2) / 2);
    EXPECT_EQ(pbarTriangleSize(n), EXPECT) << "n=" << n;
  }
}

/**
 * @test PbarTriangle_ConsistencyWithScalar
 * @brief Triangle values match normalizedAssociatedLegendreFunction for n<=N, m<=n.
 */
TEST(MathLegendrePolynomials, PbarTriangle_ConsistencyWithScalar) {
  const int N_MAX = 12; // modest degree to keep test quick and stable
  const double X = 0.3; // any x in [-1,1]
  const double TOL = 1e-10;

  // Compute triangle into vector
  auto tri = computeNormalizedPbarTriangleVector(N_MAX, X);
  ASSERT_EQ(tri.size(), pbarTriangleSize(N_MAX));

  for (int n = 0; n <= N_MAX; ++n) {
    for (int m = 0; m <= n; ++m) {
      const double TRI_VAL = tri[pbarTriangleIndex(n, m)];
      const double REF_VAL = normalizedAssociatedLegendreFunction(n, m, fx, X);
      EXPECT_NEAR(TRI_VAL, REF_VAL, TOL) << "n=" << n << " m=" << m;
    }
  }
}

/**
 * @test PbarTriangle_Endpoints
 * @brief At x = +/-1: m>0 -> 0; m=0 -> sqrt(2n+1) * P_n(+/-1) with parity on -1.
 */
TEST(MathLegendrePolynomials, PbarTriangle_Endpoints) {
  const int N_MAX = 10;
  const double TOL = 1e-12;

  // +1
  {
    std::vector<double> tri = computeNormalizedPbarTriangleVector(N_MAX, +1.0);
    for (int n = 0; n <= N_MAX; ++n) {
      // m = 0
      const double EXPECT0 =
          std::sqrt(2.0 * n + 1.0) * legendrePolynomialFunction(n, fx, +1.0); // = sqrt(2n+1)
      EXPECT_NEAR(tri[pbarTriangleIndex(n, 0)], EXPECT0, TOL) << "x=+1 n=" << n << " m=0";
      // m > 0
      for (int m = 1; m <= n; ++m) {
        EXPECT_NEAR(tri[pbarTriangleIndex(n, m)], 0.0, TOL) << "x=+1 n=" << n << " m=" << m;
      }
    }
  }

  // -1
  {
    std::vector<double> tri = computeNormalizedPbarTriangleVector(N_MAX, -1.0);
    for (int n = 0; n <= N_MAX; ++n) {
      // m = 0 -> parity
      const double PN_MINUS1 = legendrePolynomialFunction(n, fx, -1.0); // = (-1)^n
      const double EXPECT0 = std::sqrt(2.0 * n + 1.0) * PN_MINUS1;
      EXPECT_NEAR(tri[pbarTriangleIndex(n, 0)], EXPECT0, TOL) << "x=-1 n=" << n << " m=0";
      // m > 0
      for (int m = 1; m <= n; ++m) {
        EXPECT_NEAR(tri[pbarTriangleIndex(n, m)], 0.0, TOL) << "x=-1 n=" << n << " m=" << m;
      }
    }
  }
}

/**
 * @test PbarTriangle_RawBufferAPI
 * @brief Raw buffer version matches vector wrapper and enforces buffer size.
 */
TEST(MathLegendrePolynomials, PbarTriangle_RawBufferAPI) {
  const int N_MAX = 8;
  const double X = 0.123;
  const auto NEED = pbarTriangleSize(N_MAX);

  // 1) Good buffer
  std::vector<double> buf(NEED, std::numeric_limits<double>::quiet_NaN());
  ASSERT_TRUE(computeNormalizedPbarTriangle(N_MAX, X, buf.data(), buf.size()));

  const auto VIA_VEC = computeNormalizedPbarTriangleVector(N_MAX, X);
  ASSERT_EQ(VIA_VEC.size(), NEED);
  const double TOL = 1e-12;
  for (std::size_t i = 0; i < NEED; ++i) {
    EXPECT_NEAR(buf[i], VIA_VEC[i], TOL) << "i=" << i;
  }

  // 2) Too-small buffer
  if (NEED > 0) {
    std::vector<double> small(NEED - 1, 0.0);
    EXPECT_FALSE(computeNormalizedPbarTriangle(N_MAX, X, small.data(), small.size()));
  }
}

/**
 * @test PbarTriangle_BadInputs
 * @brief Negative degree should fail; N==0 should succeed with a single value.
 */
TEST(MathLegendrePolynomials, PbarTriangle_BadInputs) {
  // N < 0
  std::array<double, 1> dummy{};
  EXPECT_FALSE(computeNormalizedPbarTriangle(-1, 0.0, dummy.data(), dummy.size()));

  // N == 0 -> one value: sqrt(1) * P0(x) = 1
  std::array<double, 1> out0{0.0};
  ASSERT_TRUE(computeNormalizedPbarTriangle(0, 0.42, out0.data(), out0.size()));
  EXPECT_DOUBLE_EQ(out0[0], 1.0);
}
