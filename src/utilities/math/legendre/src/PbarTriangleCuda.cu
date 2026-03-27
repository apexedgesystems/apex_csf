/**
 * @file PbarTriangleCuda.cu
 * @brief GPU kernel implementation for batched fully normalized Legendre triangle computation.
 */

#include "src/utilities/math/legendre/inc/PbarTriangleCuda.cuh"

#include <algorithm>
#include <cmath>

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- File Helpers ----------------------------- */

SIM_HD SIM_FI double clampPm1(double x) noexcept {
  if (SIM_UNLIKELY(x > 1.0))
    return 1.0;
  if (SIM_UNLIKELY(x < -1.0))
    return -1.0;
  return x;
}

/* ----------------------------- CUDA Kernel ----------------------------- */

// Batched kernel: one block per x (batch index = blockIdx.x), threads stride columns m.
// Seeds diagonal & first off-diagonal, then upward recurrence with a register window.
// Properly handles the +/-1 endpoint analytically (fills all m=0, zeros m>0).
// Output layout: batch-major; triangle b occupies out[b*triSize ... (b+1)*triSize-1].
// If dA/dB are non-null, uses precomputed coefficients; else computes on the fly.

__global__ void
pbarTriangleBatchSingleBlockKernel(int n, const double* SIM_RESTRICT xs, double* SIM_RESTRICT out,
                                   std::size_t triSize,
                                   const double* SIM_RESTRICT dA, // optional, length=triSize
                                   const double* SIM_RESTRICT dB) // optional, length=triSize
{
  if (n < 0 || xs == nullptr || out == nullptr)
    return;

  const int B = blockIdx.x; // batch index
  const int TID = threadIdx.x;
  const int TBL = blockDim.x;

  const double X = clampPm1(xs[B]);
  const double X2 = X * X;
  const double C = (X2 < 1.0) ? ::sqrt(1.0 - X2) : 0.0;

  double* SIM_RESTRICT dst = out + static_cast<std::size_t>(B) * triSize;

  // Base
  if (TID == 0) {
    dst[pbarTriangleIndexCuda(0, 0)] = 1.0;
  }
  __syncthreads();
  if (n == 0)
    return;

  // Endpoint fast path: x == +/-1 -> only m=0 survives; others are 0
  if (SIM_UNLIKELY(X == 1.0 || X == -1.0)) {
    // Fill m=0 analytically for all n2>=1; zero m>0
    for (int n2 = TID + 1; n2 <= n; n2 += TBL) {
      const double PARITY = (X < 0.0 && (n2 & 1)) ? -1.0 : 1.0; // (-1)^n at x=-1
      dst[pbarTriangleIndexCuda(n2, 0)] = ::sqrt(2.0 * n2 + 1.0) * PARITY;
    }
    __syncthreads();
    for (int m = TID + 1; m <= n; m += TBL) {
      for (int n2 = m; n2 <= n; ++n2) {
        dst[pbarTriangleIndexCuda(n2, m)] = 0.0;
      }
    }
    return;
  }

  // Seeds:
  // 1) Thread 0 computes diagonal (sequential dependency).
  if (TID == 0) {
    // n = 1
    dst[pbarTriangleIndexCuda(1, 1)] = ::sqrt(3.0) * C * dst[pbarTriangleIndexCuda(0, 0)];
    dst[pbarTriangleIndexCuda(1, 0)] = ::sqrt(3.0) * X * dst[pbarTriangleIndexCuda(0, 0)];

    // Diagonal m = 2..n (depends on m-1)
    for (int m = 2; m <= n; ++m) {
      const double K = ::sqrt((2.0 * m + 1.0) / (2.0 * m));
      dst[pbarTriangleIndexCuda(m, m)] = K * C * dst[pbarTriangleIndexCuda(m - 1, m - 1)];
    }
  }
  __syncthreads();

  // 2) First off-diagonal can be parallelized across threads once diagonal is ready.
  for (int m = TID; m <= n - 1; m += TBL) {
    const double K = ::sqrt(2.0 * m + 3.0);
    dst[pbarTriangleIndexCuda(m + 1, m)] = K * X * dst[pbarTriangleIndexCuda(m, m)];
  }
  __syncthreads();

  // Column-wise upward recurrence with register window
  const bool USE_COEFF = (dA != nullptr && dB != nullptr);

  for (int m = TID; m <= n; m += TBL) {
    if (m + 1 > n)
      continue;                                         // column too short to need recurrence
    double prev2 = dst[pbarTriangleIndexCuda(m, m)];    // Pbar_{m,m}
    double prev = dst[pbarTriangleIndexCuda(m + 1, m)]; // Pbar_{m+1,m}
    for (int n2 = m + 2; n2 <= n; ++n2) {
      double a, bc;
      if (USE_COEFF) {
        const std::size_t IDX = pbarTriangleIndexCuda(n2, m);
        a = dA[IDX];
        bc = dB[IDX];
      } else {
        const double NM = static_cast<double>(n2 - m);
        const double NPM = static_cast<double>(n2 + m);
        a = ::sqrt(((2.0 * n2 + 1.0) * (2.0 * n2 - 1.0)) / (NM * NPM));
        bc = ::sqrt(((2.0 * n2 + 1.0) * (NPM - 1.0) * (NM - 1.0)) / ((2.0 * n2 - 3.0) * NM * NPM));
      }
      const double CURR = ::fma(a * X, prev, -bc * prev2);
      dst[pbarTriangleIndexCuda(n2, m)] = CURR; // write-only
      prev2 = prev;
      prev = CURR;
    }
  }
}

