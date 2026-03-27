/**
 * @file QuaternionCuda.cu
 * @brief CUDA kernel implementations for batch quaternion operations.
 */

#include "src/utilities/math/quaternion/inc/QuaternionCuda.cuh"

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
namespace quaternion {
namespace cuda {

/* ----------------------------- Constants ---------------------------------- */

#if COMPAT_CUDA_AVAILABLE
constexpr int K_THREADS_PER_BLOCK = 256;
#endif

/* ----------------------------- Kernels ------------------------------------ */

#if COMPAT_CUDA_AVAILABLE

/**
 * @brief Kernel: rotate vectors by quaternions.
 *
 * Uses optimized formula: v' = v + 2*w*(q.xyz x v) + 2*(q.xyz x (q.xyz x v))
 */
template <typename T>
__global__ void rotateVectorBatchKernel(const T* SIM_RESTRICT qs, const T* SIM_RESTRICT vsIn,
                                        int batch, T* SIM_RESTRICT vsOut) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  // Load quaternion [w, x, y, z]
  const T* Q = qs + IDX * 4;
  const T QW = Q[0], QX = Q[1], QY = Q[2], QZ = Q[3];

  // Load input vector
  const T* V = vsIn + IDX * 3;
  const T VX = V[0], VY = V[1], VZ = V[2];

  // t = 2 * cross(q.xyz, v)
  const T TX = T(2) * (QY * VZ - QZ * VY);
  const T TY = T(2) * (QZ * VX - QX * VZ);
  const T TZ = T(2) * (QX * VY - QY * VX);

  // v' = v + q.w * t + cross(q.xyz, t)
  T* OUT = vsOut + IDX * 3;
  OUT[0] = VX + QW * TX + (QY * TZ - QZ * TY);
  OUT[1] = VY + QW * TY + (QZ * TX - QX * TZ);
  OUT[2] = VZ + QW * TZ + (QX * TY - QY * TX);
}

/**
 * @brief Kernel: SLERP interpolation between quaternion pairs.
 */
template <typename T>
__global__ void slerpBatchKernel(const T* SIM_RESTRICT qsA, const T* SIM_RESTRICT qsB,
                                 const T* SIM_RESTRICT ts, int batch, T* SIM_RESTRICT qsOut) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = qsA + IDX * 4;
  const T* B = qsB + IDX * 4;
  const T T_VAL = ts[IDX];
  T* OUT = qsOut + IDX * 4;

  // Compute dot product
  T dot = A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3];

  // If dot < 0, negate one quaternion to take shorter path
  T sign = T(1);
  if (dot < T(0)) {
    dot = -dot;
    sign = T(-1);
  }

  T scale0, scale1;
  if (dot > T(0.9995)) {
    // Quaternions very close, use linear interpolation
    scale0 = T(1) - T_VAL;
    scale1 = T_VAL * sign;
  } else {
    // Standard SLERP
    const T THETA = acos(dot);
    const T SIN_THETA = sin(THETA);
    scale0 = sin((T(1) - T_VAL) * THETA) / SIN_THETA;
    scale1 = sin(T_VAL * THETA) / SIN_THETA * sign;
  }

  OUT[0] = scale0 * A[0] + scale1 * B[0];
  OUT[1] = scale0 * A[1] + scale1 * B[1];
  OUT[2] = scale0 * A[2] + scale1 * B[2];
  OUT[3] = scale0 * A[3] + scale1 * B[3];
}

/**
 * @brief Kernel: normalize quaternions in-place.
 */
template <typename T> __global__ void normalizeBatchKernel(T* SIM_RESTRICT qs, int batch) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  T* Q = qs + IDX * 4;
  const T NRM = sqrt(Q[0] * Q[0] + Q[1] * Q[1] + Q[2] * Q[2] + Q[3] * Q[3]);

  if (NRM > T(1e-12)) {
    const T INV = T(1) / NRM;
    Q[0] *= INV;
    Q[1] *= INV;
    Q[2] *= INV;
    Q[3] *= INV;
  }
}

/**
 * @brief Kernel: Hamilton product of quaternion pairs.
 */
template <typename T>
__global__ void multiplyBatchKernel(const T* SIM_RESTRICT qsA, const T* SIM_RESTRICT qsB, int batch,
                                    T* SIM_RESTRICT qsOut) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* A = qsA + IDX * 4;
  const T* B = qsB + IDX * 4;
  T* OUT = qsOut + IDX * 4;

  const T W1 = A[0], X1 = A[1], Y1 = A[2], Z1 = A[3];
  const T W2 = B[0], X2 = B[1], Y2 = B[2], Z2 = B[3];

  // Hamilton product
  OUT[0] = W1 * W2 - X1 * X2 - Y1 * Y2 - Z1 * Z2;
  OUT[1] = W1 * X2 + X1 * W2 + Y1 * Z2 - Z1 * Y2;
  OUT[2] = W1 * Y2 - X1 * Z2 + Y1 * W2 + Z1 * X2;
  OUT[3] = W1 * Z2 + X1 * Y2 - Y1 * X2 + Z1 * W2;
}

