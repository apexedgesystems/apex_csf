#ifndef APEX_UTILITIES_MATH_LEGENDRE_PBAR_TRIANGLE_CUDA_CUH
#define APEX_UTILITIES_MATH_LEGENDRE_PBAR_TRIANGLE_CUDA_CUH
/**
 * @file PbarTriangleCuda.cuh
 * @brief GPU kernel API for fully normalized Legendre triangle Pbar_{n,m}(x).
 *
 * Provides batched GPU computation for high-throughput workloads.
 * All device-output APIs are asynchronous w.r.t. the provided stream.
 *
 * Design goals:
 *  - Header hygiene: avoids CUDA includes; stream passed as opaque void*
 *  - Batch-major output layout for efficient GPU memory access
 *  - Optional precomputed recurrence coefficients for hot-loop optimization
 *  - Combined P and dP/dphi computation for gravity/magnetics applications
 */

#include <cmath>
#include <cstddef>

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_detect.hpp"

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- CUDA Triangle Utilities ----------------------------- */

/**
 * @brief Number of coefficients in triangular storage for degrees 0..N.
 *
 * Device-compatible version: (N+1)(N+2)/2.
 *
 * @note RT-safe: Inline, no allocations, host/device callable.
 */
SIM_HD_FI std::size_t pbarTriangleSizeCuda(int n) noexcept {
  return (n < 0) ? 0u : static_cast<std::size_t>((n + 1) * (n + 2) / 2);
}

/**
 * @brief Triangular storage index for (n,m).
 *
 * Device-compatible version: idx = n(n+1)/2 + m.
 * Precondition: 0 <= m <= n.
 *
 * @note RT-safe: Inline, no allocations, host/device callable.
 */
SIM_HD_FI std::size_t pbarTriangleIndexCuda(int n, int m) noexcept {
  return static_cast<std::size_t>(n) * static_cast<std::size_t>(n + 1) / 2 +
         static_cast<std::size_t>(m);
}

/**
 * @brief Total number of outputs for a batch of size @p batch.
 *
 * Layout is batch-major: triangle 0, then triangle 1, ..., each of size pbarTriangleSizeCuda(n).
 *
 * @note RT-safe: Inline, no allocations, host/device callable.
 */
SIM_HD_FI std::size_t pbarTriangleBatchSizeCuda(int n, int batch) noexcept {
  return pbarTriangleSizeCuda(n) * static_cast<std::size_t>((batch > 0) ? batch : 0);
}

/**
 * @brief Compute beta coefficient for derivative recurrence.
 *
 * Device-compatible version: beta(n,m) = sqrt((n-m)(n+m+1)).
 * Returns 0 for m >= n or invalid inputs.
 *
 * @note RT-safe: Inline, no allocations, host/device callable.
 */
SIM_HD_FI double betaCoefficientCuda(int n, int m) noexcept {
  if (m >= n || m < 0 || n < 0) {
    return 0.0;
  }
  // Note: sqrt is available on device via ::sqrt or std::sqrt depending on context
  const double nm = static_cast<double>(n - m);
  const double npm1 = static_cast<double>(n + m + 1);
  // Use a simple multiply; sqrt will be resolved at compile time for device/host
#if defined(__CUDA_ARCH__)
  return ::sqrt(nm * npm1);
#else
  return std::sqrt(nm * npm1);
#endif
}

/* ----------------------------- Pbar API ----------------------------- */

/**
 * @brief Compute fully normalized Pbar_{n,m}(x) triangle on GPU into device buffer.
 *
 * - Normalization: "bar" (fully normalized), consistent with EGM2008/IERS.
 * - Argument: x = sin(phi) in [-1, 1] (will be clamped on device).
 * - Storage: triangular, index idx(n,m) = n(n+1)/2 + m.
 * - Async: kernel launch is asynchronous w.r.t. @p stream.
 *
 * @param n       Maximum degree (N >= 0).
 * @param x       Input in [-1,1].
 * @param dOut    Device pointer to double buffer of length >= pbarTriangleSizeCuda(N).
 * @param outLen  Number of doubles available at @p dOut.
 * @param stream  (Optional) CUDA stream as opaque pointer (cast to cudaStream_t in .cu).
 * @return true on successful launch; false on invalid args or launch error.
 * @note NOT RT-safe: Performs temporary device allocation internally.
 */