/* ----------------------------- API ----------------------------- */

bool computeNormalizedPbarTriangleBatchCuda(int n, const double* dXs, int batch, double* dOut,
                                            std::size_t outLen, void* stream, const double* dA,
                                            const double* dB) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)n;
  (void)dXs;
  (void)batch;
  (void)dOut;
  (void)outLen;
  (void)stream;
  (void)dA;
  (void)dB;
  return false;
#else
  if (n < 0 || dXs == nullptr || dOut == nullptr || batch <= 0)
    return false;

  const std::size_t TRI = pbarTriangleSizeCuda(n);
  const std::size_t NEED = TRI * static_cast<std::size_t>(batch);
  if (outLen < NEED)
    return false;

  // If coefficients are provided, they must come as a pair (both non-null).
  if ((dA && !dB) || (!dA && dB))
    return false;

  auto s = static_cast<cudaStream_t>(stream);

  // One block per x; threads cover m. 256 is enough for n <= 255 (current ranges).
  const int THREADS = std::min(n + 1, 256);
  pbarTriangleBatchSingleBlockKernel<<<batch, THREADS, 0, s>>>(n, dXs, dOut, TRI, dA, dB);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

// RT-safe single-x variant: uses pre-allocated device input pointer.
bool computeNormalizedPbarTriangleCudaPrealloc(int n, const double* dX, double* dOut,
                                               std::size_t outLen, void* stream, const double* dA,
                                               const double* dB) noexcept {
  // Simply delegate to batch API with batch=1
  return computeNormalizedPbarTriangleBatchCuda(n, dX, /*batch=*/1, dOut, outLen, stream, dA, dB);
}

// Keep single-x API for callers, implemented via the batched kernel (batch=1).
bool computeNormalizedPbarTriangleCuda(int n, double x, double* dOut, std::size_t outLen,
                                       void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)n;
  (void)x;
  (void)dOut;
  (void)outLen;
  (void)stream;
  return false;
#else
  if (n < 0 || dOut == nullptr)
    return false;
  const std::size_t TRI = pbarTriangleSizeCuda(n);
  if (outLen < TRI)
    return false;

  // Allocate a tiny device buffer for the single x
  double* dX = nullptr;
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dX, sizeof(double))))
    return false;

  const double X = x; // host value; device will clamp
  bool ok =
      ::apex::compat::cuda::isSuccess(cudaMemcpyAsync(
          dX, &X, sizeof(double), cudaMemcpyHostToDevice, static_cast<cudaStream_t>(stream))) &&
      computeNormalizedPbarTriangleBatchCuda(n, dX, /*batch=*/1, dOut, TRI, stream);

  (void)cudaFree(dX);
  return ok;
