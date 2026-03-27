#ifndef APEX_MATH_QUATERNION_CUDA_CUH
#define APEX_MATH_QUATERNION_CUDA_CUH
/**
 * @file QuaternionCuda.cuh
 * @brief CUDA-accelerated batch quaternion operations.
 *
 * Provides GPU-accelerated versions of quaternion operations for batch
 * processing. Useful when rotating many vectors or interpolating many
 * quaternion pairs.
 *
 * API Design:
 *  - All functions take device pointers (caller manages transfers).
 *  - Stream parameter is void* to avoid polluting headers with CUDA types.
 *  - Returns bool: true on success, false on failure or CUDA unavailable.
 *
 * Data Layout:
 *  - Quaternions: contiguous [w,x,y,z] per element, batch-major.
 *  - Vectors: contiguous [x,y,z] per element, batch-major.
 *
 * @note RT-SAFE when using pre-allocated buffers and async API.
 */

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace quaternion {
namespace cuda {

/* ----------------------------- Device Utilities --------------------------- */

/**
 * @brief Compute required output size for batch vector rotation.
 * @param batch Number of vectors to rotate.
 * @return Number of elements in output array (batch * 3).
 * @note RT-SAFE: No allocation.
 */
SIM_HD_FI std::size_t rotateVectorBatchSize(int batch) noexcept {
  return (batch > 0) ? static_cast<std::size_t>(batch) * 3u : 0u;
}

/**
 * @brief Compute required output size for batch SLERP.
 * @param batch Number of quaternion pairs to interpolate.
 * @return Number of elements in output array (batch * 4).
 * @note RT-SAFE: No allocation.
 */
SIM_HD_FI std::size_t slerpBatchSize(int batch) noexcept {
  return (batch > 0) ? static_cast<std::size_t>(batch) * 4u : 0u;
}

/* ----------------------------- Batch Operations --------------------------- */

/**
 * @brief Rotate a batch of vectors by corresponding quaternions (GPU).
 *
 * Each quaternion q[i] rotates vector vIn[i] to produce vOut[i].
 * Formula: v' = q * v * q^{-1} (assumes unit quaternions).
 *
 * @param dQs Device pointer to batch quaternions [w,x,y,z] * batch.
 * @param dVsIn Device pointer to input vectors [x,y,z] * batch.
 * @param batch Number of quaternion-vector pairs.
 * @param dVsOut Device pointer to output vectors [x',y',z'] * batch.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool rotateVectorBatchCuda(const double* dQs, const double* dVsIn, int batch, double* dVsOut,
                           void* stream = nullptr) noexcept;

/**
 * @brief Float version of rotateVectorBatchCuda.
 */
bool rotateVectorBatchCudaF(const float* dQs, const float* dVsIn, int batch, float* dVsOut,
                            void* stream = nullptr) noexcept;

/**
 * @brief Batch SLERP interpolation (GPU).
 *
 * Interpolates between quaternion pairs: out[i] = slerp(qA[i], qB[i], t[i]).
 *
 * @param dQsA Device pointer to start quaternions [w,x,y,z] * batch.
 * @param dQsB Device pointer to end quaternions [w,x,y,z] * batch.
 * @param dTs Device pointer to interpolation parameters [0,1] * batch.
 * @param batch Number of quaternion pairs.
 * @param dQsOut Device pointer to output quaternions [w,x,y,z] * batch.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool slerpBatchCuda(const double* dQsA, const double* dQsB, const double* dTs, int batch,
                    double* dQsOut, void* stream = nullptr) noexcept;

/**
 * @brief Float version of slerpBatchCuda.
 */
bool slerpBatchCudaF(const float* dQsA, const float* dQsB, const float* dTs, int batch,
                     float* dQsOut, void* stream = nullptr) noexcept;

/**
 * @brief Batch quaternion normalization (GPU).
 *
 * Normalizes each quaternion in-place to unit length.
 *
 * @param dQs Device pointer to quaternions [w,x,y,z] * batch (in/out).
 * @param batch Number of quaternions.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool normalizeBatchCuda(double* dQs, int batch, void* stream = nullptr) noexcept;

/**
 * @brief Float version of normalizeBatchCuda.
 */
bool normalizeBatchCudaF(float* dQs, int batch, void* stream = nullptr) noexcept;

/**
 * @brief Batch quaternion multiplication (GPU).
 *
 * Computes out[i] = qA[i] * qB[i] (Hamilton product).
 *
 * @param dQsA Device pointer to left quaternions [w,x,y,z] * batch.
 * @param dQsB Device pointer to right quaternions [w,x,y,z] * batch.
 * @param batch Number of quaternion pairs.
 * @param dQsOut Device pointer to output quaternions [w,x,y,z] * batch.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool multiplyBatchCuda(const double* dQsA, const double* dQsB, int batch, double* dQsOut,
                       void* stream = nullptr) noexcept;

/**
 * @brief Float version of multiplyBatchCuda.
 */
bool multiplyBatchCudaF(const float* dQsA, const float* dQsB, int batch, float* dQsOut,
                        void* stream = nullptr) noexcept;

/**
 * @brief Batch conversion to rotation matrices (GPU).
 *
 * Converts each quaternion to a 3x3 rotation matrix (row-major).
 *
 * @param dQs Device pointer to quaternions [w,x,y,z] * batch.
 * @param batch Number of quaternions.
 * @param dMats Device pointer to output matrices [9 elements] * batch.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool toRotationMatrixBatchCuda(const double* dQs, int batch, double* dMats,
                               void* stream = nullptr) noexcept;

/**
 * @brief Float version of toRotationMatrixBatchCuda.
 */
bool toRotationMatrixBatchCudaF(const float* dQs, int batch, float* dMats,
                                void* stream = nullptr) noexcept;

} // namespace cuda
} // namespace quaternion
} // namespace math
} // namespace apex

#endif // APEX_MATH_QUATERNION_CUDA_CUH