SIM_NODISCARD bool computeNormalizedPbarTriangleCuda(int n, double x, double* dOut,
                                                     std::size_t outLen,
                                                     void* stream = nullptr) noexcept;

/**
 * @brief Compute fully normalized triangles for a batch of x's into device buffer.
 *
 * - Inputs: dXs[batch] with values in [-1,1] (each will be clamped on device).
 * - Output layout: batch-major; triangle b occupies
 *   out[b * pbarTriangleSizeCuda(n) ... (b+1)*size - 1] in triangular layout.
 * - Async: launch is asynchronous w.r.t. @p stream.
 * - Optional precomputed recurrence coefficients:
 *     * dA, dB (both length == pbarTriangleSizeCuda(n), triangular layout) for the
 *       upward recurrence Pbar_{n,m} = A(n,m) * x * Pbar_{n-1,m} - B(n,m) * Pbar_{n-2,m}.
 *     * If either is nullptr, the kernel computes coefficients on the fly.
 *
 * @param n        Maximum degree (N >= 0).
 * @param dXs      Device pointer to double array of length @p batch with x values.
 * @param batch    Number of x samples (batch > 0).
 * @param dOut     Device pointer to output buffer of length >= pbarTriangleBatchSizeCuda(n, batch).
 * @param outLen   Number of doubles available at @p dOut.
 * @param stream   (Optional) CUDA stream as opaque pointer.
 * @param dA       (Optional) Device pointer to precomputed A coefficients.
 * @param dB       (Optional) Device pointer to precomputed B coefficients.
 * @return true on successful launch; false on invalid args or launch error.
 * @note RT-safe if using pre-allocated device buffers.
 */
SIM_NODISCARD bool computeNormalizedPbarTriangleBatchCuda(int n, const double* dXs, int batch,
                                                          double* dOut, std::size_t outLen,
                                                          void* stream = nullptr,
                                                          const double* dA = nullptr,
                                                          const double* dB = nullptr) noexcept;

/**
 * @brief RT-safe single-x variant using pre-allocated device input.
 *
 * Unlike computeNormalizedPbarTriangleCuda(), this version takes a device pointer
 * to the input value, avoiding internal allocations.
 *
 * @param n      Maximum degree (N >= 0).
 * @param dX     Device pointer to single x value in [-1, 1].
 * @param dOut   Device pointer to output buffer (length >= pbarTriangleSizeCuda(n)).
 * @param outLen Number of doubles available at @p dOut.
 * @param stream (Optional) CUDA stream as opaque pointer.
 * @param dA     (Optional) Device pointer to precomputed A coefficients.
 * @param dB     (Optional) Device pointer to precomputed B coefficients.
 * @return true on successful launch; false on invalid args or launch error.
 * @note RT-safe: No internal allocations.
 */
SIM_NODISCARD bool computeNormalizedPbarTriangleCudaPrealloc(int n, const double* dX, double* dOut,
                                                             std::size_t outLen,
                                                             void* stream = nullptr,
                                                             const double* dA = nullptr,
                                                             const double* dB = nullptr) noexcept;

/* ----------------------------- Pbar + Derivative API ----------------------------- */

/**
 * @brief Compute Pbar triangle and phi-derivatives for a batch of samples on GPU.
 *
 * Computes both Pbar_{n,m}(sin(phi)) and dPbar_{n,m}/dphi for each sample.
 * The derivative formula:
 *   dPbar_{n,m}/dphi = m * tan(phi) * Pbar_{n,m} - beta(n,m) * Pbar_{n,m+1}
 *
 * @param n          Maximum degree (N >= 0).
 * @param dSinPhis   Device pointer to sin(phi) values (length = batch).
 * @param dCosPhis   Device pointer to cos(phi) values (length = batch).
 * @param batch      Number of samples (batch > 0).
 * @param dPOut      Device pointer to Pbar output (length >= triSize * batch).
 * @param dDpOut     Device pointer to dPbar/dphi output (length >= triSize * batch).
 * @param outLen     Number of doubles available at each output buffer.
 * @param stream     (Optional) CUDA stream as opaque pointer.
 * @param dA         (Optional) Device pointer to precomputed A coefficients.
 * @param dB         (Optional) Device pointer to precomputed B coefficients.
 * @param dBeta      (Optional) Device pointer to precomputed beta coefficients.
 * @return true on successful launch; false on invalid args or launch error.
 * @note RT-safe if using pre-allocated device buffers.
 */
