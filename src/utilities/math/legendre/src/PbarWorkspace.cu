/**
 * @file PbarWorkspace.cu
 * @brief GPU workspace management and coefficient precomputation for Legendre triangles.
 */

#include "src/utilities/math/legendre/inc/PbarWorkspace.hpp"

// Keep C/C++ headers minimal to avoid macro pollution that can upset <cstdio> re-exports.
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

#if COMPAT_CUDA_AVAILABLE

/* ----------------------------- CUDA Kernels ----------------------------- */

/**
 * Fill A(n,m), B(n,m) for n>=m+2 (else 0). Threads stride columns m.
 * A = sqrt(((2n+1)(2n-1))/((n-m)(n+m)))
 * B = sqrt(((2n+1)(n+m-1)(n-m-1))/((2n-3)(n-m)(n+m)))
 */
__global__ void fillPbarCoeffKernel(int n, double* __restrict__ aOut, double* __restrict__ bOut) {
  const int GRID_IDX = blockIdx.x * blockDim.x + threadIdx.x;
  const int GRID_SPAN = gridDim.x * blockDim.x;

  // Zero diagonal & first off-diagonal (not used by recurrence)
  for (int m = GRID_IDX; m <= n; m += GRID_SPAN) {
    const std::size_t DIAG_IDX = pbarTriangleIndexCuda(m, m);
    aOut[DIAG_IDX] = 0.0;
    bOut[DIAG_IDX] = 0.0;
    if (m + 1 <= n) {
      const std::size_t OFF_IDX = pbarTriangleIndexCuda(m + 1, m);
      aOut[OFF_IDX] = 0.0;
      bOut[OFF_IDX] = 0.0;
    }
  }

  // Upward region: n2 = m+2..n
  for (int m = GRID_IDX; m <= n; m += GRID_SPAN) {
    for (int n2 = m + 2; n2 <= n; ++n2) {
      const double NM = static_cast<double>(n2 - m);
      const double NPM = static_cast<double>(n2 + m);
      const double COEF_A = ::sqrt(((2.0 * n2 + 1.0) * (2.0 * n2 - 1.0)) / (NM * NPM));
      const double COEF_B =
          ::sqrt(((2.0 * n2 + 1.0) * (NPM - 1.0) * (NM - 1.0)) / ((2.0 * n2 - 3.0) * NM * NPM));
      const std::size_t IDX = pbarTriangleIndexCuda(n2, m);
      aOut[IDX] = COEF_A;
      bOut[IDX] = COEF_B;
    }
  }
}
#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- API ----------------------------- */

bool createPbarWorkspace(PbarWorkspace& ws, int n, int batch, bool pinnedHost,
                         void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)ws;
  (void)n;
  (void)batch;
  (void)pinnedHost;
  (void)stream;
  return false;
#else
  if (n < 0 || batch <= 0)
    return false;

  // Compute sizes
  const std::size_t TRI = pbarTriangleSizeCuda(n);
  const std::size_t NEED = TRI * static_cast<std::size_t>(batch);

  // Allocate device buffers
  double* dXs = nullptr;
  double* dOut = nullptr;
  if (!::apex::compat::cuda::isSuccess(
          cudaMalloc(&dXs, static_cast<std::size_t>(batch) * sizeof(double)))) {
    return false;
  }
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dOut, NEED * sizeof(double)))) {
    (void)cudaFree(dXs);
    return false;
  }

  // Optional pinned host buffers
  double* hXs = nullptr;
  double* hOut = nullptr;
  if (pinnedHost) {
    if (!::apex::compat::cuda::isSuccess(cudaMallocHost(
            reinterpret_cast<void**>(&hXs), static_cast<std::size_t>(batch) * sizeof(double)))) {
      (void)cudaFree(dXs);
      (void)cudaFree(dOut);
      return false;
    }
    if (!::apex::compat::cuda::isSuccess(
            cudaMallocHost(reinterpret_cast<void**>(&hOut), NEED * sizeof(double)))) {
      (void)cudaFreeHost(hXs);
      (void)cudaFree(dXs);
      (void)cudaFree(dOut);
      return false;
    }
  }

  // Replace ws (destroy old first)
  destroyPbarWorkspace(ws);
  ws.n = n;
  ws.batch = batch;
  ws.triSize = TRI;
  ws.outLen = NEED;
  ws.pinnedHost = pinnedHost;
  ws.stream = stream;

  ws.dXs = dXs;
  ws.dOut = dOut;

  // Coefficient buffers are not allocated yet; mark as not ready.
  ws.dA = nullptr;
  ws.dB = nullptr;
  ws.coeffReady = false;

  ws.hXs = hXs;
  ws.hOut = hOut;
  return true;
