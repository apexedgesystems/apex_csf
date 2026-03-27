#ifndef APEX_UTILITIES_MATH_LEGENDRE_PBAR_DERIVATIVES_HPP
#define APEX_UTILITIES_MATH_LEGENDRE_PBAR_DERIVATIVES_HPP
/**
 * @file PbarDerivatives.hpp
 * @brief Derivatives of fully normalized associated Legendre functions.
 *
 * Provides efficient computation of Pbar_{n,m}(sin phi) and its derivative
 * dPbar_{n,m}/dphi for spherical harmonic applications (gravity, magnetics).
 *
 * The derivative formula used:
 *   dPbar_{n,m}/dphi = m * tan(phi) * Pbar_{n,m} - beta(n,m) * Pbar_{n,m+1}
 *   where beta(n,m) = sqrt((n-m)(n+m+1))
 *
 * Design goals:
 *  - Zero-allocation APIs for real-time use
 *  - Combined computation for efficiency (single pass fills both P and dP)
 *  - Separate beta coefficient API for advanced users
 */

#include <cmath>
#include <cstddef>
#include <vector>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- Triangle Utilities ----------------------------- */

// Re-export from PbarTriangle.hpp for convenience
std::size_t pbarTriangleSize(int n) noexcept;
std::size_t pbarTriangleIndex(int n, int m) noexcept;

/* ----------------------------- Beta Coefficients ----------------------------- */

/**
 * @brief Compute recurrence coefficient beta(n,m) = sqrt((n-m)(n+m+1)).
 *
 * Used in the derivative formula:
 *   dPbar_{n,m}/dphi = m * tan(phi) * Pbar_{n,m} - beta(n,m) * Pbar_{n,m+1}
 *
 * @param n Degree.
 * @param m Order (0 <= m < n for non-zero result).
 * @return beta(n,m), or 0.0 if m >= n.
 * @note RT-safe: Inline, no allocations.
 */
[[nodiscard]] inline double betaCoefficient(int n, int m) noexcept {
  if (m >= n || m < 0 || n < 0) {
    return 0.0;
  }
  return std::sqrt(static_cast<double>((n - m) * (n + m + 1)));
}

/**
 * @brief Precompute beta coefficients into triangular storage.
 *
 * Fills out[idx(n,m)] = beta(n,m) for all 0 <= m <= n <= N.
 * Note: beta(n,n) = 0 for all n (boundary condition).
 *
 * @param n       Maximum degree N.
 * @param out     Output buffer (triangular layout, length >= pbarTriangleSize(N)).
 * @param outLen  Length of output buffer.
 * @return true on success, false if buffer too small or N < 0.
 * @note RT-safe: Zero-allocation, uses caller buffer.
 */
bool computeBetaCoefficients(int n, double* out, std::size_t outLen) noexcept;

/**
 * @brief Convenience wrapper returning beta coefficients as vector.
 *
 * @param n Maximum degree N.
 * @return Vector of beta coefficients in triangular storage layout.
 * @note NOT RT-safe: Returns std::vector (allocates).
 */
[[nodiscard]] std::vector<double> computeBetaCoefficientsVector(int n) noexcept;

/* ----------------------------- Combined P and dP/dphi ----------------------------- */

/**
 * @brief Compute Pbar triangle and its phi-derivative in a single pass.
 *
 * Given x = sin(phi), computes:
 *   pOut[idx(n,m)] = Pbar_{n,m}(x)
 *   dpOut[idx(n,m)] = dPbar_{n,m}/dphi
 *
 * The derivative formula:
 *   dPbar_{n,m}/dphi = m * tan(phi) * Pbar_{n,m} - beta(n,m) * Pbar_{n,m+1}
 *
 * Note: Requires both sin(phi) and cos(phi) as inputs to avoid recomputing
 * cos from sin (and to handle the phi = +/-pi/2 edge case properly).
 *
 * @param n       Maximum degree N.
 * @param sinPhi  sin(phi) where phi is geocentric latitude.
 * @param cosPhi  cos(phi) (must be consistent with sinPhi).
 * @param pOut    Output buffer for Pbar values (triangular, length >= pbarTriangleSize(N)).
 * @param dpOut   Output buffer for dPbar/dphi values (triangular, same length).
 * @param outLen  Length of each output buffer.
 * @return true on success, false if buffers too small or N < 0.
 * @note RT-safe: Zero-allocation, uses caller buffers.
 */
bool computeNormalizedPbarTriangleWithDerivatives(int n, double sinPhi, double cosPhi, double* pOut,
                                                  double* dpOut, std::size_t outLen) noexcept;

/**
 * @brief Result structure for vector-returning derivative computation.
 */
struct PbarWithDerivatives {
  std::vector<double> P;  ///< Pbar_{n,m}(sin phi) in triangular layout
  std::vector<double> dP; ///< dPbar_{n,m}/dphi in triangular layout
};

/**
 * @brief Convenience wrapper returning Pbar and dPbar/dphi as vectors.
 *
 * @param n       Maximum degree N.
 * @param sinPhi  sin(phi).
 * @param cosPhi  cos(phi).
 * @return Structure containing P and dP vectors.
 * @note NOT RT-safe: Returns std::vectors (allocates).
 */
[[nodiscard]] PbarWithDerivatives
computeNormalizedPbarTriangleWithDerivativesVector(int n, double sinPhi, double cosPhi) noexcept;

/**
 * @brief Compute Pbar triangle and derivative using precomputed beta coefficients.
 *
 * More efficient when beta coefficients are cached across multiple calls.
 *
 * @param n       Maximum degree N.
 * @param sinPhi  sin(phi).
 * @param cosPhi  cos(phi).
 * @param beta    Precomputed beta coefficients (triangular, length >= pbarTriangleSize(N)).
 * @param pOut    Output buffer for Pbar values.
 * @param dpOut   Output buffer for dPbar/dphi values.
 * @param outLen  Length of each output buffer.
 * @return true on success, false if buffers too small or N < 0.
 * @note RT-safe: Zero-allocation, uses caller buffers.
 */
bool computeNormalizedPbarTriangleWithDerivativesCached(int n, double sinPhi, double cosPhi,
                                                        const double* beta, double* pOut,
                                                        double* dpOut, std::size_t outLen) noexcept;

} // namespace legendre
} // namespace math
} // namespace apex

#endif // APEX_UTILITIES_MATH_LEGENDRE_PBAR_DERIVATIVES_HPP
