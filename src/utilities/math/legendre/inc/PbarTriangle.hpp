#ifndef APEX_UTILITIES_MATH_LEGENDRE_PBAR_TRIANGLE_HPP
#define APEX_UTILITIES_MATH_LEGENDRE_PBAR_TRIANGLE_HPP
/**
 * @file PbarTriangle.hpp
 * @brief CPU computation of fully normalized Legendre triangle Pbar_{n,m}(x).
 *
 * Provides efficient triangular storage computation for all degrees 0..N and orders 0..n.
 * Uses upward recurrence for numerical stability.
 *
 * Design goals:
 *  - Zero-allocation API via caller-provided buffer
 *  - Predictable triangular storage layout: idx(n,m) = n(n+1)/2 + m
 *  - EGM2008/IERS compatible normalization
 */

#include <cstddef>
#include <vector>

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- Triangle Utilities ----------------------------- */

/**
 * @brief Number of coefficients in triangular storage for degrees 0..N.
 *
 * count = (N+1)(N+2)/2
 *
 * @param n Maximum degree N.
 * @return Number of elements in triangular storage.
 * @note RT-safe: Inline, no allocations.
 */
[[nodiscard]] inline std::size_t pbarTriangleSize(int n) noexcept {
  return (n < 0) ? 0u : static_cast<std::size_t>((n + 1) * (n + 2) / 2);
}

/**
 * @brief Triangular storage index for (n,m).
 *
 * idx = n(n+1)/2 + m
 * Precondition: 0 <= m <= n.
 *
 * @param n Degree.
 * @param m Order.
 * @return Linear index into triangular storage.
 * @note RT-safe: Inline, no allocations.
 */
[[nodiscard]] inline std::size_t pbarTriangleIndex(int n, int m) noexcept {
  return static_cast<std::size_t>(n) * static_cast<std::size_t>(n + 1) / 2 +
         static_cast<std::size_t>(m);
}

/* ----------------------------- Recurrence Coefficients ----------------------------- */

/**
 * @brief Precompute recurrence coefficients A and B for upward recurrence.
 *
 * The coefficients are used in:
 *   Pbar_{n,m} = A(n,m) * x * Pbar_{n-1,m} - B(n,m) * Pbar_{n-2,m}
 *
 * where:
 *   A(n,m) = sqrt(((2n+1)(2n-1)) / ((n-m)(n+m)))
 *   B(n,m) = sqrt(((2n+1)(n+m-1)(n-m-1)) / ((2n-3)(n-m)(n+m)))
 *
 * Storage is triangular: A[idx(n,m)] and B[idx(n,m)] for all 0 <= m <= n <= N.
 * Coefficients for n < m+2 are set to 0 (not used in recurrence).
 *
 * @param n      Maximum degree N.
 * @param aOut   Output buffer for A coefficients (triangular, length >= pbarTriangleSize(N)).
 * @param bOut   Output buffer for B coefficients (triangular, same length).
 * @param outLen Length of each output buffer.
 * @return true on success, false if buffer too small or N < 0.
 * @note RT-safe: Zero-allocation, uses caller buffers.
 */
bool computeRecurrenceCoefficients(int n, double* aOut, double* bOut, std::size_t outLen) noexcept;

/**
 * @brief Result structure for vector-returning coefficient computation.
 */
struct RecurrenceCoefficients {
  std::vector<double> A; ///< A coefficients in triangular layout
  std::vector<double> B; ///< B coefficients in triangular layout
};

/**
 * @brief Convenience wrapper returning recurrence coefficients as vectors.
 *
 * @param n Maximum degree N.
 * @return Structure containing A and B vectors.
 * @note NOT RT-safe: Returns std::vectors (allocates).
 */
[[nodiscard]] RecurrenceCoefficients computeRecurrenceCoefficientsVector(int n) noexcept;

/* ----------------------------- API ----------------------------- */