#endif
}

void destroyPbarWorkspace(PbarWorkspace& ws) noexcept {
#if COMPAT_CUDA_AVAILABLE
  // Host pinned
  if (ws.hDpOut) {
    (void)cudaFreeHost(ws.hDpOut);
    ws.hDpOut = nullptr;
  }
  if (ws.hCosPhis) {
    (void)cudaFreeHost(ws.hCosPhis);
    ws.hCosPhis = nullptr;
  }
  if (ws.hOut) {
    (void)cudaFreeHost(ws.hOut);
    ws.hOut = nullptr;
  }
  if (ws.hXs) {
    (void)cudaFreeHost(ws.hXs);
    ws.hXs = nullptr;
  }

  // Device outputs/inputs
  if (ws.dDpOut) {
    (void)cudaFree(ws.dDpOut);
    ws.dDpOut = nullptr;
  }
  if (ws.dCosPhis) {
    (void)cudaFree(ws.dCosPhis);
    ws.dCosPhis = nullptr;
  }
  if (ws.dOut) {
    (void)cudaFree(ws.dOut);
    ws.dOut = nullptr;
  }
  if (ws.dXs) {
    (void)cudaFree(ws.dXs);
    ws.dXs = nullptr;
  }

  // Device precomputed coefficients
  if (ws.dBeta) {
    (void)cudaFree(ws.dBeta);
    ws.dBeta = nullptr;
  }
  if (ws.dA) {
    (void)cudaFree(ws.dA);
    ws.dA = nullptr;
  }
  if (ws.dB) {
    (void)cudaFree(ws.dB);
    ws.dB = nullptr;
  }
#else
  (void)ws;
#endif
  // Reset all fields.
  ws = PbarWorkspace{};
}

bool ensurePbarCoefficients(PbarWorkspace& ws) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)ws;
  return false;
#else
  if (ws.n < 0 || ws.triSize == 0)
    return false;

  // Already prepared?
  if (ws.coeffReady && ws.dA != nullptr && ws.dB != nullptr)
    return true;

  // Allocate A/B
  double* dA = nullptr;
  double* dB = nullptr;
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dA, ws.triSize * sizeof(double)))) {
    return false;
  }
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dB, ws.triSize * sizeof(double)))) {
    (void)cudaFree(dA);
    return false;
  }

  // Launch fill kernel
  auto s = static_cast<cudaStream_t>(ws.stream);
  constexpr int THREADS = 256;
  // Columns are m = 0..n; cover with a few blocks but cap to 64
  const int NEED_BLOCKS = (ws.n + 1 + THREADS - 1) / THREADS;
  const int BLOCKS = std::min(std::max(1, NEED_BLOCKS), 64);
  fillPbarCoeffKernel<<<BLOCKS, THREADS, 0, s>>>(ws.n, dA, dB);
  if (!::apex::compat::cuda::isSuccess(cudaGetLastError())) {
    (void)cudaFree(dA);
    (void)cudaFree(dB);
    return false;
  }

  // Install in workspace
  ws.dA = dA;
  ws.dB = dB;
  ws.coeffReady = true;
  return true;
#endif
}

void freePbarCoefficients(PbarWorkspace& ws) noexcept {
#if COMPAT_CUDA_AVAILABLE
  if (ws.dA) {
    (void)cudaFree(ws.dA);
    ws.dA = nullptr;
  }
  if (ws.dB) {
    (void)cudaFree(ws.dB);
    ws.dB = nullptr;
  }
#else
  (void)ws;
#endif
  ws.coeffReady = false;
}

bool createPbarWorkspaceWithDerivatives(PbarWorkspace& ws, int n, int batch, bool pinnedHost,
                                        void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)ws;
  (void)n;
  (void)batch;
  (void)pinnedHost;
  (void)stream;
  return false;