#endif
}

/* ----------------------------- Derivative Kernels ----------------------------- */

#if COMPAT_CUDA_AVAILABLE

// Kernel to fill beta coefficients: beta(n,m) = sqrt((n-m)(n+m+1)), beta(n,n) = 0
__global__ void fillBetaCoefficientsKernel(int n, double* SIM_RESTRICT betaOut) {
  const int GRID_IDX = blockIdx.x * blockDim.x + threadIdx.x;
  const int GRID_SPAN = gridDim.x * blockDim.x;

  for (int m = GRID_IDX; m <= n; m += GRID_SPAN) {
    for (int n2 = m; n2 <= n; ++n2) {
      const std::size_t IDX = pbarTriangleIndexCuda(n2, m);
      if (m < n2) {
        const double NM = static_cast<double>(n2 - m);
        const double NPM1 = static_cast<double>(n2 + m + 1);
        betaOut[IDX] = ::sqrt(NM * NPM1);
      } else {
        betaOut[IDX] = 0.0; // beta(n,n) = 0
      }
    }
  }
}

// Batched kernel with derivatives: computes P and dP/dphi together.
// One block per sample, threads stride columns m.
// Derivative formula: dPbar_{n,m}/dphi = m * tan(phi) * Pbar_{n,m} - beta(n,m) * Pbar_{n,m+1}
__global__ void pbarTriangleWithDerivativesBatchKernel(
    int n, const double* SIM_RESTRICT sinPhis, const double* SIM_RESTRICT cosPhis,
    double* SIM_RESTRICT pOut, double* SIM_RESTRICT dpOut, std::size_t triSize,
    const double* SIM_RESTRICT dA,    // optional recurrence A coefficients
    const double* SIM_RESTRICT dB,    // optional recurrence B coefficients
    const double* SIM_RESTRICT dBeta) // optional beta coefficients
{
  if (n < 0 || sinPhis == nullptr || cosPhis == nullptr || pOut == nullptr || dpOut == nullptr)
    return;

  const int B = blockIdx.x; // batch index
  const int TID = threadIdx.x;
  const int TBL = blockDim.x;

  const double SPHI = clampPm1(sinPhis[B]);
  double cphi = cosPhis[B];
  constexpr double COS_MIN = 1e-12;
  if (::fabs(cphi) < COS_MIN) {
    cphi = (cphi >= 0.0) ? COS_MIN : -COS_MIN;
  }
  const double TAN_PHI = SPHI / cphi;
  const double C = ::sqrt(1.0 - SPHI * SPHI); // cos from sin for recurrence (more stable)

  double* SIM_RESTRICT pDst = pOut + static_cast<std::size_t>(B) * triSize;
  double* SIM_RESTRICT dpDst = dpOut + static_cast<std::size_t>(B) * triSize;

  // Base case: P_{0,0} = 1, dP_{0,0}/dphi = 0
  if (TID == 0) {
    pDst[pbarTriangleIndexCuda(0, 0)] = 1.0;
    dpDst[pbarTriangleIndexCuda(0, 0)] = 0.0;
  }
  __syncthreads();
  if (n == 0)
    return;

  // Endpoint fast path: sin(phi) == +/-1 -> cos(phi) == 0
  if (SIM_UNLIKELY(SPHI == 1.0 || SPHI == -1.0)) {
    // m=0: P values are sqrt(2n+1) * (+/-1)^n; derivatives are 0 at poles
    for (int n2 = TID + 1; n2 <= n; n2 += TBL) {
      const double PARITY = (SPHI < 0.0 && (n2 & 1)) ? -1.0 : 1.0;
      pDst[pbarTriangleIndexCuda(n2, 0)] = ::sqrt(2.0 * n2 + 1.0) * PARITY;
      dpDst[pbarTriangleIndexCuda(n2, 0)] = 0.0; // derivative at poles is 0 for m=0
    }
    __syncthreads();
    // m>0: P = 0, dP = 0
    for (int m = TID + 1; m <= n; m += TBL) {
      for (int n2 = m; n2 <= n; ++n2) {
        pDst[pbarTriangleIndexCuda(n2, m)] = 0.0;
        dpDst[pbarTriangleIndexCuda(n2, m)] = 0.0;
      }
    }
    return;
  }

  // ---- Step 1: Compute P values (same as P-only kernel) ----

  // Thread 0 computes diagonal (sequential dependency)
  if (TID == 0) {
    pDst[pbarTriangleIndexCuda(1, 1)] = ::sqrt(3.0) * C * pDst[pbarTriangleIndexCuda(0, 0)];
    pDst[pbarTriangleIndexCuda(1, 0)] = ::sqrt(3.0) * SPHI * pDst[pbarTriangleIndexCuda(0, 0)];

    for (int m = 2; m <= n; ++m) {
      const double K = ::sqrt((2.0 * m + 1.0) / (2.0 * m));
      pDst[pbarTriangleIndexCuda(m, m)] = K * C * pDst[pbarTriangleIndexCuda(m - 1, m - 1)];
    }
  }
  __syncthreads();

  // First off-diagonal in parallel
  for (int m = TID; m <= n - 1; m += TBL) {
    const double K = ::sqrt(2.0 * m + 3.0);
    pDst[pbarTriangleIndexCuda(m + 1, m)] = K * SPHI * pDst[pbarTriangleIndexCuda(m, m)];
  }
  __syncthreads();

  // Column-wise upward recurrence for P
  const bool USE_AB = (dA != nullptr && dB != nullptr);

  for (int m = TID; m <= n; m += TBL) {
    if (m + 1 > n)
      continue;
    double prev2 = pDst[pbarTriangleIndexCuda(m, m)];
    double prev = pDst[pbarTriangleIndexCuda(m + 1, m)];
    for (int n2 = m + 2; n2 <= n; ++n2) {
      double a, bc;
      if (USE_AB) {
        const std::size_t IDX = pbarTriangleIndexCuda(n2, m);
        a = dA[IDX];
        bc = dB[IDX];
      } else {
        const double NM = static_cast<double>(n2 - m);
        const double NPM = static_cast<double>(n2 + m);
        a = ::sqrt(((2.0 * n2 + 1.0) * (2.0 * n2 - 1.0)) / (NM * NPM));
        bc = ::sqrt(((2.0 * n2 + 1.0) * (NPM - 1.0) * (NM - 1.0)) / ((2.0 * n2 - 3.0) * NM * NPM));
      }
      const double CURR = ::fma(a * SPHI, prev, -bc * prev2);
      pDst[pbarTriangleIndexCuda(n2, m)] = CURR;
      prev2 = prev;
      prev = CURR;
    }
  }
  __syncthreads();

  // ---- Step 2: Compute dP/dphi from P ----
  // dPbar_{n,m}/dphi = m * tan(phi) * Pbar_{n,m} - beta(n,m) * Pbar_{n,m+1}

  const bool USE_BETA = (dBeta != nullptr);

  for (int n2 = TID; n2 <= n; n2 += TBL) {
    for (int m = 0; m <= n2; ++m) {
      const std::size_t IDX = pbarTriangleIndexCuda(n2, m);
      const double PNM = pDst[IDX];

      double dP = static_cast<double>(m) * TAN_PHI * PNM;

      if (m < n2) {
        double beta;
        if (USE_BETA) {
          beta = dBeta[IDX];
        } else {
          const double NM = static_cast<double>(n2 - m);
          const double NPM1 = static_cast<double>(n2 + m + 1);
          beta = ::sqrt(NM * NPM1);
        }
        const double PNM1 = pDst[pbarTriangleIndexCuda(n2, m + 1)];
        dP -= beta * PNM1;
      }

      dpDst[IDX] = dP;
    }
  }
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- Derivative API ----------------------------- */

bool computeBetaCoefficientsCuda(int n, double* dBeta, std::size_t outLen, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)n;
  (void)dBeta;
  (void)outLen;
  (void)stream;
  return false;
#else
  if (n < 0 || dBeta == nullptr)
    return false;

  const std::size_t TRI = pbarTriangleSizeCuda(n);
  if (outLen < TRI)
    return false;

  auto s = static_cast<cudaStream_t>(stream);

  constexpr int THREADS = 256;
  const int NEED_BLOCKS = (n + 1 + THREADS - 1) / THREADS;
  const int BLOCKS = std::min(std::max(1, NEED_BLOCKS), 64);

  fillBetaCoefficientsKernel<<<BLOCKS, THREADS, 0, s>>>(n, dBeta);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool computeNormalizedPbarTriangleWithDerivativesBatchCuda(int n, const double* dSinPhis,
                                                           const double* dCosPhis, int batch,
                                                           double* dPOut, double* dDpOut,
                                                           std::size_t outLen, void* stream,
                                                           const double* dA, const double* dB,
                                                           const double* dBeta) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)n;
  (void)dSinPhis;
  (void)dCosPhis;
  (void)batch;
  (void)dPOut;
  (void)dDpOut;
  (void)outLen;
  (void)stream;
  (void)dA;
  (void)dB;
  (void)dBeta;
  return false;
