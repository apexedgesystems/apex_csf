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
 * recurrence in n, seeded on the diagonal: O(n - m) work, no allocation, no
 * table. The normalized form walks the recurrence computeNormalizedPbarTriangle
 * applies to every column (forming one coefficient from an identity of the
 * other to save a square root per step), so a scalar value agrees with the
 * triangle entry at pbarTriangleIndex(n, m) to rounding.
 * Both forms omit the Condon-Shortley phase (geodesy convention).
 *
 * Range:
 *  - Normalized values are bounded by sqrt(2n+1) and stay accurate through
 *    n = 2190, the EGM2008 degree.
 *  - Unnormalized values grow like (2m-1)!! and become inf once the true value
 *    exceeds DBL_MAX: P_n^n(0) = (2n-1)!! does from n = 151. Use the
 *    normalized form at high degree.
 *  - Near |x| = 1 with large m, the diagonal seed (1 - x^2)^(m/2) falls into
 *    subnormals and then to zero (for example x = 0.999 from m near 240), and
 *    the column loses its digits with it. This is true of every forward-column
 *    method, std::sph_legendre included; the remedy is Holmes-Featherstone
 *    scaling, which this library does not apply.
 *
 * Two API styles:
 *  - Simple: evaluation at x, clamped to [-1, 1]. Real-time safe.
 *  - Transform: evaluation at fX(x), with the transformed value clamped to
 *    [-1, 1] (for example fX = cos with a colatitude in [0, pi]).
 *
 * Invalid orders (n < 0, m < 0, m > n) return NaN. A NaN argument returns
 * NaN for every function but the constant P_0^0 = 1.
 */

#include "src/utilities/compatibility/inc/compat_rt_attrs.hpp"

#include <functional>

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
 * @return P_n^m(x); inf where the true value exceeds DBL_MAX; NaN for invalid n/m.
 * @note RT-safe: O(n - m), no allocation.
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
 * @note RT-safe: O(n - m), no allocation.
 */
[[nodiscard]] double normalizedAssociatedLegendre(int n, int m, double x) noexcept APEX_NONBLOCKING;

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