#else
  if (n < 0 || batch <= 0)
    return false;

  // Compute sizes
  const std::size_t TRI = pbarTriangleSizeCuda(n);
  const std::size_t NEED = TRI * static_cast<std::size_t>(batch);
  const std::size_t BATCH_BYTES = static_cast<std::size_t>(batch) * sizeof(double);

  // Allocate device buffers for P computation
  double* dXs = nullptr;
  double* dOut = nullptr;
  double* dCosPhis = nullptr;
  double* dDpOut = nullptr;

  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dXs, BATCH_BYTES)))
    return false;
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dOut, NEED * sizeof(double)))) {
    (void)cudaFree(dXs);
    return false;
  }
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dCosPhis, BATCH_BYTES))) {
    (void)cudaFree(dXs);
    (void)cudaFree(dOut);
    return false;
  }
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dDpOut, NEED * sizeof(double)))) {
    (void)cudaFree(dXs);
    (void)cudaFree(dOut);
    (void)cudaFree(dCosPhis);
    return false;
  }

  // Optional pinned host buffers
  double* hXs = nullptr;
  double* hOut = nullptr;
  double* hCosPhis = nullptr;
  double* hDpOut = nullptr;

  if (pinnedHost) {
    if (!::apex::compat::cuda::isSuccess(
            cudaMallocHost(reinterpret_cast<void**>(&hXs), BATCH_BYTES))) {
      (void)cudaFree(dXs);
      (void)cudaFree(dOut);
      (void)cudaFree(dCosPhis);
      (void)cudaFree(dDpOut);
      return false;
    }
    if (!::apex::compat::cuda::isSuccess(
            cudaMallocHost(reinterpret_cast<void**>(&hOut), NEED * sizeof(double)))) {
      (void)cudaFreeHost(hXs);
      (void)cudaFree(dXs);
      (void)cudaFree(dOut);
      (void)cudaFree(dCosPhis);
      (void)cudaFree(dDpOut);
      return false;
    }
    if (!::apex::compat::cuda::isSuccess(
            cudaMallocHost(reinterpret_cast<void**>(&hCosPhis), BATCH_BYTES))) {
      (void)cudaFreeHost(hXs);
      (void)cudaFreeHost(hOut);
      (void)cudaFree(dXs);
      (void)cudaFree(dOut);
      (void)cudaFree(dCosPhis);
      (void)cudaFree(dDpOut);
      return false;
    }
    if (!::apex::compat::cuda::isSuccess(
            cudaMallocHost(reinterpret_cast<void**>(&hDpOut), NEED * sizeof(double)))) {
      (void)cudaFreeHost(hXs);
      (void)cudaFreeHost(hOut);
      (void)cudaFreeHost(hCosPhis);
      (void)cudaFree(dXs);
      (void)cudaFree(dOut);
      (void)cudaFree(dCosPhis);
      (void)cudaFree(dDpOut);
      return false;
    }
  }

  // Replace ws (destroy old first)
  destroyPbarWorkspace(ws);
  ws.n = n;
  ws.batch = batch;
  ws.triSize = TRI;
  ws.outLen = NEED;
  ws.pinnedHost = pinnedHost;
  ws.stream = stream;
  ws.derivativesEnabled = true;

  ws.dXs = dXs;
  ws.dOut = dOut;
  ws.dCosPhis = dCosPhis;
  ws.dDpOut = dDpOut;

  ws.dA = nullptr;
  ws.dB = nullptr;
  ws.coeffReady = false;
  ws.dBeta = nullptr;
  ws.betaReady = false;

  ws.hXs = hXs;
  ws.hOut = hOut;
  ws.hCosPhis = hCosPhis;
  ws.hDpOut = hDpOut;

  return true;
#endif
}

bool ensureBetaCoefficients(PbarWorkspace& ws) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)ws;
  return false;
#else
  if (ws.n < 0 || ws.triSize == 0)
    return false;

  // Already prepared?
  if (ws.betaReady && ws.dBeta != nullptr)
    return true;

  // Allocate beta buffer
  double* dBeta = nullptr;
  if (!::apex::compat::cuda::isSuccess(cudaMalloc(&dBeta, ws.triSize * sizeof(double)))) {
    return false;
  }

  // Fill using existing kernel from PbarTriangleCuda.cu
  if (!computeBetaCoefficientsCuda(ws.n, dBeta, ws.triSize, ws.stream)) {
    (void)cudaFree(dBeta);
    return false;
  }

  // Install in workspace
  ws.dBeta = dBeta;
  ws.betaReady = true;
  return true;
#endif
}

void freeBetaCoefficients(PbarWorkspace& ws) noexcept {
#if COMPAT_CUDA_AVAILABLE
  if (ws.dBeta) {
    (void)cudaFree(ws.dBeta);
    ws.dBeta = nullptr;
  }
#else
  (void)ws;
#endif
  ws.betaReady = false;
}

bool enqueueCompute(PbarWorkspace& ws, const double* hXs, double* hOut, bool copyBack) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)ws;
  (void)hXs;
  (void)hOut;
  (void)copyBack;
  return false;
