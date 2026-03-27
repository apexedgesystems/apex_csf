/**
 * @file SphericalHarmonicModel.cpp
 * @brief Implementation of body-agnostic spherical harmonic gravity model.
 */

#include "src/sim/environment/gravity/inc/SphericalHarmonicModel.hpp"

#include "src/utilities/math/legendre/inc/PbarDerivatives.hpp"
#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"

#include <cmath>

using apex::math::legendre::computeBetaCoefficients;
using apex::math::legendre::computeNormalizedPbarTriangle;
using apex::math::legendre::computeNormalizedPbarTriangleWithDerivativesCached;

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- SphericalHarmonicModel Methods ----------------------------- */

void SphericalHarmonicModel::ensureScratchCapacity(int16_t N) noexcept {
  const std::size_t n = static_cast<std::size_t>(N);
  const std::size_t tri = (n + 1) * (n + 2) / 2;
  const std::size_t needM = n + 1;

  if (P_.size() != tri)
    P_.resize(tri);
  if (dP_.size() != tri)
    dP_.resize(tri);
  if (cosml_.size() != needM)
    cosml_.resize(needM);
  if (sinml_.size() != needM)
    sinml_.resize(needM);
}

bool SphericalHarmonicModel::loadCoefficients(const CoeffSource& src, int16_t N) noexcept {
  const std::size_t tri = static_cast<std::size_t>(N + 1) * static_cast<std::size_t>(N + 2) / 2u;
  // Interleaved storage: 2 doubles per coefficient (C, S)
  const std::size_t interleavedSize = tri * 2;
  if (coeffCS_.size() != interleavedSize)
    coeffCS_.resize(interleavedSize);

  const int n0 = static_cast<int>(src.minDegree());

  for (int n = 0; n <= N; ++n) {
    for (int m = 0; m <= n; ++m) {
      double c = 0.0, s = 0.0;

      if (n < n0) {
        // Use standard normalized coefficients for degrees below source minimum.
        // C00 = 1.0 (normalized), all others = 0.0 (center of mass convention).
        if (n == 0 && m == 0) {
          c = 1.0;
        }
      } else {
        if (!src.get(static_cast<int16_t>(n), static_cast<int16_t>(m), c, s)) {
          return false;
        }
      }

      const std::size_t k = idx(n, m);
      // Interleaved: coeffCS_[2k] = C, coeffCS_[2k+1] = S
      coeffCS_[2 * k] = c;
      coeffCS_[2 * k + 1] = s;
    }
  }
  return true;
}

void SphericalHarmonicModel::buildBetaTable(int16_t N) noexcept {
  const std::size_t tri = static_cast<std::size_t>(N + 1) * static_cast<std::size_t>(N + 2) / 2u;
  if (beta_.size() != tri)
    beta_.resize(tri, 0.0);

  computeBetaCoefficients(static_cast<int>(N), beta_.data(), beta_.size());

  if (recurrA_.size() != tri)
    recurrA_.resize(tri, 0.0);
  if (recurrB_.size() != tri)
    recurrB_.resize(tri, 0.0);

  apex::math::legendre::computeRecurrenceCoefficients(static_cast<int>(N), recurrA_.data(),
                                                      recurrB_.data(), tri);
}

bool SphericalHarmonicModel::init(const CoeffSource& src,
                                  const SphericalHarmonicParams& p) noexcept {
  if (p.GM <= 0.0 || p.a <= 0.0 || p.N < 0)
    return false;

  const int16_t nMax = src.maxDegree();
  if (nMax < 0)
    return false;

  N_ = std::min<std::int16_t>(p.N, nMax);
  GM_ = p.GM;
  a_ = p.a;
  src_ = &src;

  ensureScratchCapacity(N_);
  if (!loadCoefficients(src, N_)) {
    return false;
  }
  buildBetaTable(N_);
  return true;
}

void SphericalHarmonicModel::computePbar(double x) const noexcept {
  (void)apex::math::legendre::computeNormalizedPbarTriangleCached(
      N_, x, recurrA_.data(), recurrB_.data(), P_.data(), P_.size());
}

