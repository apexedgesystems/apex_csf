/**
 * @file ArrayCuda.cu
 * @brief CUDA kernel implementations for batch linear algebra operations.
 */

#include "src/utilities/math/linalg/inc/ArrayCuda.cuh"

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
namespace linalg {
namespace cuda {

/* ----------------------------- Constants ---------------------------------- */

#if COMPAT_CUDA_AVAILABLE
constexpr int K_THREADS_PER_BLOCK = 256;
#endif

/* ----------------------------- 3x3 GEMM Kernel ---------------------------- */

#if COMPAT_CUDA_AVAILABLE

/**
 * @brief Kernel: batch 3x3 matrix multiplication.
 * C = A * B for each matrix in batch.
 */
template <typename T>
__global__ void gemm3x3BatchKernel(const T* SIM_RESTRICT as, const T* SIM_RESTRICT bs, int batch,
                                   T* SIM_RESTRICT cs) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = as + IDX * 9;
  const T* B = bs + IDX * 9;
  T* C = cs + IDX * 9;

  // Row-major 3x3 multiplication
  C[0] = A[0] * B[0] + A[1] * B[3] + A[2] * B[6];
  C[1] = A[0] * B[1] + A[1] * B[4] + A[2] * B[7];
  C[2] = A[0] * B[2] + A[1] * B[5] + A[2] * B[8];

  C[3] = A[3] * B[0] + A[4] * B[3] + A[5] * B[6];
  C[4] = A[3] * B[1] + A[4] * B[4] + A[5] * B[7];
  C[5] = A[3] * B[2] + A[4] * B[5] + A[5] * B[8];

  C[6] = A[6] * B[0] + A[7] * B[3] + A[8] * B[6];
  C[7] = A[6] * B[1] + A[7] * B[4] + A[8] * B[7];
  C[8] = A[6] * B[2] + A[7] * B[5] + A[8] * B[8];
}

/* ----------------------------- Transpose Kernel --------------------------- */

/**
 * @brief Kernel: batch 3x3 matrix transpose.
 */
template <typename T>
__global__ void transpose3x3BatchKernel(const T* SIM_RESTRICT as, int batch, T* SIM_RESTRICT outs) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = as + IDX * 9;
  T* OUT = outs + IDX * 9;

  OUT[0] = A[0];
  OUT[1] = A[3];
  OUT[2] = A[6];
  OUT[3] = A[1];
  OUT[4] = A[4];
  OUT[5] = A[7];
  OUT[6] = A[2];
  OUT[7] = A[5];
  OUT[8] = A[8];
}

/* ----------------------------- Inverse Kernel ----------------------------- */

/**
 * @brief Kernel: batch 3x3 matrix inverse using Cramer's rule.
 */
template <typename T>
__global__ void inverse3x3BatchKernel(const T* SIM_RESTRICT as, int batch, T* SIM_RESTRICT outs) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = as + IDX * 9;
  T* OUT = outs + IDX * 9;

  // Cofactors
  const T C00 = A[4] * A[8] - A[5] * A[7];
  const T C01 = -(A[3] * A[8] - A[5] * A[6]);
  const T C02 = A[3] * A[7] - A[4] * A[6];
  const T C10 = -(A[1] * A[8] - A[2] * A[7]);
  const T C11 = A[0] * A[8] - A[2] * A[6];
  const T C12 = -(A[0] * A[7] - A[1] * A[6]);
  const T C20 = A[1] * A[5] - A[2] * A[4];
  const T C21 = -(A[0] * A[5] - A[2] * A[3]);
  const T C22 = A[0] * A[4] - A[1] * A[3];

  // Determinant
  const T DET = A[0] * C00 + A[1] * C01 + A[2] * C02;
  const T INV_DET = T(1) / DET;

  // Transpose of cofactor matrix scaled by 1/det
  OUT[0] = C00 * INV_DET;
  OUT[1] = C10 * INV_DET;
  OUT[2] = C20 * INV_DET;
  OUT[3] = C01 * INV_DET;
  OUT[4] = C11 * INV_DET;
  OUT[5] = C21 * INV_DET;
  OUT[6] = C02 * INV_DET;
  OUT[7] = C12 * INV_DET;
  OUT[8] = C22 * INV_DET;
}

/* ----------------------------- Determinant Kernel ------------------------- */

/**
 * @brief Kernel: batch 3x3 determinant.
 */
template <typename T>
__global__ void determinant3x3BatchKernel(const T* SIM_RESTRICT as, int batch,
                                          T* SIM_RESTRICT dets) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = as + IDX * 9;

  // det = a(ei - fh) - b(di - fg) + c(dh - eg)
  dets[IDX] = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) +
              A[2] * (A[3] * A[7] - A[4] * A[6]);
}

/* ----------------------------- Matrix-Vector Kernel ----------------------- */

/**
 * @brief Kernel: batch 3x3 matrix-vector multiply.
 */
template <typename T>
__global__ void matvec3x3BatchKernel(const T* SIM_RESTRICT as, const T* SIM_RESTRICT xs, int batch,
                                     T* SIM_RESTRICT ys) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = as + IDX * 9;
  const T* X = xs + IDX * 3;
  T* Y = ys + IDX * 3;

  Y[0] = A[0] * X[0] + A[1] * X[1] + A[2] * X[2];
  Y[1] = A[3] * X[0] + A[4] * X[1] + A[5] * X[2];
  Y[2] = A[6] * X[0] + A[7] * X[1] + A[8] * X[2];
}