#else
  if (ws.n < 0 || ws.batch <= 0 || ws.dXs == nullptr || ws.dOut == nullptr)
    return false;

  auto s = static_cast<cudaStream_t>(ws.stream);

  // Choose source of H2D xs
  const double* srcXs = hXs ? hXs : ws.hXs;
  if (!srcXs)
    return false; // must have host xs somewhere

  if (!::apex::compat::cuda::isSuccess(
          cudaMemcpyAsync(ws.dXs, srcXs, static_cast<std::size_t>(ws.batch) * sizeof(double),
                          cudaMemcpyHostToDevice, s))) {
    return false;
  }

  // If coefficients are ready, thread them into the kernel call.
  const double* dA = (ws.coeffReady ? ws.dA : nullptr);
  const double* dB = (ws.coeffReady ? ws.dB : nullptr);

  if (!computeNormalizedPbarTriangleBatchCuda(ws.n, ws.dXs, ws.batch, ws.dOut, ws.outLen, ws.stream,
                                              dA, dB)) {
    return false;
  }

  if (copyBack) {
    // Choose destination for D2H results
    double* dstOut = hOut ? hOut : ws.hOut;
    if (!dstOut)
      return false; // must have a host destination if copyBack requested

    if (!::apex::compat::cuda::isSuccess(cudaMemcpyAsync(
            dstOut, ws.dOut, ws.outLen * sizeof(double), cudaMemcpyDeviceToHost, s))) {
      return false;
    }
  }

  return true; // enqueue success (not synchronized)
#endif
}

bool enqueueComputeWithDerivatives(PbarWorkspace& ws, const double* hSinPhis,
                                   const double* hCosPhis, double* hPOut, double* hDpOut,
                                   bool copyBack) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)ws;
  (void)hSinPhis;
  (void)hCosPhis;
  (void)hPOut;
  (void)hDpOut;
  (void)copyBack;
  return false;
#else
  if (!ws.derivativesEnabled)
    return false;
  if (ws.n < 0 || ws.batch <= 0)
    return false;
  if (ws.dXs == nullptr || ws.dOut == nullptr || ws.dCosPhis == nullptr || ws.dDpOut == nullptr)
    return false;

  auto s = static_cast<cudaStream_t>(ws.stream);
  const std::size_t BATCH_BYTES = static_cast<std::size_t>(ws.batch) * sizeof(double);

  // Choose source of H2D sin(phi)
  const double* srcSin = hSinPhis ? hSinPhis : ws.hXs;
  if (!srcSin)
    return false;

  // Choose source of H2D cos(phi)
  const double* srcCos = hCosPhis ? hCosPhis : ws.hCosPhis;
  if (!srcCos)
    return false;

  // Copy inputs to device
  if (!::apex::compat::cuda::isSuccess(
          cudaMemcpyAsync(ws.dXs, srcSin, BATCH_BYTES, cudaMemcpyHostToDevice, s))) {
    return false;
  }
  if (!::apex::compat::cuda::isSuccess(
          cudaMemcpyAsync(ws.dCosPhis, srcCos, BATCH_BYTES, cudaMemcpyHostToDevice, s))) {
    return false;
  }

  // Get optional precomputed coefficients
  const double* dA = ws.coeffReady ? ws.dA : nullptr;
  const double* dB = ws.coeffReady ? ws.dB : nullptr;
  const double* dBeta = ws.betaReady ? ws.dBeta : nullptr;

  // Run the derivative kernel
  if (!computeNormalizedPbarTriangleWithDerivativesBatchCuda(ws.n, ws.dXs, ws.dCosPhis, ws.batch,
                                                             ws.dOut, ws.dDpOut, ws.outLen,
                                                             ws.stream, dA, dB, dBeta)) {
    return false;
  }

  if (copyBack) {
    // Choose destinations for D2H results
    double* dstP = hPOut ? hPOut : ws.hOut;
    double* dstDp = hDpOut ? hDpOut : ws.hDpOut;
    if (!dstP || !dstDp)
      return false;

    if (!::apex::compat::cuda::isSuccess(cudaMemcpyAsync(dstP, ws.dOut, ws.outLen * sizeof(double),
                                                         cudaMemcpyDeviceToHost, s))) {
      return false;
    }
    if (!::apex::compat::cuda::isSuccess(cudaMemcpyAsync(
            dstDp, ws.dDpOut, ws.outLen * sizeof(double), cudaMemcpyDeviceToHost, s))) {
      return false;
    }
  }

  return true;
#endif
}

bool synchronize(const PbarWorkspace& ws) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)ws;
  return false;
#else
  auto s = static_cast<cudaStream_t>(ws.stream);
  return ::apex::compat::cuda::isSuccess(cudaStreamSynchronize(s));
#endif
}

} // namespace legendre
} // namespace math
} // namespace apex