#else
  if (n < 0 || dSinPhis == nullptr || dCosPhis == nullptr || batch <= 0)
    return false;
  if (dPOut == nullptr || dDpOut == nullptr)
    return false;

  const std::size_t TRI = pbarTriangleSizeCuda(n);
  const std::size_t NEED = TRI * static_cast<std::size_t>(batch);
  if (outLen < NEED)
    return false;

  // A/B must come as a pair if provided
  if ((dA && !dB) || (!dA && dB))
    return false;

  auto s = static_cast<cudaStream_t>(stream);

  const int THREADS = std::min(n + 1, 256);
  pbarTriangleWithDerivativesBatchKernel<<<batch, THREADS, 0, s>>>(n, dSinPhis, dCosPhis, dPOut,
                                                                   dDpOut, TRI, dA, dB, dBeta);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

// RT-safe single-sample derivative variant: uses pre-allocated device input pointers.
bool computeNormalizedPbarTriangleWithDerivativesCudaPrealloc(int n, const double* dSinPhi,
                                                              const double* dCosPhi, double* dPOut,
                                                              double* dDpOut, std::size_t outLen,
                                                              void* stream, const double* dA,
                                                              const double* dB,
                                                              const double* dBeta) noexcept {
  // Simply delegate to batch API with batch=1
  return computeNormalizedPbarTriangleWithDerivativesBatchCuda(
      n, dSinPhi, dCosPhi, /*batch=*/1, dPOut, dDpOut, outLen, stream, dA, dB, dBeta);
}

