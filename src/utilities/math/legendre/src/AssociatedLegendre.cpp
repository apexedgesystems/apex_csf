/**
 * @file AssociatedLegendre.cpp
 * @brief Implementation of scalar associated Legendre functions.
 */

#include "src/utilities/math/legendre/inc/AssociatedLegendre.hpp"

#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

inline bool validOrder(int n, int m) noexcept APEX_NONBLOCKING {
  return n >= 0 && m >= 0 && m <= n;
}

/// Clamp into [-1, 1]; NaN passes through.
inline double clampUnit(double t) noexcept APEX_NONBLOCKING {
  if (t > 1.0) {
    return 1.0;
  }
  if (t < -1.0) {
    return -1.0;
  }
  return t;
}

/// sqrt(1 - t^2) for t in [-1, 1]. (1 - t)(1 + t) keeps its digits as |t| -> 1,
/// where 1 - t*t cancels; both factors are non-negative, so no branch is needed
/// and NaN propagates.
inline double sinOf(double t) noexcept APEX_NONBLOCKING { return std::sqrt((1.0 - t) * (1.0 + t)); }

/*
 * Fully normalized column m, walked up to degree n (t in [-1, 1], 0 <= m <= n):
 *
 *   Pbar_{m,m} = sqrt(2 prod_{k=1..m} (2k+1)/(2k)) s^m  (m >= 1; Pbar_{0,0} = 1; s = sqrt(1-t^2))
 *   Pbar_{k,m} = A(k,m) t Pbar_{k-1,m} - B(k,m) Pbar_{k-2,m}
 *   A(k,m) = sqrt((2k+1)(2k-1) / ((k-m)(k+m)))
 *   B(k,m) = sqrt((2k+1)(k+m-1)(k-m-1) / ((2k-3)(k-m)(k+m))) = A(k,m) / A(k-1,m)
 *
 * The diagonal is the triangle's product of sqrt((2k+1)/(2k)) s terms with a
 * single square root taken at the end. The off-diagonal step is the triangle's
 * recurrence with B formed from the identity B(k,m) = A(k,m)/A(k-1,m), so each
 * step takes one square root instead of two; A(m+1,m) = sqrt(2m+3) is the
 * first off-diagonal factor, Pbar_{m+1,m} = sqrt(2m+3) t Pbar_{m,m}. Every
 * intermediate is a normalized value, bounded by sqrt(2k+1), so nothing
 * overflows at any degree.
 */
double normalizedColumn(int n, int m, double t) noexcept APEX_NONBLOCKING {
  const double S = sinOf(t);

  double diag = 1.0; // Pbar_{0,0}
  if (m >= 1) {
    double ratio = 3.0; // 2 * prod_{k=1..m} (2k+1)/(2k)
    double power = S;   // s^m
    for (int k = 2; k <= m; ++k) {
      ratio *= (2.0 * k + 1.0) / (2.0 * k);
      power *= S;
    }
    diag = std::sqrt(ratio) * power;
  }
  if (n == m) {
    return diag;
  }

  double aPrev = std::sqrt(2.0 * m + 3.0); // A(m+1, m)
  double prev = diag;
  double cur = aPrev * t * diag; // Pbar_{m+1,m}
  for (int k = m + 2; k <= n; ++k) {
    const double A = std::sqrt(((2.0 * k + 1.0) * (2.0 * k - 1.0)) /
                               (static_cast<double>(k - m) * static_cast<double>(k + m)));
    const double NEXT = A * (t * cur - prev / aPrev);
    prev = cur;
    cur = NEXT;
    aPrev = A;
  }
  return cur;
}

/*
 * Unnormalized column m, walked up to degree n (t in [-1, 1], 0 <= m <= n):
 *
 *   P_{m,m} = (2m-1)!! s^m
 *   P_{m+1,m} = (2m+1) t P_{m,m}
 *   (k-m) P_{k,m} = (2k-1) t P_{k-1,m} - (k+m-1) P_{k-2,m}
 *
 * With m = 0 the last line is Bonnet's recurrence for P_n.
 */
double unnormalizedColumn(int n, int m, double t) noexcept APEX_NONBLOCKING {
  const double S = sinOf(t);

  double diag = 1.0; // P_{0,0}
  double odd = 1.0;  // 2k - 1
  for (int k = 1; k <= m; ++k) {
    diag *= odd * S;
    odd += 2.0;
  }
  if (n == m) {
    return diag;
  }

  double prev = diag;
  double cur = (2.0 * m + 1.0) * t * diag; // P_{m+1,m}
  for (int k = m + 2; k <= n; ++k) {
    const double NEXT =
        ((2.0 * k - 1.0) * t * cur - static_cast<double>(k + m - 1) * prev) / (k - m);
    prev = cur;
    cur = NEXT;
  }
  return cur;
}

constexpr double NAN_VALUE = std::numeric_limits<double>::quiet_NaN();

} // namespace

/* ----------------------------- Simple API ----------------------------- */

double associatedLegendre(int n, int m, double x) noexcept APEX_NONBLOCKING {
  return validOrder(n, m) ? unnormalizedColumn(n, m, clampUnit(x)) : NAN_VALUE;
}

double legendrePolynomial(int n, double x) noexcept APEX_NONBLOCKING {
  return associatedLegendre(n, 0, x);
}

/*
 * Normalization convention: EGM/IERS fully-normalized associated Legendre functions.
 *
 *   Pbar_{nm}(x) = N_{nm} P_{nm}(x),
 *   N_{nm} = sqrt( (2n+1) * (2 - delta_{0m}) * (n-m)! / (n+m)! )
 *
 * This matches the normalization used by EGM2008 (and IERS Conventions),
 * ensuring compatibility with fully-normalized spherical harmonic coefficients
 * Cbar_{nm}, Sbar_{nm}. The column walk produces Pbar directly; N_{nm} is never
 * formed.
 */
double normalizedAssociatedLegendre(int n, int m, double x) noexcept APEX_NONBLOCKING {
  return validOrder(n, m) ? normalizedColumn(n, m, clampUnit(x)) : NAN_VALUE;
}

/* ----------------------------- Transform API ----------------------------- */

double associatedLegendreFunction(int n, int m, std::function<double(double)> fX, double x) {
  return validOrder(n, m) ? unnormalizedColumn(n, m, clampUnit(fX(x))) : NAN_VALUE;
}

double legendrePolynomialFunction(int n, std::function<double(double)> fX, double x) {
  return associatedLegendreFunction(n, 0, std::move(fX), x);
}

double normalizedAssociatedLegendreFunction(int n, int m, std::function<double(double)> fX,
                                            double x) {
  return validOrder(n, m) ? normalizedColumn(n, m, clampUnit(fX(x))) : NAN_VALUE;
}

} // namespace legendre
} // namespace math
} // namespace apex
