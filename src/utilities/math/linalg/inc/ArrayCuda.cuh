#ifndef APEX_MATH_LINALG_ARRAY_CUDA_CUH
#define APEX_MATH_LINALG_ARRAY_CUDA_CUH
/**
 * @file ArrayCuda.cuh
 * @brief CUDA-accelerated batch linear algebra operations.
 *
 * Provides GPU-accelerated versions of array operations for batch processing.
 * Useful for processing many small matrices in parallel (e.g., per-particle
 * rotation matrices in simulations).
 *
 * API Design:
 *  - All functions take device pointers (caller manages transfers).
 *  - Stream parameter is void* to avoid polluting headers with CUDA types.
 *  - Returns bool: true on success, false on failure or CUDA unavailable.
 *
 * Data Layout:
 *  - All matrices are row-major, batch-major (matrix[i] starts at i*rows*cols).
 *  - Vector operations use contiguous storage.
 *
 * @note RT-SAFE when using pre-allocated buffers and async API.
 */

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace linalg {
namespace cuda {

/* ----------------------------- Device Utilities --------------------------- */

/**
 * @brief Compute batch output size for matrix multiply.
 * @param m Rows of A.
 * @param n Cols of B.
 * @param batch Number of matrix pairs.
 * @return Total elements in output (batch * m * n).
 * @note RT-SAFE: No allocation.
 */
SIM_HD_FI std::size_t gemmBatchSize(int m, int n, int batch) noexcept {
  if (m <= 0 || n <= 0 || batch <= 0) {
    return 0;
  }
  return static_cast<std::size_t>(batch) * static_cast<std::size_t>(m) *
         static_cast<std::size_t>(n);
}

/**
 * @brief Compute batch output size for 3x3 operations.
 * @param batch Number of matrices.
 * @return Total elements (batch * 9).
 * @note RT-SAFE: No allocation.
 */
SIM_HD_FI std::size_t matrix3BatchSize(int batch) noexcept {
  return (batch > 0) ? static_cast<std::size_t>(batch) * 9u : 0u;
}

/* ----------------------------- Batch GEMM --------------------------------- */

/**
 * @brief Batch 3x3 matrix multiplication (GPU).
 *
 * Computes C[i] = A[i] * B[i] for each matrix in the batch.
 * All matrices are 3x3, row-major.
 *
 * @param dAs Device pointer to A matrices (batch * 9 elements).
 * @param dBs Device pointer to B matrices (batch * 9 elements).
 * @param batch Number of matrix pairs.
 * @param dCs Device pointer to output matrices (batch * 9 elements).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool gemm3x3BatchCuda(const double* dAs, const double* dBs, int batch, double* dCs,
                      void* stream = nullptr) noexcept;

/**
 * @brief Float version of gemm3x3BatchCuda.
 */
bool gemm3x3BatchCudaF(const float* dAs, const float* dBs, int batch, float* dCs,
                       void* stream = nullptr) noexcept;

/* ----------------------------- Batch Transpose ---------------------------- */

/**
 * @brief Batch 3x3 matrix transpose (GPU).
 *
 * Computes Out[i] = transpose(A[i]) for each matrix in the batch.
 *
 * @param dAs Device pointer to input matrices (batch * 9 elements).
 * @param batch Number of matrices.
 * @param dOuts Device pointer to output matrices (batch * 9 elements).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool transpose3x3BatchCuda(const double* dAs, int batch, double* dOuts,
                           void* stream = nullptr) noexcept;

/**
 * @brief Float version of transpose3x3BatchCuda.
 */
bool transpose3x3BatchCudaF(const float* dAs, int batch, float* dOuts,
                            void* stream = nullptr) noexcept;

/* ----------------------------- Batch Inverse ------------------------------ */

/**
 * @brief Batch 3x3 matrix inverse (GPU).
 *
 * Computes Out[i] = inverse(A[i]) for each matrix in the batch.
 * Uses Cramer's rule (efficient for 3x3).
 *
 * @param dAs Device pointer to input matrices (batch * 9 elements).
 * @param batch Number of matrices.
 * @param dOuts Device pointer to output matrices (batch * 9 elements).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 * @note Singular matrices produce NaN/Inf in output.
 */
bool inverse3x3BatchCuda(const double* dAs, int batch, double* dOuts,
                         void* stream = nullptr) noexcept;

/**
 * @brief Float version of inverse3x3BatchCuda.
 */
bool inverse3x3BatchCudaF(const float* dAs, int batch, float* dOuts,
                          void* stream = nullptr) noexcept;

/* ----------------------------- Batch Determinant -------------------------- */

/**
 * @brief Batch 3x3 matrix determinant (GPU).
 *
 * Computes det[i] = determinant(A[i]) for each matrix in the batch.
 *
 * @param dAs Device pointer to input matrices (batch * 9 elements).
 * @param batch Number of matrices.
 * @param dDets Device pointer to output determinants (batch elements).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool determinant3x3BatchCuda(const double* dAs, int batch, double* dDets,
                             void* stream = nullptr) noexcept;

/**
 * @brief Float version of determinant3x3BatchCuda.
 */
bool determinant3x3BatchCudaF(const float* dAs, int batch, float* dDets,
                              void* stream = nullptr) noexcept;

/* ----------------------------- Matrix-Vector ------------------------------ */

/**
 * @brief Batch 3x3 matrix-vector multiply (GPU).
 *
 * Computes y[i] = A[i] * x[i] for each matrix-vector pair.
 *
 * @param dAs Device pointer to matrices (batch * 9 elements).
 * @param dXs Device pointer to input vectors (batch * 3 elements).
 * @param batch Number of pairs.
 * @param dYs Device pointer to output vectors (batch * 3 elements).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool matvec3x3BatchCuda(const double* dAs, const double* dXs, int batch, double* dYs,
                        void* stream = nullptr) noexcept;

/**
 * @brief Float version of matvec3x3BatchCuda.
 */
bool matvec3x3BatchCudaF(const float* dAs, const float* dXs, int batch, float* dYs,
                         void* stream = nullptr) noexcept;

/* ----------------------------- Vector Operations -------------------------- */

/**
 * @brief Batch 3D cross product (GPU).
 *
 * Computes c[i] = a[i] x b[i] for each vector pair.
 *
 * @param dAs Device pointer to first vectors (batch * 3 elements).
 * @param dBs Device pointer to second vectors (batch * 3 elements).
 * @param batch Number of vector pairs.
 * @param dCs Device pointer to output vectors (batch * 3 elements).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool cross3BatchCuda(const double* dAs, const double* dBs, int batch, double* dCs,
                     void* stream = nullptr) noexcept;

/**
 * @brief Float version of cross3BatchCuda.
 */
bool cross3BatchCudaF(const float* dAs, const float* dBs, int batch, float* dCs,
                      void* stream = nullptr) noexcept;

/**
 * @brief Batch 3D dot product (GPU).
 *
 * Computes out[i] = a[i] . b[i] for each vector pair.
 *
 * @param dAs Device pointer to first vectors (batch * 3 elements).
 * @param dBs Device pointer to second vectors (batch * 3 elements).
 * @param batch Number of vector pairs.
 * @param dOuts Device pointer to output scalars (batch elements).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool dot3BatchCuda(const double* dAs, const double* dBs, int batch, double* dOuts,
                   void* stream = nullptr) noexcept;

/**
 * @brief Float version of dot3BatchCuda.
 */
bool dot3BatchCudaF(const float* dAs, const float* dBs, int batch, float* dOuts,
                    void* stream = nullptr) noexcept;

/**
 * @brief Batch 3D vector normalization (GPU).
 *
 * Normalizes each vector in-place to unit length.
 *
 * @param dVs Device pointer to vectors (batch * 3 elements, in/out).
 * @param batch Number of vectors.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 * @note Zero-length vectors remain unchanged.
 */
bool normalize3BatchCuda(double* dVs, int batch, void* stream = nullptr) noexcept;

/**
 * @brief Float version of normalize3BatchCuda.
 */
bool normalize3BatchCudaF(float* dVs, int batch, void* stream = nullptr) noexcept;

} // namespace cuda
} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_ARRAY_CUDA_CUH
