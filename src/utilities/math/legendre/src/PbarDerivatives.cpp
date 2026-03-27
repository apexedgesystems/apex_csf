/**
 * @file PbarDerivatives.cpp
 * @brief Implementation of Legendre function derivatives for spherical harmonics.
 */

#include "src/utilities/math/legendre/inc/PbarDerivatives.hpp"

#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

#include <cmath>
#include <limits>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- Constants ----------------------------- */

namespace {

// Minimum cos(phi) to avoid division by zero in tan(phi)
constexpr double COS_PHI_MIN = 1e-12;

} // namespace

/* ----------------------------- Beta Coefficients ----------------------------- */

bool computeBetaCoefficients(int n, double* out, std::size_t outLen) noexcept {
  if (n < 0 || !out) {
    return false;
  }

  const std::size_t NEED = pbarTriangleSize(n);
  if (outLen < NEED) {
    return false;
  }

  for (int deg = 0; deg <= n; ++deg) {
    for (int ord = 0; ord <= deg; ++ord) {
      const std::size_t IDX = pbarTriangleIndex(deg, ord);
      if (ord < deg) {
        // beta(n,m) = sqrt((n-m)(n+m+1))
        out[IDX] = std::sqrt(static_cast<double>((deg - ord) * (deg + ord + 1)));
      } else {
        // beta(n,n) = 0 (boundary)
        out[IDX] = 0.0;
      }
    }
  }

  return true;
}

std::vector<double> computeBetaCoefficientsVector(int n) noexcept {
  const std::size_t NEED = pbarTriangleSize(n);
  std::vector<double> beta(NEED);
  computeBetaCoefficients(n, beta.data(), beta.size());
  return beta;
}

/* ----------------------------- Combined P and dP/dphi ----------------------------- */

bool computeNormalizedPbarTriangleWithDerivatives(int n, double sinPhi, double cosPhi, double* pOut,
                                                  double* dpOut, std::size_t outLen) noexcept {
  if (n < 0 || !pOut || !dpOut) {
    return false;
  }

  const std::size_t NEED = pbarTriangleSize(n);
  if (outLen < NEED) {
    return false;
  }

  // First compute Pbar triangle
  if (!computeNormalizedPbarTriangle(n, sinPhi, pOut, outLen)) {
    return false;
  }

  // Clamp cos(phi) to avoid division by zero
  const double COS_SAFE = (std::abs(cosPhi) < COS_PHI_MIN) ? COS_PHI_MIN : cosPhi;
  const double TAN_PHI = sinPhi / COS_SAFE;

  // Compute dPbar/dphi using the recurrence relation:
  //   dPbar_{n,m}/dphi = m * tan(phi) * Pbar_{n,m} - beta(n,m) * Pbar_{n,m+1}
  for (int deg = 0; deg <= n; ++deg) {
    for (int ord = 0; ord <= deg; ++ord) {
      const std::size_t IDX = pbarTriangleIndex(deg, ord);
      const double PBAR_NM = pOut[IDX];

      // First term: m * tan(phi) * Pbar_{n,m}
      double dP = static_cast<double>(ord) * TAN_PHI * PBAR_NM;

      // Second term: -beta(n,m) * Pbar_{n,m+1}
      if (ord < deg) {
        const double BETA = std::sqrt(static_cast<double>((deg - ord) * (deg + ord + 1)));
        const double PBAR_NM1 = pOut[pbarTriangleIndex(deg, ord + 1)];
        dP -= BETA * PBAR_NM1;
      }

      dpOut[IDX] = dP;
    }
  }

  return true;
}

PbarWithDerivatives computeNormalizedPbarTriangleWithDerivativesVector(int n, double sinPhi,
                                                                       double cosPhi) noexcept {
  PbarWithDerivatives result;
  const std::size_t NEED = pbarTriangleSize(n);

  result.P.resize(NEED);
  result.dP.resize(NEED);

  computeNormalizedPbarTriangleWithDerivatives(n, sinPhi, cosPhi, result.P.data(), result.dP.data(),
                                               NEED);
  return result;
}

bool computeNormalizedPbarTriangleWithDerivativesCached(int n, double sinPhi, double cosPhi,
                                                        const double* beta, double* pOut,
                                                        double* dpOut,
                                                        std::size_t outLen) noexcept {
  if (n < 0 || !beta || !pOut || !dpOut) {
    return false;
  }

  const std::size_t NEED = pbarTriangleSize(n);
  if (outLen < NEED) {
    return false;
  }

  // First compute Pbar triangle
  if (!computeNormalizedPbarTriangle(n, sinPhi, pOut, outLen)) {
    return false;
  }

  // Clamp cos(phi) to avoid division by zero
  const double COS_SAFE = (std::abs(cosPhi) < COS_PHI_MIN) ? COS_PHI_MIN : cosPhi;
  const double TAN_PHI = sinPhi / COS_SAFE;

  // Compute dPbar/dphi using precomputed beta coefficients
  for (int deg = 0; deg <= n; ++deg) {
    for (int ord = 0; ord <= deg; ++ord) {
      const std::size_t IDX = pbarTriangleIndex(deg, ord);
      const double PBAR_NM = pOut[IDX];

      // First term: m * tan(phi) * Pbar_{n,m}
      double dP = static_cast<double>(ord) * TAN_PHI * PBAR_NM;

      // Second term: -beta(n,m) * Pbar_{n,m+1}
      if (ord < deg) {
        const double PBAR_NM1 = pOut[pbarTriangleIndex(deg, ord + 1)];
        dP -= beta[IDX] * PBAR_NM1;
      }

      dpOut[IDX] = dP;
    }
  }

  return true;
}

} // namespace legendre
} // namespace math
} // namespace apex
