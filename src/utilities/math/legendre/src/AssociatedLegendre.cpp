/**
 * @file AssociatedLegendre.cpp
 * @brief Implementation of scalar associated Legendre polynomials.
 */

#include "src/utilities/math/legendre/inc/AssociatedLegendre.hpp"

#include "src/utilities/math/legendre/inc/Factorials.hpp"

#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

// Identity transform for simple API
inline double identity(double x) noexcept { return x; }

} // namespace

/* ----------------------------- Simple API ----------------------------- */

double associatedLegendre(int n, int m, double x) {
  return associatedLegendreFunction(n, m, identity, x);
}

double legendrePolynomial(int n, double x) { return associatedLegendre(n, 0, x); }

double normalizedAssociatedLegendre(int n, int m, double x) {
  return normalizedAssociatedLegendreFunction(n, m, identity, x);
}

/* ----------------------------- Transform API ----------------------------- */

double associatedLegendreFunction(int n, int m, std::function<double(double)> fX, double x) {
  // Preconditions (embedded-friendly; no exceptions)
  if (n < 0 || m < 0 || m > n) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  // Clamp x into [-1, 1] to avoid domain drift
  if (x > 1.0)
    x = 1.0;
  if (x < -1.0)
    x = -1.0;

  const double T = fX(x);
  const double T2 = T * T;

  const int DIFF = n - m;
  const int R = DIFF / 2;

  // Factorials up to 2n for the summation coefficients
  auto facts = precomputedFactorials(2 * n);
  const double* factorialTbl = facts.data();

  const double TWO_NEG_N = std::ldexp(1.0, -n);

  // Stable (1 - t^2)^(m/2): use exp(log1p(-t^2)) to behave better near |t| ~ 1
  const double FACTOR_M = (m == 0) ? 1.0 : std::exp(0.5 * m * std::log1p(-T2));

  // Special-case t == 0: only the term where exponent becomes zero contributes
  if (T2 == 0.0) {
    if (DIFF == 0) {
      // k = 0 hits exponent 0
      double term = factorialTbl[static_cast<std::size_t>(2 * n)] /
                    (factorialTbl[0] * factorialTbl[static_cast<std::size_t>(n)] *
                     factorialTbl[static_cast<std::size_t>(DIFF)]);
      return TWO_NEG_N * FACTOR_M * term;
    } else if ((DIFF & 1) == 0) {
      const int K0 = DIFF / 2;
      if (K0 <= R) {
        double term = factorialTbl[static_cast<std::size_t>(2 * n - 2 * K0)] /
                      (factorialTbl[static_cast<std::size_t>(K0)] *
                       factorialTbl[static_cast<std::size_t>(n - K0)] *
                       factorialTbl[static_cast<std::size_t>(DIFF - 2 * K0)]);
        if (K0 & 1)
          term = -term;
        return TWO_NEG_N * FACTOR_M * term;
      }
    }
    return 0.0;
  }

  // General path
  double tPower = (DIFF == 0 ? 1.0 : std::pow(T, DIFF));
  const double INV_T_SQ = 1.0 / T2;

  double sum = 0.0;
  for (int k = 0; k <= R; ++k) {
    // term = (2n-2k)! / (k! (n-k)! (DIFF-2k)!)
    double term =
        factorialTbl[static_cast<std::size_t>(2 * n - 2 * k)] /
        (factorialTbl[static_cast<std::size_t>(k)] * factorialTbl[static_cast<std::size_t>(n - k)] *
         factorialTbl[static_cast<std::size_t>(DIFF - 2 * k)]);
    if (k & 1)
      term = -term;
    sum += term * tPower;
    tPower *= INV_T_SQ; // t^(DIFF - 2(k+1))
  }

  return TWO_NEG_N * FACTOR_M * sum;
}

double legendrePolynomialFunction(int n, std::function<double(double)> fX, double x) {
  return associatedLegendreFunction(n, 0, fX, x);
}

/*
 * Normalization convention: EGM/IERS fully-normalized associated Legendre functions.
 *
 *   Pbar_{nm}(x) = N_{nm} P_{nm}(x),
 *   N_{nm} = sqrt( (2n+1) * (2 - delta_{0m}) * (n-m)! / (n+m)! )
 *
 * Key implications:
 * - For m = 0: Pbar_{n0}(x) = sqrt(2n+1) * P_n(x)
 * - For m > 0: Pbar_{nm}(x) = sqrt(2(2n+1) * (n-m)!/(n+m)!) * P_n^m(x)
 *
 * This matches the normalization used by EGM2008 (and IERS Conventions),
 * ensuring compatibility with fully-normalized spherical harmonic coefficients
 * Cbar_{nm}, Sbar_{nm}.
 */

double normalizedAssociatedLegendreFunction(int n, int m, std::function<double(double)> fX,
                                            double x) {
  if (n < 0 || m < 0 || m > n) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  const double PNM = associatedLegendreFunction(n, m, fX, x);

  auto facts = precomputedFactorials(n + m);
  const double* factorialTbl = facts.data();

  const double RATIO = factorialTbl[static_cast<std::size_t>(n - m)] /
                       factorialTbl[static_cast<std::size_t>(n + m)]; // (n-m)!/(n+m)!
  const double TWO_MINUS_DELTA0M = (m == 0) ? 1.0 : 2.0;              // (2 - delta_{0m})
  const double SCALE = std::sqrt((2.0 * n + 1.0) * TWO_MINUS_DELTA0M * RATIO);

  return PNM * SCALE;
}

} // namespace legendre
} // namespace math
} // namespace apex