bool SphericalHarmonicModel::potential(const double r[3], double& V) const noexcept {
  if (!src_ || N_ < 0)
    return false;

  const double x = r[0], y = r[1], z = r[2];
  const double r2 = x * x + y * y + z * z;
  const double rmag = std::sqrt(r2);
  if (rmag == 0.0) {
    V = 0.0;
    return false;
  }

  const double sphi = z / rmag;
  const double rxy = std::sqrt(x * x + y * y);

  double cl = 1.0, sl = 0.0;
  if (rxy > 0.0) {
    cl = x / rxy;
    sl = y / rxy;
  }

  cosml_[0] = 1.0;
  sinml_[0] = 0.0;
  if (N_ >= 1) {
    cosml_[1] = cl;
    sinml_[1] = sl;
  }
  for (int m = 2; m <= N_; ++m) {
    cosml_[m] = cosml_[m - 1] * cl - sinml_[m - 1] * sl;
    sinml_[m] = sinml_[m - 1] * cl + cosml_[m - 1] * sl;
  }

  computePbar(sphi);

  const double u = a_ / rmag;
  double uPow = 1.0;
  double sumN = 0.0;

  // Interleaved coefficient storage: coeffCS_[2k] = C, coeffCS_[2k+1] = S
  const double* __restrict__ pCoeffCS = coeffCS_.data();
  const double* __restrict__ pP = P_.data();
  const double* __restrict__ pCosml = cosml_.data();
  const double* __restrict__ pSinml = sinml_.data();

  std::size_t rowBase = 0;
  for (int n = 0; n <= N_; ++n) {
    double sn = 0.0;
    const int nPlusOne = n + 1;
#ifdef _OPENMP
#pragma omp simd reduction(+ : sn)
#endif
    for (int m = 0; m < nPlusOne; ++m) {
      const std::size_t k = rowBase + static_cast<std::size_t>(m);
      const std::size_t k2 = k * 2;
      const double f = pCoeffCS[k2] * pCosml[m] + pCoeffCS[k2 + 1] * pSinml[m];
      sn += pP[k] * f;
    }
    sumN += uPow * sn;
    uPow *= u;
    rowBase += static_cast<std::size_t>(n + 1);
  }

  V = (GM_ / rmag) * sumN;
  return true;
}