/* ----------------------------- Cross Product Kernel ----------------------- */

/**
 * @brief Kernel: batch 3D cross product.
 */
template <typename T>
__global__ void cross3BatchKernel(const T* SIM_RESTRICT as, const T* SIM_RESTRICT bs, int batch,
                                  T* SIM_RESTRICT cs) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = as + IDX * 3;
  const T* B = bs + IDX * 3;
  T* C = cs + IDX * 3;

  C[0] = A[1] * B[2] - A[2] * B[1];
  C[1] = A[2] * B[0] - A[0] * B[2];
  C[2] = A[0] * B[1] - A[1] * B[0];
}

/* ----------------------------- Dot Product Kernel ------------------------- */

/**
 * @brief Kernel: batch 3D dot product.
 */
template <typename T>
__global__ void dot3BatchKernel(const T* SIM_RESTRICT as, const T* SIM_RESTRICT bs, int batch,
                                T* SIM_RESTRICT outs) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = as + IDX * 3;
  const T* B = bs + IDX * 3;

  outs[IDX] = A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

/* ----------------------------- Normalize Kernel --------------------------- */

/**
 * @brief Kernel: batch 3D vector normalization.
 */
template <typename T> __global__ void normalize3BatchKernel(T* SIM_RESTRICT vs, int batch) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  T* V = vs + IDX * 3;
  const T NRM = sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);

  if (NRM > T(1e-12)) {
    const T INV = T(1) / NRM;
    V[0] *= INV;
    V[1] *= INV;
    V[2] *= INV;
  }
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- API Functions ------------------------------ */

bool gemm3x3BatchCuda(const double* dAs, const double* dBs, int batch, double* dCs,
                      void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dBs;
  (void)batch;
  (void)dCs;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dBs == nullptr || dCs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  gemm3x3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dBs, batch, dCs);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool gemm3x3BatchCudaF(const float* dAs, const float* dBs, int batch, float* dCs,
                       void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dBs;
  (void)batch;
  (void)dCs;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dBs == nullptr || dCs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  gemm3x3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dBs, batch, dCs);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool transpose3x3BatchCuda(const double* dAs, int batch, double* dOuts, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)batch;
  (void)dOuts;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dOuts == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  transpose3x3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, batch, dOuts);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool transpose3x3BatchCudaF(const float* dAs, int batch, float* dOuts, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)batch;
  (void)dOuts;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dOuts == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  transpose3x3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, batch, dOuts);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool inverse3x3BatchCuda(const double* dAs, int batch, double* dOuts, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)batch;
  (void)dOuts;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dOuts == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  inverse3x3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, batch, dOuts);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool inverse3x3BatchCudaF(const float* dAs, int batch, float* dOuts, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)batch;
  (void)dOuts;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dOuts == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  inverse3x3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, batch, dOuts);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool determinant3x3BatchCuda(const double* dAs, int batch, double* dDets, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)batch;
  (void)dDets;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dDets == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  determinant3x3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, batch, dDets);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool determinant3x3BatchCudaF(const float* dAs, int batch, float* dDets, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)batch;
  (void)dDets;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dDets == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  determinant3x3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, batch, dDets);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool matvec3x3BatchCuda(const double* dAs, const double* dXs, int batch, double* dYs,
                        void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dXs;
  (void)batch;
  (void)dYs;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dXs == nullptr || dYs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  matvec3x3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dXs, batch, dYs);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool matvec3x3BatchCudaF(const float* dAs, const float* dXs, int batch, float* dYs,
                         void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dXs;
  (void)batch;
  (void)dYs;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dXs == nullptr || dYs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  matvec3x3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dXs, batch, dYs);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool cross3BatchCuda(const double* dAs, const double* dBs, int batch, double* dCs,
                     void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dBs;
  (void)batch;
  (void)dCs;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dBs == nullptr || dCs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  cross3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dBs, batch, dCs);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool cross3BatchCudaF(const float* dAs, const float* dBs, int batch, float* dCs,
                      void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dBs;
  (void)batch;
  (void)dCs;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dBs == nullptr || dCs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  cross3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dBs, batch, dCs);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool dot3BatchCuda(const double* dAs, const double* dBs, int batch, double* dOuts,
                   void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dBs;
  (void)batch;
  (void)dOuts;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dBs == nullptr || dOuts == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  dot3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dBs, batch, dOuts);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool dot3BatchCudaF(const float* dAs, const float* dBs, int batch, float* dOuts,
                    void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dAs;
  (void)dBs;
  (void)batch;
  (void)dOuts;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dAs == nullptr || dBs == nullptr || dOuts == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  dot3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dAs, dBs, batch, dOuts);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool normalize3BatchCuda(double* dVs, int batch, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dVs;
  (void)batch;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dVs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  normalize3BatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dVs, batch);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool normalize3BatchCudaF(float* dVs, int batch, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dVs;
  (void)batch;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dVs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  normalize3BatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dVs, batch);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

} // namespace cuda
} // namespace linalg
} // namespace math
} // namespace apex
