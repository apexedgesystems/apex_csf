/**
 * @file LegendrePbarDerivatives_uTest.cpp
 * @brief Unit tests for Legendre function derivative computation.
 *
 * Notes:
 *  - Tests verify mathematical properties and consistency.
 *  - Tests are platform-agnostic: assert invariants, not exact values.
 */

#include "src/utilities/math/legendre/inc/PbarDerivatives.hpp"
#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using apex::math::legendre::betaCoefficient;
using apex::math::legendre::computeBetaCoefficients;
using apex::math::legendre::computeBetaCoefficientsVector;
using apex::math::legendre::computeNormalizedPbarTriangle;
using apex::math::legendre::computeNormalizedPbarTriangleWithDerivatives;
using apex::math::legendre::computeNormalizedPbarTriangleWithDerivativesCached;
using apex::math::legendre::computeNormalizedPbarTriangleWithDerivativesVector;
using apex::math::legendre::pbarTriangleIndex;
using apex::math::legendre::pbarTriangleSize;

/* ----------------------------- Beta Coefficient Tests ----------------------------- */

/**
 * @test BetaCoefficient_Formula
 * @brief Verifies beta(n,m) = sqrt((n-m)(n+m+1)) for valid inputs.
 */
TEST(MathLegendrePbarDerivatives, BetaCoefficient_Formula) {
  const double TOL = 1e-12;

  // beta(2,0) = sqrt((2-0)(2+0+1)) = sqrt(6)
  EXPECT_NEAR(betaCoefficient(2, 0), std::sqrt(6.0), TOL);

  // beta(2,1) = sqrt((2-1)(2+1+1)) = sqrt(4) = 2
  EXPECT_NEAR(betaCoefficient(2, 1), 2.0, TOL);

  // beta(3,0) = sqrt((3-0)(3+0+1)) = sqrt(12) = 2*sqrt(3)
  EXPECT_NEAR(betaCoefficient(3, 0), std::sqrt(12.0), TOL);

  // beta(3,1) = sqrt((3-1)(3+1+1)) = sqrt(10)
  EXPECT_NEAR(betaCoefficient(3, 1), std::sqrt(10.0), TOL);

  // beta(3,2) = sqrt((3-2)(3+2+1)) = sqrt(6)
  EXPECT_NEAR(betaCoefficient(3, 2), std::sqrt(6.0), TOL);
}

/**
 * @test BetaCoefficient_Boundary
 * @brief Verifies beta(n,n) = 0 and invalid inputs return 0.
 */
TEST(MathLegendrePbarDerivatives, BetaCoefficient_Boundary) {
  // beta(n,n) = 0 (m >= n)
  for (int n = 0; n <= 5; ++n) {
    EXPECT_DOUBLE_EQ(betaCoefficient(n, n), 0.0) << "n=" << n;
  }

  // Invalid inputs return 0
  EXPECT_DOUBLE_EQ(betaCoefficient(-1, 0), 0.0);
  EXPECT_DOUBLE_EQ(betaCoefficient(2, -1), 0.0);
  EXPECT_DOUBLE_EQ(betaCoefficient(2, 3), 0.0); // m > n
}

/**
 * @test ComputeBetaCoefficients_MatchesInline
 * @brief Verifies precomputed beta table matches inline function.
 */
TEST(MathLegendrePbarDerivatives, ComputeBetaCoefficients_MatchesInline) {
  const int N_MAX = 10;
  const double TOL = 1e-12;

  auto beta = computeBetaCoefficientsVector(N_MAX);
  ASSERT_EQ(beta.size(), pbarTriangleSize(N_MAX));

  for (int n = 0; n <= N_MAX; ++n) {
    for (int m = 0; m <= n; ++m) {
      const double EXPECTED = betaCoefficient(n, m);
      EXPECT_NEAR(beta[pbarTriangleIndex(n, m)], EXPECTED, TOL) << "n=" << n << " m=" << m;
    }
  }
}

/**
 * @test ComputeBetaCoefficients_RawBufferAPI
 * @brief Verifies raw buffer API and buffer size checks.
 */