bool SphericalHarmonicModel::evaluate(const double r[3], double& V, double a[3]) const noexcept {
  if (accelMode_ != AccelMode::Analytic) {
    if (!potential(r, V))
      return false;

    const double eps = 0.1;
    for (int i = 0; i < 3; ++i) {
      double rp[3] = {r[0], r[1], r[2]};
      double rm[3] = {r[0], r[1], r[2]};
      rp[i] += eps;
      rm[i] -= eps;

      double vp = 0.0, vm = 0.0;
      if (!potential(rp, vp))
        return false;
      if (!potential(rm, vm))
        return false;

      a[i] = (vp - vm) / (2.0 * eps);
    }
    return true;
  }

  if (!src_ || N_ < 0)
    return false;

  const double x = r[0], y = r[1], z = r[2];
  const double r2 = x * x + y * y + z * z;
  const double rmag = std::sqrt(r2);
  if (rmag == 0.0) {
    V = 0.0;
    a[0] = a[1] = a[2] = 0.0;
    return false;
  }

  const double rxy = std::sqrt(x * x + y * y);
  const double sphi = z / rmag;
  double cphi = (rxy > 0.0) ? (rxy / rmag) : 0.0;
  constexpr double TINY = 1e-12;
  if (cphi < TINY)
    cphi = TINY;

  double cl = 1.0, sl = 0.0;
  if (rxy > 0.0) {
    cl = x / rxy;
    sl = y / rxy;
  }

  cosml_[0] = 1.0;
  sinml_[0] = 0.0;
  if (N_ >= 1) {
    cosml_[1] = cl;
    sinml_[1] = sl;
  }
  for (int m = 2; m <= N_; ++m) {
    cosml_[m] = cosml_[m - 1] * cl - sinml_[m - 1] * sl;
    sinml_[m] = sinml_[m - 1] * cl + cosml_[m - 1] * sl;
  }

  computeNormalizedPbarTriangleWithDerivativesCached(static_cast<int>(N_), sphi, cphi, beta_.data(),
                                                     P_.data(), dP_.data(), P_.size());

  const double u = a_ / rmag;
  double uPow = 1.0;

  double sumN = 0.0;
  double sumR = 0.0;
  double sumPhi = 0.0;
  double sumLam = 0.0;

  // Interleaved coefficient storage: coeffCS_[2k] = C, coeffCS_[2k+1] = S
  const double* __restrict__ pCoeffCS = coeffCS_.data();
  const double* __restrict__ pP = P_.data();
  const double* __restrict__ pdP = dP_.data();
  const double* __restrict__ pCosml = cosml_.data();
  const double* __restrict__ pSinml = sinml_.data();

  std::size_t rowBase = 0;
  for (int n = 0; n <= N_; ++n) {
    double sN = 0.0;
    double tPhi = 0.0;
    double tLam = 0.0;

    {
      const std::size_t k0 = rowBase;
      const double f0 = pCoeffCS[k0 * 2]; // C coefficient only for m=0
      const double pnm0 = pP[k0];
      sN += pnm0 * f0;
      tPhi += pdP[k0] * f0;
    }

#ifdef _OPENMP
#pragma omp simd reduction(+ : sN, tPhi, tLam)
#endif
    for (int m = 1; m <= n; ++m) {
      const std::size_t k = rowBase + static_cast<std::size_t>(m);
      const std::size_t k2 = k * 2;
      const double c = pCoeffCS[k2];
      const double s = pCoeffCS[k2 + 1];

      const double cm = pCosml[m];
      const double sm = pSinml[m];

      const double f = c * cm + s * sm;
      const double g = s * cm - c * sm;

      const double pnm = pP[k];
      sN += pnm * f;
      tPhi += pdP[k] * f;
      tLam += static_cast<double>(m) * pnm * g;
    }

    sumN += uPow * sN;
    sumR += static_cast<double>(n + 1) * uPow * sN;
    sumPhi += uPow * tPhi;
    sumLam += uPow * tLam;

    uPow *= u;
    rowBase += static_cast<std::size_t>(n + 1);
  }

  V = (GM_ / rmag) * sumN;

  const double dVr = -(GM_ / (rmag * rmag)) * sumR;
  const double dVphi = (GM_ / rmag) * sumPhi;
  const double dVlam = (GM_ / rmag) * sumLam;

  double dVdx = dVr * (x / rmag);
  double dVdy = dVr * (y / rmag);
  double dVdz = dVr * (z / rmag);

  if (rxy >= TINY) {
    const double invR2 = 1.0 / (rmag * rmag);
    const double invRxy = 1.0 / rxy;
    const double invRxy2 = invRxy * invRxy;

    const double commonX = -(z * x) * invR2 * invRxy;
    const double commonY = -(z * y) * invR2 * invRxy;
    const double commonZ = (rxy)*invR2;

    dVdx += dVphi * commonX;
    dVdy += dVphi * commonY;
    dVdz += dVphi * commonZ;

    dVdx += dVlam * (-y * invRxy2);
    dVdy += dVlam * (x * invRxy2);
  }

  a[0] = dVdx;
  a[1] = dVdy;
  a[2] = dVdz;
  return true;
}

bool SphericalHarmonicModel::acceleration(const double r[3], double a[3]) const noexcept {
  if (accelMode_ != AccelMode::Analytic) {
    double v0 = 0.0;
    if (!potential(r, v0))
      return false;

    const double eps = 0.1;
    for (int i = 0; i < 3; ++i) {
      double rp[3] = {r[0], r[1], r[2]};
      double rm[3] = {r[0], r[1], r[2]};
      rp[i] += eps;
      rm[i] -= eps;

      double vp = 0.0, vm = 0.0;
      if (!potential(rp, vp))
        return false;
      if (!potential(rm, vm))
        return false;

      a[i] = (vp - vm) / (2.0 * eps);
    }
    return true;
  }

  double vDummy = 0.0;
  return evaluate(r, vDummy, a);
}

} // namespace gravity
} // namespace environment
} // namespace sim
