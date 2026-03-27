#ifndef APEX_UTILITIES_MATH_LEGENDRE_ASSOCIATED_LEGENDRE_HPP
#define APEX_UTILITIES_MATH_LEGENDRE_ASSOCIATED_LEGENDRE_HPP
/**
 * @file AssociatedLegendre.hpp
 * @brief Scalar associated Legendre polynomial evaluation (CPU).
 *
 * Provides:
 *  - Unnormalized associated Legendre function P_n^m(x)
 *  - Legendre polynomial P_n(x) = P_n^0(x)
 *  - Fully normalized (EGM/IERS convention) associated Legendre function
 *
 * Two API styles:
 *  - Simple: Direct evaluation at x (recommended for most uses)
 *  - Transform: Apply a callable fX(x) before evaluation (advanced use)
 *
 * Design goals:
 *  - Simple API for common use cases
 *  - Deterministic edge-case behavior (|x| > 1 clamped, m > n returns NaN)
 */

#include <functional>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- Simple API ----------------------------- */

/**
 * @brief Unnormalized associated Legendre function P_n^m(x).
 *
 * Valid for integer degree n >= 0 and order 0 <= m <= n.
 * Returns NaN for invalid inputs (n < 0, m < 0, m > n).
 * Input x is clamped to [-1, 1].
 *
 * @param n Degree (n >= 0).
 * @param m Order (0 <= m <= n).
 * @param x Argument (will be clamped to [-1, 1]).
 * @return Value of P_n^m(x), or NaN for invalid n/m.
 * @note NOT RT-safe: Allocates factorial table internally.
 */
double associatedLegendre(int n, int m, double x);

/**
 * @brief Legendre polynomial P_n(x) == P_n^0(x).
 *
 * @param n Degree (n >= 0).
 * @param x Argument (will be clamped to [-1, 1]).
 * @return Value of P_n(x), or NaN for n < 0.
 * @note NOT RT-safe: Allocates factorial table internally.
 */
double legendrePolynomial(int n, double x);

/**
 * @brief Normalized associated Legendre function (EGM/IERS orthonormal convention).
 *
 * Applies orthonormal scaling to P_n^m(x):
 *   Pbar_n^m(x) = sqrt( (2 - delta_{0m}) * (2n+1) * (n-m)! / (n+m)! ) * P_n^m(x)
 *
 * Valid for 0 <= m <= n.
 *
 * @param n Degree (n >= 0).
 * @param m Order (0 <= m <= n).
 * @param x Argument (will be clamped to [-1, 1]).
 * @return Normalized Pbar_n^m(x), or NaN for invalid n/m.
 * @note NOT RT-safe: Allocates factorial table internally.
 */
double normalizedAssociatedLegendre(int n, int m, double x);

/* ----------------------------- Transform API ----------------------------- */

/**
 * @brief Unnormalized associated Legendre function P_n^m(fX(x)).
 *
 * Advanced variant that applies a transform fX to x before evaluation.
 * Useful for coordinate conversions (e.g., fX = cos for colatitude input).
 *
 * @param n   Degree (n >= 0).
 * @param m   Order (0 <= m <= n).
 * @param fX  Callable to transform x prior to evaluation.
 * @param x   Input to the transform function.
 * @return Value of P_n^m(fX(x)), or NaN for invalid n/m.
 * @note NOT RT-safe: Allocates factorial table internally.
 */
double associatedLegendreFunction(int n, int m, std::function<double(double)> fX, double x);

/**
 * @brief Legendre polynomial P_n(fX(x)) == P_n^0(fX(x)).
 *
 * @param n   Degree (n >= 0).
 * @param fX  Callable to transform x prior to evaluation.
 * @param x   Input to the transform function.
 * @return Value of P_n(fX(x)), or NaN for n < 0.
 * @note NOT RT-safe: Allocates factorial table internally.
 */
double legendrePolynomialFunction(int n, std::function<double(double)> fX, double x);

/**
 * @brief Normalized associated Legendre function Pbar_n^m(fX(x)).
 *
 * Advanced variant that applies a transform fX to x before evaluation.
 *
 * @param n   Degree (n >= 0).
 * @param m   Order (0 <= m <= n).
 * @param fX  Callable to transform x prior to evaluation.
 * @param x   Input to the transform function.
 * @return Normalized Pbar_n^m(fX(x)), or NaN for invalid n/m.
 * @note NOT RT-safe: Allocates factorial table internally.
 */
double normalizedAssociatedLegendreFunction(int n, int m, std::function<double(double)> fX,
                                            double x);

} // namespace legendre
} // namespace math
} // namespace apex

#endif // APEX_UTILITIES_MATH_LEGENDRE_ASSOCIATED_LEGENDRE_HPP