/**
 * @brief Kernel: convert quaternions to rotation matrices.
 */
template <typename T>
__global__ void toRotationMatrixBatchKernel(const T* SIM_RESTRICT qs, int batch,
                                            T* SIM_RESTRICT mats) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch) {
    return;
  }

  const T* Q = qs + IDX * 4;
  T* M = mats + IDX * 9;

  const T W = Q[0], X = Q[1], Y = Q[2], Z = Q[3];

  // Precompute repeated products
  const T XX = X * X, YY = Y * Y, ZZ = Z * Z;
  const T XY = X * Y, XZ = X * Z, YZ = Y * Z;
  const T WX = W * X, WY = W * Y, WZ = W * Z;

  // Row-major 3x3 rotation matrix
  M[0] = T(1) - T(2) * (YY + ZZ);
  M[1] = T(2) * (XY - WZ);
  M[2] = T(2) * (XZ + WY);

  M[3] = T(2) * (XY + WZ);
  M[4] = T(1) - T(2) * (XX + ZZ);
  M[5] = T(2) * (YZ - WX);

  M[6] = T(2) * (XZ - WY);
  M[7] = T(2) * (YZ + WX);
  M[8] = T(1) - T(2) * (XX + YY);
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- API Functions ------------------------------ */

bool rotateVectorBatchCuda(const double* dQs, const double* dVsIn, int batch, double* dVsOut,
                           void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQs;
  (void)dVsIn;
  (void)batch;
  (void)dVsOut;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQs == nullptr || dVsIn == nullptr || dVsOut == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  rotateVectorBatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQs, dVsIn, batch, dVsOut);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool rotateVectorBatchCudaF(const float* dQs, const float* dVsIn, int batch, float* dVsOut,
                            void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQs;
  (void)dVsIn;
  (void)batch;
  (void)dVsOut;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQs == nullptr || dVsIn == nullptr || dVsOut == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  rotateVectorBatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQs, dVsIn, batch, dVsOut);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool slerpBatchCuda(const double* dQsA, const double* dQsB, const double* dTs, int batch,
                    double* dQsOut, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQsA;
  (void)dQsB;
  (void)dTs;
  (void)batch;
  (void)dQsOut;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQsA == nullptr || dQsB == nullptr || dTs == nullptr || dQsOut == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  slerpBatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQsA, dQsB, dTs, batch, dQsOut);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool slerpBatchCudaF(const float* dQsA, const float* dQsB, const float* dTs, int batch,
                     float* dQsOut, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQsA;
  (void)dQsB;
  (void)dTs;
  (void)batch;
  (void)dQsOut;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQsA == nullptr || dQsB == nullptr || dTs == nullptr || dQsOut == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  slerpBatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQsA, dQsB, dTs, batch, dQsOut);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool normalizeBatchCuda(double* dQs, int batch, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQs;
  (void)batch;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  normalizeBatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQs, batch);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool normalizeBatchCudaF(float* dQs, int batch, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQs;
  (void)batch;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQs == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  normalizeBatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQs, batch);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool multiplyBatchCuda(const double* dQsA, const double* dQsB, int batch, double* dQsOut,
                       void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQsA;
  (void)dQsB;
  (void)batch;
  (void)dQsOut;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQsA == nullptr || dQsB == nullptr || dQsOut == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  multiplyBatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQsA, dQsB, batch, dQsOut);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool multiplyBatchCudaF(const float* dQsA, const float* dQsB, int batch, float* dQsOut,
                        void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQsA;
  (void)dQsB;
  (void)batch;
  (void)dQsOut;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQsA == nullptr || dQsB == nullptr || dQsOut == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  multiplyBatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQsA, dQsB, batch, dQsOut);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool toRotationMatrixBatchCuda(const double* dQs, int batch, double* dMats, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQs;
  (void)batch;
  (void)dMats;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQs == nullptr || dMats == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  toRotationMatrixBatchKernel<double><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQs, batch, dMats);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool toRotationMatrixBatchCudaF(const float* dQs, int batch, float* dMats, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dQs;
  (void)batch;
  (void)dMats;
  (void)stream;
  return false;
#else
  if (batch <= 0 || dQs == nullptr || dMats == nullptr) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int BLOCKS = (batch + K_THREADS_PER_BLOCK - 1) / K_THREADS_PER_BLOCK;

  toRotationMatrixBatchKernel<float><<<BLOCKS, K_THREADS_PER_BLOCK, 0, s>>>(dQs, batch, dMats);

  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

} // namespace cuda
} // namespace quaternion
} // namespace math
} // namespace apex