SIM_NODISCARD bool computeNormalizedPbarTriangleWithDerivativesBatchCuda(
    int n, const double* dSinPhis, const double* dCosPhis, int batch, double* dPOut, double* dDpOut,
    std::size_t outLen, void* stream = nullptr, const double* dA = nullptr,
    const double* dB = nullptr, const double* dBeta = nullptr) noexcept;

/**
 * @brief Compute Pbar triangle and phi-derivatives for a single sample on GPU.
 *
 * Convenience wrapper that handles device allocation for the single input.
 *
 * @param n        Maximum degree (N >= 0).
 * @param sinPhi   sin(phi) value in [-1, 1].
 * @param cosPhi   cos(phi) value.
 * @param dPOut    Device pointer to Pbar output (length >= pbarTriangleSizeCuda(n)).
 * @param dDpOut   Device pointer to dPbar/dphi output (same length).
 * @param outLen   Number of doubles available at each output buffer.
 * @param stream   (Optional) CUDA stream as opaque pointer.
 * @return true on successful launch; false on invalid args or launch error.
 * @note NOT RT-safe: Performs temporary device allocation internally.
 */
SIM_NODISCARD bool
computeNormalizedPbarTriangleWithDerivativesCuda(int n, double sinPhi, double cosPhi, double* dPOut,
                                                 double* dDpOut, std::size_t outLen,
                                                 void* stream = nullptr) noexcept;

/**
 * @brief RT-safe single-sample derivative variant using pre-allocated device inputs.
 *
 * Unlike computeNormalizedPbarTriangleWithDerivativesCuda(), this version takes device
 * pointers to the input values, avoiding internal allocations.
 *
 * @param n         Maximum degree (N >= 0).
 * @param dSinPhi   Device pointer to single sin(phi) value.
 * @param dCosPhi   Device pointer to single cos(phi) value.
 * @param dPOut     Device pointer to Pbar output (length >= pbarTriangleSizeCuda(n)).
 * @param dDpOut    Device pointer to dPbar/dphi output (same length).
 * @param outLen    Number of doubles available at each output buffer.
 * @param stream    (Optional) CUDA stream as opaque pointer.
 * @param dA        (Optional) Device pointer to precomputed A coefficients.
 * @param dB        (Optional) Device pointer to precomputed B coefficients.
 * @param dBeta     (Optional) Device pointer to precomputed beta coefficients.
 * @return true on successful launch; false on invalid args or launch error.
 * @note RT-safe: No internal allocations.
 */
SIM_NODISCARD bool computeNormalizedPbarTriangleWithDerivativesCudaPrealloc(
    int n, const double* dSinPhi, const double* dCosPhi, double* dPOut, double* dDpOut,
    std::size_t outLen, void* stream = nullptr, const double* dA = nullptr,
    const double* dB = nullptr, const double* dBeta = nullptr) noexcept;

/**
 * @brief Precompute beta coefficients into device buffer.
 *
 * Fills dBeta[idx(n,m)] = sqrt((n-m)(n+m+1)) for all 0 <= m <= n <= N.
 * beta(n,n) = 0 for all n (boundary condition).
 *
 * @param n       Maximum degree N.
 * @param dBeta   Device pointer to output buffer (length >= pbarTriangleSizeCuda(n)).
 * @param outLen  Number of doubles available at dBeta.
 * @param stream  (Optional) CUDA stream as opaque pointer.
 * @return true on successful launch; false on invalid args or launch error.
 * @note RT-safe if buffer is pre-allocated.
 */
SIM_NODISCARD bool computeBetaCoefficientsCuda(int n, double* dBeta, std::size_t outLen,
                                               void* stream = nullptr) noexcept;

} // namespace legendre
} // namespace math
} // namespace apex

#endif // APEX_UTILITIES_MATH_LEGENDRE_PBAR_TRIANGLE_CUDA_CUH