/**
 * @brief Compute the fully normalized Legendre triangle Pbar_{n,m}(x) into caller buffer.
 *
 * - Normalization: "bar" (fully normalized), consistent with EGM2008/IERS.
 * - Argument: x = sin(phi) (equivalently x = cos(theta) with theta = pi/2 - phi).
 * - Storage: triangular, index idx(n,m) = n(n+1)/2 + m.
 * - No dynamic allocation; writes into caller-provided buffer.
 *
 * @param n       Maximum degree.
 * @param x       Input in [-1,1] (will be clamped).
 * @param out     Pointer to output buffer of length >= pbarTriangleSize(N).
 * @param outLen  Length of @p out (used for bounds checking).
 * @return true if computed successfully; false if N<0 or buffer too small.
 * @note RT-safe: Zero-allocation, uses caller buffer.
 */
bool computeNormalizedPbarTriangle(int n, double x, double* out, std::size_t outLen) noexcept;

/**
 * @brief Compute triangle using precomputed recurrence coefficients.
 *
 * More efficient when A/B coefficients are cached across multiple calls.
 * This eliminates sqrt() calls from the hot loop.
 *
 * @param n       Maximum degree.
 * @param x       Input in [-1,1] (will be clamped).
 * @param A       Precomputed A coefficients (triangular, length >= pbarTriangleSize(N)).
 * @param B       Precomputed B coefficients (triangular, same length).
 * @param out     Pointer to output buffer of length >= pbarTriangleSize(N).
 * @param outLen  Length of @p out (used for bounds checking).
 * @return true if computed successfully; false if N<0 or buffer too small.
 * @note RT-safe: Zero-allocation, uses caller buffers.
 */
bool computeNormalizedPbarTriangleCached(int n, double x, const double* A, const double* B,
                                         double* out, std::size_t outLen) noexcept;

/**
 * @brief Compute triangle using interleaved recurrence coefficients.
 *
 * Optimized version where A and B coefficients are interleaved for better cache
 * utilization: AB[2*idx] = A, AB[2*idx+1] = B.
 *
 * @param n       Maximum degree.
 * @param x       Input in [-1,1] (will be clamped).
 * @param AB      Interleaved A/B coefficients (length >= 2 * pbarTriangleSize(N)).
 * @param out     Pointer to output buffer of length >= pbarTriangleSize(N).
 * @param outLen  Length of @p out (used for bounds checking).
 * @return true if computed successfully; false if N<0 or buffer too small.
 * @note RT-safe: Zero-allocation, uses caller buffers.
 */
bool computeNormalizedPbarTriangleCachedInterleaved(int n, double x, const double* AB, double* out,
                                                    std::size_t outLen) noexcept;

/**
 * @brief Compute interleaved recurrence coefficients.
 *
 * Stores coefficients as {A[0], B[0], A[1], B[1], ...} for cache efficiency.
 *
 * @param n      Maximum degree N.
 * @param abOut  Output buffer for interleaved A/B (length >= 2 * pbarTriangleSize(N)).
 * @param outLen Length of output buffer.
 * @return true on success, false if buffer too small or N < 0.
 * @note RT-safe: Zero-allocation, uses caller buffer.
 */
bool computeRecurrenceCoefficientsInterleaved(int n, double* abOut, std::size_t outLen) noexcept;

/**
 * @brief Convenience wrapper that returns the triangle in a vector.
 *
 * @param n Maximum degree.
 * @param x Input in [-1,1].
 * @return Vector containing triangle values in triangular storage layout.
 * @note NOT RT-safe: Returns std::vector (allocates).
 */
[[nodiscard]] std::vector<double> computeNormalizedPbarTriangleVector(int n, double x) noexcept;

} // namespace legendre
} // namespace math
} // namespace apex

#endif // APEX_UTILITIES_MATH_LEGENDRE_PBAR_TRIANGLE_HPP
