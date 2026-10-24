#ifndef APEX_UTILITIES_MATH_LEGENDRE_ASSOCIATED_LEGENDRE_HPP
#define APEX_UTILITIES_MATH_LEGENDRE_ASSOCIATED_LEGENDRE_HPP
/**
 * @file AssociatedLegendre.hpp
 * @brief Scalar associated Legendre function evaluation (CPU).
 *
 * Provides:
 *  - Unnormalized associated Legendre function P_n^m(x)
 *  - Legendre polynomial P_n(x) = P_n^0(x)
 *  - Fully normalized (EGM/IERS convention) associated Legendre function Pbar_n^m(x)
 *
 * Every value is the top of column m of the stable upward three-term
 * recurrence in n, seeded on the diagonal: an O(m) seed then an O(n - m)
 * walk, O(n) in all; no allocation, no table. The normalized form walks the recurrence
 * computeNormalizedPbarTriangle applies to every column (forming one coefficient from an identity
 * of the other to save a square root per step), so a scalar value agrees with the triangle entry at
 * pbarTriangleIndex(n, m) to rounding. Both forms omit the Condon-Shortley phase (geodesy
 * convention).
 *
 * Range:
 *  - Normalized values are bounded by sqrt(2n+1) and stay accurate through
 *    n = 2190, the EGM2008 degree.
 *  - Unnormalized values grow like (2m-1)!!. The walk carries a binary
 *    exponent alongside, so intermediates never overflow or underflow; the
 *    result is inf only where the true value exceeds DBL_MAX (P_n^n(0) =
 *    (2n-1)!! does from n = 151) and 0 only where it is below the smallest
 *    subnormal. Use the normalized form at high degree. For a subnormal
 *    argument the odd-offset values (n - m odd) are proportional to x and
 *    begin life as subnormals, so they carry only the precision a subnormal
 *    of their size can hold (about 34 bits at the smallest subnormal) and
 *    later values of that parity inherit it; the even-offset values are
 *    unaffected.
 *  - Near |x| = 1 with large m, the diagonal seed (1 - x^2)^(m/2) falls into
 *    subnormals and then to zero (for example x = 0.999 from m near 240), and
 *    the column loses its digits with it. This is true of every forward-column
 *    method, std::sph_legendre included; the remedy is Holmes-Featherstone
 *    scaling, which this library does not apply.
 *
 * API styles:
 *  - Simple: evaluation at x, clamped to [-1, 1]. Real-time safe.
 *  - Cached: the normalized function with its coefficients precomputed once
 *    in a ScalarTables (see below). Real-time safe once the tables exist.
 *  - Transform: evaluation at fX(x), with the transformed value clamped to
 *    [-1, 1] (for example fX = cos with a colatitude in [0, pi]).
 *
 * Invalid orders (n < 0, m < 0, m > n) return NaN. A NaN argument returns
 * NaN for every function but the constant P_0^0 = 1.
 */

#include "compat_rt_attrs.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- Simple API ----------------------------- */

/**
 * @brief Unnormalized associated Legendre function P_n^m(x).
 *
 * Valid for integer degree n >= 0 and order 0 <= m <= n.
 *
 * @param n Degree (n >= 0).
 * @param m Order (0 <= m <= n).
 * @param x Argument (clamped to [-1, 1]).
 * @return P_n^m(x); inf where the true value exceeds DBL_MAX and 0 where it is
 *         below the smallest subnormal; NaN for invalid n/m.
 * @note RT-safe: O(m) seed plus O(n - m) walk, O(n) in all; no allocation.
 */
[[nodiscard]] double associatedLegendre(int n, int m, double x) noexcept APEX_NONBLOCKING;

/**
 * @brief Legendre polynomial P_n(x) == P_n^0(x).
 *
 * @param n Degree (n >= 0).
 * @param x Argument (clamped to [-1, 1]).
 * @return P_n(x), or NaN for n < 0.
 * @note RT-safe: O(n), no allocation.
 */
[[nodiscard]] double legendrePolynomial(int n, double x) noexcept APEX_NONBLOCKING;

/**
 * @brief Normalized associated Legendre function (EGM/IERS orthonormal convention).
 *
 *   Pbar_n^m(x) = sqrt( (2 - delta_{0m}) * (2n+1) * (n-m)! / (n+m)! ) * P_n^m(x)
 *
 * Valid for 0 <= m <= n.
 *
 * @param n Degree (n >= 0).
 * @param m Order (0 <= m <= n).
 * @param x Argument (clamped to [-1, 1]).
 * @return Pbar_n^m(x), or NaN for invalid n/m.
 * @note RT-safe: O(m) seed plus O(n - m) walk, O(n) in all; no allocation.
 */
[[nodiscard]] double normalizedAssociatedLegendre(int n, int m, double x) noexcept APEX_NONBLOCKING;

/* ----------------------------- Cached API ----------------------------- */

