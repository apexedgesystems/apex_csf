/**
 * @file MnaSystemCuda.cu
 * @brief CUDA implementation of MNA solver using cuSOLVER.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystemCuda.cuh"

#if COMPAT_HAVE_CUSOLVER
#include <cuda_runtime.h>
#include <cusolverDn.h>
#endif

#include <cstring>

namespace sim::electronics::algorithms::mna::cuda {

/* ----------------------------- Workspace Management ----------------------------- */

bool MnaCudaWorkspace::prepare(std::size_t dim) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (initialized && dim <= maxDim) {
    return true; // Already prepared for this size
  }

  // Release any existing allocations
  release();

  // Create cuSOLVER handle
  cusolverDnHandle_t handle = nullptr;
  if (cusolverDnCreate(&handle) != CUSOLVER_STATUS_SUCCESS) {
    return false;
  }
  solverHandle = handle;

  // Allocate device memory
  std::size_t matrixBytes = dim * dim * sizeof(double);
  std::size_t vectorBytes = dim * sizeof(double);
  std::size_t pivotBytes = dim * sizeof(int);

  if (cudaMalloc(&dA, matrixBytes) != cudaSuccess) {
    release();
    return false;
  }
  if (cudaMalloc(&db, vectorBytes) != cudaSuccess) {
    release();
    return false;
  }
  if (cudaMalloc(&dInfo, sizeof(int)) != cudaSuccess) {
    release();
    return false;
  }
  if (cudaMalloc(reinterpret_cast<void**>(&dIpiv), pivotBytes) != cudaSuccess) {
    release();
    return false;
  }

  // Query cuSOLVER workspace size
  int lwork = 0;
  if (cusolverDnDgetrf_bufferSize(handle, static_cast<int>(dim), static_cast<int>(dim),
                                  static_cast<double*>(dA), static_cast<int>(dim),
                                  &lwork) != CUSOLVER_STATUS_SUCCESS) {
    release();
    return false;
  }

  workSize = static_cast<std::size_t>(lwork);
  if (cudaMalloc(&dWork, workSize * sizeof(double)) != cudaSuccess) {
    release();
    return false;
  }

  maxDim = dim;
  initialized = true;
  return true;
#else
  (void)dim;
  return false;
#endif
}

void MnaCudaWorkspace::release() noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (dA) {
    cudaFree(dA);
    dA = nullptr;
  }
  if (db) {
    cudaFree(db);
    db = nullptr;
  }
  if (dInfo) {
    cudaFree(dInfo);
    dInfo = nullptr;
  }
  if (dWork) {
    cudaFree(dWork);
    dWork = nullptr;
  }
  if (dIpiv) {
    cudaFree(dIpiv);
    dIpiv = nullptr;
  }
  if (solverHandle) {
    cusolverDnDestroy(static_cast<cusolverDnHandle_t>(solverHandle));
    solverHandle = nullptr;
  }
  maxDim = 0;
  workSize = 0;
  initialized = false;
#endif
}

/* ----------------------------- GPU Solve ----------------------------- */

bool solveCuda(MnaCudaWorkspace& ws, const double* A, double* b, std::size_t dim) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (!ws.canHandle(dim)) {
    return false;
  }

  cusolverDnHandle_t handle = static_cast<cusolverDnHandle_t>(ws.solverHandle);
  int n = static_cast<int>(dim);
  std::size_t matrixBytes = dim * dim * sizeof(double);
  std::size_t vectorBytes = dim * sizeof(double);

  // Copy matrix and RHS to device
  if (cudaMemcpy(ws.dA, A, matrixBytes, cudaMemcpyHostToDevice) != cudaSuccess) {
    return false;
  }
  if (cudaMemcpy(ws.db, b, vectorBytes, cudaMemcpyHostToDevice) != cudaSuccess) {
    return false;
  }

  // LU factorization
  if (cusolverDnDgetrf(handle, n, n, static_cast<double*>(ws.dA), n, static_cast<double*>(ws.dWork),
                       ws.dIpiv, static_cast<int*>(ws.dInfo)) != CUSOLVER_STATUS_SUCCESS) {
    return false;
  }

  // Check for singular matrix
  int info = 0;
  if (cudaMemcpy(&info, ws.dInfo, sizeof(int), cudaMemcpyDeviceToHost) != cudaSuccess) {
    return false;
  }
  if (info != 0) {
    return false; // Singular matrix
  }

  // Back-substitution
  // Use CUBLAS_OP_T because the caller provides row-major A, but cuSOLVER
  // interprets flat memory as column-major. Row-major A stored flat equals
  // column-major A^T, so dgetrf factorized A^T. Using CUBLAS_OP_T in dgetrs
  // solves (A^T)^T * x = A * x = b, which is the correct system.
  if (cusolverDnDgetrs(handle, CUBLAS_OP_T, n,
                       1, // One RHS
                       static_cast<const double*>(ws.dA), n, ws.dIpiv, static_cast<double*>(ws.db),
                       n, static_cast<int*>(ws.dInfo)) != CUSOLVER_STATUS_SUCCESS) {
    return false;
  }

  // Copy solution back to host
  if (cudaMemcpy(b, ws.db, vectorBytes, cudaMemcpyDeviceToHost) != cudaSuccess) {
    return false;
  }

  return true;
#else
  (void)ws;
  (void)A;
  (void)b;
  (void)dim;
  return false;
#endif
}

bool solveCudaDeviceResident(MnaCudaWorkspace& ws, double* dA, double* dB,
                             std::size_t dim) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (!ws.canHandle(dim) || dA == nullptr || dB == nullptr) {
    return false;
  }

  cusolverDnHandle_t handle = static_cast<cusolverDnHandle_t>(ws.solverHandle);
  int n = static_cast<int>(dim);

  // LU factorize dA in place.
  if (cusolverDnDgetrf(handle, n, n, dA, n, static_cast<double*>(ws.dWork), ws.dIpiv,
                       static_cast<int*>(ws.dInfo)) != CUSOLVER_STATUS_SUCCESS) {
    return false;
  }

  // Check for singular matrix.
  int info = 0;
  if (cudaMemcpy(&info, ws.dInfo, sizeof(int), cudaMemcpyDeviceToHost) != cudaSuccess) {
    return false;
  }
  if (info != 0) {
    return false;
  }

  // Back-substitute (CUBLAS_OP_T to match row-major convention).
  if (cusolverDnDgetrs(handle, CUBLAS_OP_T, n, 1, dA, n, ws.dIpiv, dB, n,
                       static_cast<int*>(ws.dInfo)) != CUSOLVER_STATUS_SUCCESS) {
    return false;
  }

  return true;
#else
  (void)ws;
  (void)dA;
  (void)dB;
  (void)dim;
  return false;
#endif
}

bool solveBatchCuda(MnaCudaWorkspace& ws, const double* As, double* bs, std::size_t dim,
                    std::size_t batchSize) noexcept {
#if COMPAT_HAVE_CUSOLVER
  // For now, solve each system sequentially on GPU
  // Future optimization: use cuSOLVER batch APIs or custom kernels
  std::size_t matrixSize = dim * dim;
  std::size_t vectorSize = dim;

  for (std::size_t i = 0; i < batchSize; ++i) {
    if (!solveCuda(ws, As + i * matrixSize, bs + i * vectorSize, dim)) {
      return false;
    }
  }
  return true;
#else
  (void)ws;
  (void)As;
  (void)bs;
  (void)dim;
  (void)batchSize;
  return false;
#endif
}

} // namespace sim::electronics::algorithms::mna::cuda