bool computeNormalizedPbarTriangleWithDerivativesCuda(int n, double sinPhi, double cosPhi,
                                                      double* dPOut, double* dDpOut,
                                                      std::size_t outLen, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)n;
  (void)sinPhi;
  (void)cosPhi;
  (void)dPOut;
  (void)dDpOut;
  (void)outLen;
  (void)stream;
  return false;
#else
  if (n < 0 || dPOut == nullptr || dDpOut == nullptr)
    return false;

  const std::size_t TRI = pbarTriangleSizeCuda(n);
  if (outLen < TRI)
    return false;

  // Allocate tiny device buffers for the single sample
  double* dSin = nullptr;
  double* dCos = nullptr;
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dSin, sizeof(double))))
    return false;
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dCos, sizeof(double)))) {
    (void)cudaFree(dSin);
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);

  bool ok = ::apex::compat::cuda::isSuccess(
                cudaMemcpyAsync(dSin, &sinPhi, sizeof(double), cudaMemcpyHostToDevice, s)) &&
            ::apex::compat::cuda::isSuccess(
                cudaMemcpyAsync(dCos, &cosPhi, sizeof(double), cudaMemcpyHostToDevice, s)) &&
            computeNormalizedPbarTriangleWithDerivativesBatchCuda(n, dSin, dCos, /*batch=*/1, dPOut,
                                                                  dDpOut, TRI, stream);

  (void)cudaFree(dSin);
  (void)cudaFree(dCos);
  return ok;
#endif
}

} // namespace legendre
} // namespace math
} // namespace apex
