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
 * With m = 0 the last line is Bonnet's recurrence for P_n. These values grow
 * like (2m-1)!! and shrink like s^m, so the seed and the recurrence's
 * intermediate products leave double's exponent range long before a final
 * value does. The walk therefore carries the pair (prev, cur) scaled by
 * 2^-scale and renormalises by 2^512 whenever |cur| leaves [2^-512, 2^512]:
 * every intermediate stays representable (|cur| <= 2^512 and a coefficient
 * below 2^13 keep each product under 2^526), an exact zero stays zero, and
 * the single ldexp at the end is the only place range is decided, giving
 * inf or 0 exactly when the true value lies beyond double.
 */
double unnormalizedColumn(int n, int m, double t) noexcept APEX_NONBLOCKING {
  constexpr double UP = 0x1p512;
  constexpr double DOWN = 0x1p-512;
  const double S = sinOf(t);
  int scale = 0;

  double diag = 1.0; // P_{0,0}, non-negative throughout the seed
  double odd = 1.0;  // 2k - 1
  for (int k = 1; k <= m; ++k) {
    diag *= odd * S;
    odd += 2.0;
    if (diag > UP) {
      diag *= DOWN;
      scale += 512;
    } else if (diag < DOWN && diag != 0.0) {
      diag *= UP;
      scale -= 512;
    }
  }
  if (n == m) {
    return std::ldexp(diag, scale);
  }

  double prev = diag;
  double cur = (2.0 * m + 1.0) * t * diag; // P_{m+1,m}
  for (int k = m + 2; k <= n; ++k) {
    const double NEXT =
        ((2.0 * k - 1.0) * t * cur - static_cast<double>(k + m - 1) * prev) / (k - m);
    prev = cur;
    cur = NEXT;
    const double MAG = std::fabs(cur);
    if (MAG > UP) {
      cur *= DOWN;
      prev *= DOWN;
      scale += 512;
    } else if (MAG < DOWN && MAG != 0.0) {
      cur *= UP;
      prev *= UP;
      scale -= 512;
    }
  }
  return std::ldexp(cur, scale);
}

constexpr double NAN_VALUE = std::numeric_limits<double>::quiet_NaN();

/// Index of column m's first pair, {A(m+2,m), B(m+2,m)}, in ScalarTables::ab:
/// columns 0..m-1 hold N-1, N-2, ..., N-m pairs (none once m >= N-1).
inline std::size_t columnPairOffset(int maxDegree, int m) noexcept APEX_NONBLOCKING {
  const auto N = static_cast<std::size_t>(maxDegree);
  const auto M = static_cast<std::size_t>(m);
  return M * (N - 1) - M * (M - 1) / 2;
}

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

/* ----------------------------- Cached API ----------------------------- */

ScalarTables makeScalarTables(int maxDegree) {
  ScalarTables tables;
  if (maxDegree < 0) {
    return tables;
  }
  const auto N = static_cast<std::size_t>(maxDegree);
  tables.maxDegree = maxDegree;
  tables.diag.resize(N + 1);
  tables.off.resize(N + 1);
  tables.ab.resize((N >= 1) ? N * (N - 1) : 0); // (N-1)N/2 pairs

  // The same product the stateless walk forms, so diag[m] matches it bit for bit.
  double ratio = 1.0;
  tables.diag[0] = 1.0;
  for (int m = 1; m <= maxDegree; ++m) {
    ratio *= (m == 1) ? 3.0 : (2.0 * m + 1.0) / (2.0 * m);
    tables.diag[static_cast<std::size_t>(m)] = std::sqrt(ratio);
  }
  for (int m = 0; m <= maxDegree; ++m) {
    tables.off[static_cast<std::size_t>(m)] = std::sqrt(2.0 * m + 3.0);
  }
  // A and B as computeRecurrenceCoefficients tabulates them, stored column-major.
  for (int m = 0; m + 2 <= maxDegree; ++m) {
    double* pair = tables.ab.data() + 2 * columnPairOffset(maxDegree, m);
    for (int k = m + 2; k <= maxDegree; ++k, pair += 2) {
      const double NM = static_cast<double>(k - m);
      const double NPM = static_cast<double>(k + m);
      pair[0] = std::sqrt(((2.0 * k + 1.0) * (2.0 * k - 1.0)) / (NM * NPM));
      pair[1] =
          std::sqrt(((2.0 * k + 1.0) * (NPM - 1.0) * (NM - 1.0)) / ((2.0 * k - 3.0) * NM * NPM));
    }
  }
  return tables;
}

double normalizedAssociatedLegendre(int n, int m, double x,
                                    const ScalarTables& tables) noexcept APEX_NONBLOCKING {
  if (!validOrder(n, m) || n > tables.maxDegree) {
    return NAN_VALUE;
  }
  const double T = clampUnit(x);
  const double S = sinOf(T);

  double power = 1.0; // s^m
  if (m < SCALAR_POW_SEED_MIN_ORDER) {
    for (int k = 0; k < m; ++k) {
      power *= S;
    }
  } else {
    power = std::pow(S, m);
  }
  const double DIAG = tables.diag[static_cast<std::size_t>(m)] * power;
  if (n == m) {
    return DIAG;
  }

  double prev = DIAG;
  double cur = tables.off[static_cast<std::size_t>(m)] * T * DIAG; // Pbar_{m+1,m}
  const double* pair = tables.ab.data() + 2 * columnPairOffset(tables.maxDegree, m);
  for (int k = m + 2; k <= n; ++k, pair += 2) {
    const double NEXT = pair[0] * T * cur - pair[1] * prev;
    prev = cur;
    cur = NEXT;
  }
  return cur;
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
