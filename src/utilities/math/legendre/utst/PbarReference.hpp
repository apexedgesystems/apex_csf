#ifndef APEX_UTILITIES_MATH_LEGENDRE_UTST_PBAR_REFERENCE_HPP
#define APEX_UTILITIES_MATH_LEGENDRE_UTST_PBAR_REFERENCE_HPP
/**
 * @file PbarReference.hpp
 * @brief Extended-precision reference for Pbar_n^m, for accuracy tests.
 *
 * The plain column walk (two square roots per step, the diagonal one order at
 * a time) evaluated in long double: on x86-64 that carries three more decimal
 * digits than double and a far wider exponent range, so it resolves the
 * double implementations' rounding and does not underflow where they do. It
 * takes the same double argument they take, so it measures their arithmetic,
 * not the rounding of the argument itself.
 */

#include <cmath>

namespace legendre_test {

/// Pbar_n^m(x) in long double; 0 <= m <= n, x in [-1, 1].
inline long double pbarReference(int n, int m, double x) {
  const long double T = static_cast<long double>(x);
  const long double S = std::sqrt((1.0L - T) * (1.0L + T));
  long double diag = 1.0L;
  for (int k = 1; k <= m; ++k) {
    diag *= std::sqrt((2.0L * k + 1.0L) / ((k == 1) ? 1.0L : 2.0L * k)) * S;
  }
  if (n == m) {
    return diag;
  }
  long double prev = diag;
  long double cur = std::sqrt(2.0L * m + 3.0L) * T * diag;
  for (int k = m + 2; k <= n; ++k) {
    const long double NM = k - m;
    const long double NPM = k + m;
    const long double A = std::sqrt((2.0L * k + 1.0L) * (2.0L * k - 1.0L) / (NM * NPM));
    const long double B =
        std::sqrt((2.0L * k + 1.0L) * (NPM - 1.0L) * (NM - 1.0L) / ((2.0L * k - 3.0L) * NM * NPM));
    const long double NEXT = A * T * cur - B * prev;
    prev = cur;
    cur = NEXT;
  }
  return cur;
}

/// Arguments within about 2.6 degrees of either pole, where 1 - x*x cancels.
inline constexpr double NEAR_POLE_ARGS[] = {0.999, 0.99999, 0.9999999, -0.9999999, 0.999999999};

/// Smallest diagonal seed a double holds with full digits. Below it the seed is
/// subnormal (a documented limit of every forward-column method) and the
/// whole column inherits the lost digits.
inline constexpr long double NORMAL_FLOOR = 2.3e-308L;

/// Column-scaled accuracy of an implementation over the triangle up to N at x.
struct ColumnScaledError {
  double worst{0.0}; ///< max over columns of max_n |got - ref| / max_n |ref|
  int worstM{-1};    ///< column holding the worst error
  int columns{0};    ///< columns compared (those with a normal diagonal seed)
};

/**
 * @brief Compare get(n, m) with the reference over every column whose seed is normal.
 *
 * Each column's error is measured against that column's largest reference
 * magnitude: near the poles a high-degree column crosses zero many times, and
 * an entry-wise relative error there would measure the zero, not the
 * arithmetic. Seed precision, which scales the whole column, is still seen in
 * full.
 */
template <class Get> ColumnScaledError columnScaledError(int N, double x, Get get) {
  ColumnScaledError out;
  for (int m = 0; m <= N; ++m) {
    if (std::fabs(pbarReference(m, m, x)) < NORMAL_FLOOR) {
      continue;
    }
    long double colMax = 0.0L;
    long double errMax = 0.0L;
    for (int n = m; n <= N; ++n) {
      const long double REF = pbarReference(n, m, x);
      colMax = std::fmax(colMax, std::fabs(REF));
      errMax = std::fmax(errMax, std::fabs(static_cast<long double>(get(n, m)) - REF));
    }
    const auto REL = static_cast<double>(errMax / colMax);
    if (REL > out.worst) {
      out.worst = REL;
      out.worstM = m;
    }
    ++out.columns;
  }
  return out;
}

} // namespace legendre_test

#endif // APEX_UTILITIES_MATH_LEGENDRE_UTST_PBAR_REFERENCE_HPP