TEST(MathLegendrePbarDerivatives, ComputeBetaCoefficients_RawBufferAPI) {
  const int N_MAX = 6;
  const auto NEED = pbarTriangleSize(N_MAX);

  // Good buffer
  std::vector<double> buf(NEED, std::numeric_limits<double>::quiet_NaN());
  ASSERT_TRUE(computeBetaCoefficients(N_MAX, buf.data(), buf.size()));

  const auto VIA_VEC = computeBetaCoefficientsVector(N_MAX);
  const double TOL = 1e-12;
  for (std::size_t i = 0; i < NEED; ++i) {
    EXPECT_NEAR(buf[i], VIA_VEC[i], TOL) << "i=" << i;
  }

  // Too-small buffer
  if (NEED > 0) {
    std::vector<double> small(NEED - 1, 0.0);
    EXPECT_FALSE(computeBetaCoefficients(N_MAX, small.data(), small.size()));
  }
}

/* ----------------------------- Derivative Tests ----------------------------- */

/**
 * @test WithDerivatives_PMatchesPbarTriangle
 * @brief P output matches computeNormalizedPbarTriangle exactly.
 */
TEST(MathLegendrePbarDerivatives, WithDerivatives_PMatchesPbarTriangle) {
  const int N_MAX = 12;
  const double SIN_PHI = 0.3;
  const double COS_PHI = std::sqrt(1.0 - SIN_PHI * SIN_PHI);
  const double TOL = 1e-12;

  auto result = computeNormalizedPbarTriangleWithDerivativesVector(N_MAX, SIN_PHI, COS_PHI);
  ASSERT_EQ(result.P.size(), pbarTriangleSize(N_MAX));
  ASSERT_EQ(result.dP.size(), pbarTriangleSize(N_MAX));

  // Compare P with standalone Pbar triangle
  std::vector<double> pRef(pbarTriangleSize(N_MAX));
  ASSERT_TRUE(computeNormalizedPbarTriangle(N_MAX, SIN_PHI, pRef.data(), pRef.size()));

  for (std::size_t i = 0; i < result.P.size(); ++i) {
    EXPECT_NEAR(result.P[i], pRef[i], TOL) << "i=" << i;
  }
}

/**
 * @test WithDerivatives_CachedMatchesNonCached
 * @brief Cached version produces identical results to non-cached.
 */
TEST(MathLegendrePbarDerivatives, WithDerivatives_CachedMatchesNonCached) {
  const int N_MAX = 10;
  const double SIN_PHI = 0.5;
  const double COS_PHI = std::sqrt(1.0 - SIN_PHI * SIN_PHI);
  const double TOL = 1e-12;

  // Non-cached
  auto result = computeNormalizedPbarTriangleWithDerivativesVector(N_MAX, SIN_PHI, COS_PHI);

  // Cached
  auto beta = computeBetaCoefficientsVector(N_MAX);
  const auto NEED = pbarTriangleSize(N_MAX);
  std::vector<double> pCached(NEED), dpCached(NEED);
  ASSERT_TRUE(computeNormalizedPbarTriangleWithDerivativesCached(
      N_MAX, SIN_PHI, COS_PHI, beta.data(), pCached.data(), dpCached.data(), NEED));

  for (std::size_t i = 0; i < NEED; ++i) {
    EXPECT_NEAR(pCached[i], result.P[i], TOL) << "P[" << i << "]";
    EXPECT_NEAR(dpCached[i], result.dP[i], TOL) << "dP[" << i << "]";
  }
}

/**
 * @test WithDerivatives_m0_Derivative
 * @brief For m=0: dPbar_{n,0}/dphi = -beta(n,0) * Pbar_{n,1}.
 */
TEST(MathLegendrePbarDerivatives, WithDerivatives_m0_Derivative) {
  const int N_MAX = 8;
  const double PHI = M_PI / 6.0; // 30 degrees
  const double SIN_PHI = std::sin(PHI);
  const double COS_PHI = std::cos(PHI);
  const double TOL = 1e-10;

  auto result = computeNormalizedPbarTriangleWithDerivativesVector(N_MAX, SIN_PHI, COS_PHI);

  for (int n = 1; n <= N_MAX; ++n) {
    // dPbar_{n,0}/dphi = 0 * tan(phi) * Pbar_{n,0} - beta(n,0) * Pbar_{n,1}
    //                  = -beta(n,0) * Pbar_{n,1}
    const double BETA_N0 = betaCoefficient(n, 0);
    const double PBAR_N1 = result.P[pbarTriangleIndex(n, 1)];
    const double EXPECTED = -BETA_N0 * PBAR_N1;
    EXPECT_NEAR(result.dP[pbarTriangleIndex(n, 0)], EXPECTED, TOL) << "n=" << n;
  }
}