/// Orders below this build the diagonal seed s^m with a multiply loop; at or
/// above it, one std::pow call. A fixed cross-target choice: the loop/pow
/// crossover measured at m = 32 on an aarch64 Thor and near m = 56 on an
/// x86-64 laptop; at m = 32 the laptop sample paid about 7 ns for the pow
/// seed over the loop.
inline constexpr int SCALAR_POW_SEED_MIN_ORDER = 32;

/**
 * @struct ScalarTables
 * @brief Coefficients the normalized column walk would otherwise recompute,
 *        precomputed once for every degree up to maxDegree.
 *
 *  - diag[m] = Pbar_{m,m} / s^m, the diagonal's normalization product (m = 0..N).
 *  - off[m]  = sqrt(2m+3), the first off-diagonal factor (m = 0..N).
 *  - ab      = the recurrence pairs {A(k,m), B(k,m)}, column by column: column m
 *              holds k = m+2..N contiguously, so a walk reads one forward
 *              stream. (The triangle's layout puts consecutive k of one column
 *              k+1 entries apart, which costs a cache line per step at high
 *              degree.)
 *
 * Read-only after makeScalarTables returns, so one instance can serve any
 * number of threads. Size: about 8 N^2 bytes (38 MB at N = 2190).
 */
struct ScalarTables {
  int maxDegree{-1};        ///< Largest degree the tables cover (-1: empty).
  std::vector<double> diag; ///< Pbar_{m,m} / s^m.
  std::vector<double> off;  ///< sqrt(2m+3).
  std::vector<double> ab;   ///< {A, B} pairs, column-major.
};

/**
 * @brief Build the tables for every degree and order up to maxDegree.
 *
 * @param maxDegree Largest degree to cover; negative yields empty tables.
 * @return Tables ready for normalizedAssociatedLegendre(n, m, x, tables).
 * @note NOT RT-safe: allocates; throws std::bad_alloc if allocation fails.
 */
[[nodiscard]] ScalarTables makeScalarTables(int maxDegree);

/**
 * @brief Normalized associated Legendre function Pbar_n^m(x) from precomputed tables.
 *
 * The same column walk as normalizedAssociatedLegendre(n, m, x), reading its
 * coefficients from @p tables: the step is two multiplies and a subtract, and
 * the diagonal is one table lookup times s^m (a multiply loop below
 * SCALAR_POW_SEED_MIN_ORDER, one std::pow at or above it). Agrees with the
 * stateless form to rounding, and is bit-identical to it on the diagonal below
 * that order.
 *
 * @param n      Degree (0 <= n <= tables.maxDegree).
 * @param m      Order (0 <= m <= n).
 * @param x      Argument (clamped to [-1, 1]).
 * @param tables Tables from makeScalarTables covering degree n.
 * @return Pbar_n^m(x); NaN for invalid n/m or n beyond tables.maxDegree.
 * @note RT-safe: O(n - m) walk after a seed of at most 31 multiplies or one
 *       std::pow; no allocation. The tables must exist and stay unchanged for
 *       the duration of the call.
 */
[[nodiscard]] double
normalizedAssociatedLegendre(int n, int m, double x,
                             const ScalarTables& tables) noexcept APEX_NONBLOCKING;

/* ----------------------------- Transform API ----------------------------- */

/**
 * @brief Unnormalized associated Legendre function P_n^m(fX(x)).
 *
 * Applies fX to x and evaluates at the result, clamped to [-1, 1]. Useful for
 * coordinate conversions (e.g., fX = cos for colatitude input). fX is not
 * called for invalid n/m.
 *
 * @param n   Degree (n >= 0).
 * @param m   Order (0 <= m <= n).
 * @param fX  Callable to transform x prior to evaluation.
 * @param x   Input to the transform function.
 * @return P_n^m(fX(x)), or NaN for invalid n/m.
 * @note No allocation inside; constructing the std::function at the call site
 *       may allocate for a callable larger than its small-object storage.
 */
double associatedLegendreFunction(int n, int m, std::function<double(double)> fX, double x);

/**
 * @brief Legendre polynomial P_n(fX(x)) == P_n^0(fX(x)).
 *
 * @param n   Degree (n >= 0).
 * @param fX  Callable to transform x prior to evaluation.
 * @param x   Input to the transform function.
 * @return P_n(fX(x)), or NaN for n < 0.
 * @note No allocation inside; see associatedLegendreFunction.
 */
double legendrePolynomialFunction(int n, std::function<double(double)> fX, double x);

/**
 * @brief Normalized associated Legendre function Pbar_n^m(fX(x)).
 *
 * Applies fX to x and evaluates at the result, clamped to [-1, 1].
 *
 * @param n   Degree (n >= 0).
 * @param m   Order (0 <= m <= n).
 * @param fX  Callable to transform x prior to evaluation.
 * @param x   Input to the transform function.
 * @return Pbar_n^m(fX(x)), or NaN for invalid n/m.
 * @note No allocation inside; see associatedLegendreFunction.
 */
double normalizedAssociatedLegendreFunction(int n, int m, std::function<double(double)> fX,
                                            double x);

} // namespace legendre
} // namespace math
} // namespace apex

#endif // APEX_UTILITIES_MATH_LEGENDRE_ASSOCIATED_LEGENDRE_HPP