/**
 * @test WithDerivatives_Sectoral_Derivative
 * @brief For sectoral (m=n): dPbar_{n,n}/dphi = n * tan(phi) * Pbar_{n,n}.
 */
TEST(MathLegendrePbarDerivatives, WithDerivatives_Sectoral_Derivative) {
  const int N_MAX = 8;
  const double PHI = M_PI / 4.0; // 45 degrees
  const double SIN_PHI = std::sin(PHI);
  const double COS_PHI = std::cos(PHI);
  const double TAN_PHI = SIN_PHI / COS_PHI;
  const double TOL = 1e-10;

  auto result = computeNormalizedPbarTriangleWithDerivativesVector(N_MAX, SIN_PHI, COS_PHI);

  for (int n = 0; n <= N_MAX; ++n) {
    // dPbar_{n,n}/dphi = n * tan(phi) * Pbar_{n,n} - beta(n,n) * Pbar_{n,n+1}
    // Since beta(n,n) = 0, we get: n * tan(phi) * Pbar_{n,n}
    const double PBAR_NN = result.P[pbarTriangleIndex(n, n)];
    const double EXPECTED = static_cast<double>(n) * TAN_PHI * PBAR_NN;
    EXPECT_NEAR(result.dP[pbarTriangleIndex(n, n)], EXPECTED, TOL) << "n=" << n;
  }
}

/**
 * @test WithDerivatives_RawBufferAPI
 * @brief Raw buffer API works correctly with size checks.
 */
TEST(MathLegendrePbarDerivatives, WithDerivatives_RawBufferAPI) {
  const int N_MAX = 6;
  const double SIN_PHI = 0.25;
  const double COS_PHI = std::sqrt(1.0 - SIN_PHI * SIN_PHI);
  const auto NEED = pbarTriangleSize(N_MAX);
  const double TOL = 1e-12;

  // Good buffer
  std::vector<double> pBuf(NEED), dpBuf(NEED);
  ASSERT_TRUE(computeNormalizedPbarTriangleWithDerivatives(N_MAX, SIN_PHI, COS_PHI, pBuf.data(),
                                                           dpBuf.data(), NEED));

  auto result = computeNormalizedPbarTriangleWithDerivativesVector(N_MAX, SIN_PHI, COS_PHI);
  for (std::size_t i = 0; i < NEED; ++i) {
    EXPECT_NEAR(pBuf[i], result.P[i], TOL) << "P[" << i << "]";
    EXPECT_NEAR(dpBuf[i], result.dP[i], TOL) << "dP[" << i << "]";
  }

  // Too-small buffer
  if (NEED > 0) {
    std::vector<double> small(NEED - 1);
    EXPECT_FALSE(computeNormalizedPbarTriangleWithDerivatives(N_MAX, SIN_PHI, COS_PHI, pBuf.data(),
                                                              small.data(), NEED - 1));
  }
}

/**
 * @test WithDerivatives_BadInputs
 * @brief Negative degree and null pointers return false.
 */
TEST(MathLegendrePbarDerivatives, WithDerivatives_BadInputs) {
  std::array<double, 1> dummy{};
  std::array<double, 1> dummyDp{};

  // N < 0
  EXPECT_FALSE(
      computeNormalizedPbarTriangleWithDerivatives(-1, 0.0, 1.0, dummy.data(), dummyDp.data(), 1));

  // Null pointers
  EXPECT_FALSE(
      computeNormalizedPbarTriangleWithDerivatives(2, 0.0, 1.0, nullptr, dummyDp.data(), 6));
  EXPECT_FALSE(computeNormalizedPbarTriangleWithDerivatives(2, 0.0, 1.0, dummy.data(), nullptr, 6));
}

/**
 * @test WithDerivatives_N0_SingleValue
 * @brief N=0 should give Pbar_{0,0} = 1 and dPbar_{0,0}/dphi = 0.
 */
TEST(MathLegendrePbarDerivatives, WithDerivatives_N0_SingleValue) {
  std::array<double, 1> p{0.0};
  std::array<double, 1> dp{999.0};

  ASSERT_TRUE(computeNormalizedPbarTriangleWithDerivatives(0, 0.5, std::sqrt(0.75), p.data(),
                                                           dp.data(), 1));
  EXPECT_DOUBLE_EQ(p[0], 1.0);
  EXPECT_DOUBLE_EQ(dp[0], 0.0); // m=0, n=0, so dP = 0*tan(phi)*1 - 0 = 0
}
