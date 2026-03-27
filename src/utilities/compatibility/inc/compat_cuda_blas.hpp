#ifndef APEX_UTILITIES_COMPATIBILITY_CUDA_BLAS_HPP
#define APEX_UTILITIES_COMPATIBILITY_CUDA_BLAS_HPP
/**
 * @file compat_cuda_blas.hpp
 * @brief Lightweight CUDA/cuBLAS/cuSOLVER shims: header detection, layout &
 *        transpose mapping (incl. row-major emulation), handle/stream utils,
 *        and status translation to raw uint8_t.
 *
 * - Does NOT depend on your array module.
 * - Reuses apex::compat::blas::Layout (RowMajor/ColMajor).
 * - Allows callers to remain layout-agnostic while using column-major cuBLAS.
 * - Returns raw uint8_t (0 = success). Callers may translate to project enums.
 *
 * @note RT-SAFE: All functions in this header are inline and perform no
 *       blocking operations beyond the underlying CUDA/cuBLAS calls (which
 *       are caller responsibility). Handle creation/destruction is RT-UNSAFE.
 */

#include "src/utilities/compatibility/inc/compat_blas.hpp" // for apex::compat::blas::Layout

#include <climits> // INT32_MAX
#include <cstddef>
#include <cstdint>

/* ----------------------------- Header Detection ----------------------------- */

#ifndef COMPAT_HAVE_CUDA
#if defined(__CUDACC__) || (defined(__has_include) && __has_include(<cuda_runtime_api.h>))
#define COMPAT_HAVE_CUDA 1
#else
#define COMPAT_HAVE_CUDA 0
#endif
#endif

#ifndef COMPAT_HAVE_CUBLAS
#if defined(__has_include) && __has_include(<cublas_v2.h>)
#define COMPAT_HAVE_CUBLAS 1
#else
#define COMPAT_HAVE_CUBLAS 0
#endif
#endif

#ifndef COMPAT_HAVE_CUSOLVER
#if defined(__has_include) && __has_include(<cusolverDn.h>)
#define COMPAT_HAVE_CUSOLVER 1
#else
#define COMPAT_HAVE_CUSOLVER 0
#endif
#endif

#if COMPAT_HAVE_CUDA
#include <cuda_runtime_api.h> // for cudaStream_t
#endif
#if COMPAT_HAVE_CUBLAS
#include <cublas_v2.h>
#endif
#if COMPAT_HAVE_CUSOLVER
#include <cusolverDn.h>
#endif

namespace apex {
namespace compat {
namespace cuda {

/* ----------------------------- Availability Queries ----------------------------- */

/**
 * @brief Check if CUDA runtime headers are available.
 * @return True if CUDA runtime is available at compile time.
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr bool runtimeAvailable() noexcept { return COMPAT_HAVE_CUDA == 1; }

/**
 * @brief Check if cuBLAS headers are available.
 * @return True if cuBLAS is available at compile time.
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr bool cublasAvailable() noexcept { return COMPAT_HAVE_CUBLAS == 1; }

/**
 * @brief Check if cuSOLVER headers are available.
 * @return True if cuSOLVER is available at compile time.
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr bool cusolverAvailable() noexcept { return COMPAT_HAVE_CUSOLVER == 1; }

/* ----------------------------- Status Mapping ----------------------------- */

/**
 * @brief Success status code.
 * @return 0 (success).
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr std::uint8_t ok() noexcept { return 0u; }

/**
 * @brief Library failure status code.
 * @return 7 (library failure).
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr std::uint8_t mapLibFailure() noexcept { return 7u; }

/**
 * @brief Singular matrix status code.
 * @return 6 (singular/zero pivot).
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr std::uint8_t mapSingular() noexcept { return 6u; }

/**
 * @brief Invalid value status code.
 * @return 8 (invalid value).
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr std::uint8_t mapInvalidValue() noexcept { return 8u; }

/**
 * @brief Unsupported operation status code.
 * @return 9 (unsupported).
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr std::uint8_t mapUnsupported() noexcept { return 9u; }

#if COMPAT_HAVE_CUBLAS
/**
 * @brief Convert cuBLAS status to uint8_t status code.
 * @param s cuBLAS status.
 * @return Mapped status code.
 * @note RT-SAFE: Pure inline, no side effects.
 */
inline std::uint8_t toUint8(cublasStatus_t s) noexcept {
  switch (s) {
  case CUBLAS_STATUS_SUCCESS:
    return ok();
  case CUBLAS_STATUS_INVALID_VALUE:
    return mapInvalidValue();
  case CUBLAS_STATUS_NOT_SUPPORTED:
    return mapUnsupported();
  case CUBLAS_STATUS_ARCH_MISMATCH:
    return mapUnsupported();
  case CUBLAS_STATUS_ALLOC_FAILED:
    return mapLibFailure();
  case CUBLAS_STATUS_EXECUTION_FAILED:
    return mapLibFailure();
  case CUBLAS_STATUS_INTERNAL_ERROR:
    return mapLibFailure();
  default:
    return mapLibFailure();
  }
}
#endif

#if COMPAT_HAVE_CUSOLVER
/**
 * @brief Convert cuSOLVER status to uint8_t status code.
 * @param s cuSOLVER status.
 * @return Mapped status code.
 * @note RT-SAFE: Pure inline, no side effects.
 */
inline std::uint8_t toUint8(cusolverStatus_t s) noexcept {
  switch (s) {
  case CUSOLVER_STATUS_SUCCESS:
    return ok();
  case CUSOLVER_STATUS_INVALID_VALUE:
    return mapInvalidValue();
#ifdef CUSOLVER_STATUS_NOT_SUPPORTED
  case CUSOLVER_STATUS_NOT_SUPPORTED:
    return mapUnsupported();
#endif
#ifdef CUSOLVER_STATUS_ARCH_MISMATCH
  case CUSOLVER_STATUS_ARCH_MISMATCH:
    return mapUnsupported();
#endif
#ifdef CUSOLVER_STATUS_NOT_INITIALIZED
  case CUSOLVER_STATUS_NOT_INITIALIZED:
    return mapLibFailure();
#endif
#ifdef CUSOLVER_STATUS_ALLOC_FAILED
  case CUSOLVER_STATUS_ALLOC_FAILED:
    return mapLibFailure();
#endif
#ifdef CUSOLVER_STATUS_MAPPING_ERROR
  case CUSOLVER_STATUS_MAPPING_ERROR:
    return mapLibFailure();
#endif
#ifdef CUSOLVER_STATUS_EXECUTION_FAILED
  case CUSOLVER_STATUS_EXECUTION_FAILED:
    return mapLibFailure();
#endif
#ifdef CUSOLVER_STATUS_INTERNAL_ERROR
  case CUSOLVER_STATUS_INTERNAL_ERROR:
    return mapLibFailure();
#endif
#ifdef CUSOLVER_STATUS_MATRIX_TYPE_NOT_SUPPORTED
  case CUSOLVER_STATUS_MATRIX_TYPE_NOT_SUPPORTED:
    return mapUnsupported();
#endif
  default:
    return mapLibFailure();
  }
}

/**
 * @brief LAPACK-like info mapping used after getrf/getri on device/host info.
 * @param info LAPACK-style info value.
 * @return Mapped status code.
 * @note RT-SAFE: Pure constexpr.
 */
inline constexpr std::uint8_t lapackLikeInfoToUint8(int info) noexcept {
  if (info == 0)
    return ok();
  if (info > 0)
    return mapSingular(); // zero pivot
  return mapLibFailure(); // -k: illegal argument
}
#endif

/* ----------------------------- cuBLAS Operation Mapping ----------------------------- */

#if COMPAT_HAVE_CUBLAS
/**
 * @brief Convert apex::compat::blas::Transpose to cuBLAS operation.
 * @param t Transpose flag.
 * @return cuBLAS operation constant.
 * @note RT-SAFE: Pure inline.
 */
inline cublasOperation_t toCublasOp(apex::compat::blas::Transpose t) noexcept {
  return (t == apex::compat::blas::Transpose::NoTrans) ? CUBLAS_OP_N : CUBLAS_OP_T;
}
#endif

/* ----------------------------- Types ----------------------------- */

/**
 * @brief GEMM parameter packing with RowMajor emulation.
 *
 * cuBLAS is column-major. For RowMajor inputs C = op(A)*op(B), we emulate by
 * computing C^T = op(B)^T * op(A)^T and swapping operands/ops as needed.
 *
 * @note RT-SAFE: POD type, no allocations.
 */
struct GemmDims {
  // dimensions for the cuBLAS call (always column-major convention)
  int m;   ///< rows of op(A)
  int n;   ///< cols of op(B)
  int k;   ///< cols of op(A) = rows of op(B)
  int lda; ///< leading dimension of A
  int ldb; ///< leading dimension of B
  int ldc; ///< leading dimension of C
  // cuBLAS transpose ops
#if COMPAT_HAVE_CUBLAS
  cublasOperation_t opA;
  cublasOperation_t opB;
#else
  std::uint8_t opA;
  std::uint8_t opB;
#endif
  bool swappedAB; ///< Whether we emulated RowMajor by swapping roles of A and B
};

/* ----------------------------- GEMM Helpers ----------------------------- */

/**
 * @brief Compute cuBLAS GEMM dims/ops from logical shapes and layout.
 *
 * Logical inputs:
 *   - A has shape (aRows x aCols) before opA
 *   - B has shape (bRows x bCols) before opB
 *   - C has shape (cRows x cCols)
 * Leading dimensions are given in element counts for the provided layout.
 *
 * @param aRows Rows of A.
 * @param aCols Cols of A.
 * @param lda Leading dimension of A.
 * @param bRows Rows of B.
 * @param bCols Cols of B.
 * @param ldb Leading dimension of B.
 * @param cRows Rows of C.
 * @param cCols Cols of C.
 * @param ldc Leading dimension of C.
 * @param layout Storage layout.
 * @param transA Transpose flag for A.
 * @param transB Transpose flag for B.
 * @param out Output GemmDims structure.
 * @return False if values overflow 32-bit 'int'.
 * @note RT-SAFE: Pure inline, no side effects.
 */
inline bool makeGemmDims(std::size_t aRows, std::size_t aCols, std::size_t lda, std::size_t bRows,
                         std::size_t bCols, std::size_t ldb, [[maybe_unused]] std::size_t cRows,
                         [[maybe_unused]] std::size_t cCols, std::size_t ldc,
                         apex::compat::blas::Layout layout, apex::compat::blas::Transpose transA,
                         apex::compat::blas::Transpose transB, GemmDims& out) noexcept {
  // helper: fits in 32-bit int
  auto fitsI = [](std::size_t x) -> bool { return x <= static_cast<std::size_t>(INT32_MAX); };

  // compute effective shapes after transpose flags
  const std::size_t A_ROWS = (transA == apex::compat::blas::Transpose::NoTrans) ? aRows : aCols;
  const std::size_t A_COLS = (transA == apex::compat::blas::Transpose::NoTrans) ? aCols : aRows;
  const std::size_t B_ROWS = (transB == apex::compat::blas::Transpose::NoTrans) ? bRows : bCols;
  const std::size_t B_COLS = (transB == apex::compat::blas::Transpose::NoTrans) ? bCols : bRows;

  // For column-major (native): op(A)(A_ROWS x A_COLS) * op(B)(B_ROWS x B_COLS) = (A_ROWS x B_COLS)
  // For row-major: emulate via transpose: C^T = op(B)^T * op(A)^T.
  const bool SWAPPED = (layout == apex::compat::blas::Layout::RowMajor);

  // map sizes
  const std::size_t M_SZ = SWAPPED ? B_COLS : A_ROWS;
  const std::size_t N_SZ = SWAPPED ? A_ROWS : B_COLS;
  const std::size_t K_SZ = SWAPPED ? B_ROWS : A_COLS;

  if (!fitsI(M_SZ) || !fitsI(N_SZ) || !fitsI(K_SZ) || !fitsI(lda) || !fitsI(ldb) || !fitsI(ldc))
    return false;

#if COMPAT_HAVE_CUBLAS
  const auto OP_A = toCublasOp(SWAPPED ? (transB == apex::compat::blas::Transpose::NoTrans
                                              ? apex::compat::blas::Transpose::Trans
                                              : apex::compat::blas::Transpose::NoTrans)
                                       : transA);
  const auto OP_B = toCublasOp(SWAPPED ? (transA == apex::compat::blas::Transpose::NoTrans
                                              ? apex::compat::blas::Transpose::Trans
                                              : apex::compat::blas::Transpose::NoTrans)
                                       : transB);
#else
  const std::uint8_t OP_A = 0, OP_B = 0;
#endif

  out = GemmDims{static_cast<int>(M_SZ),
                 static_cast<int>(N_SZ),
                 static_cast<int>(K_SZ),
                 static_cast<int>(lda),
                 static_cast<int>(ldb),
                 static_cast<int>(ldc),
                 OP_A,
                 OP_B,
                 SWAPPED};
  return true;
}

/* ----------------------------- Handle Types ----------------------------- */

#if COMPAT_HAVE_CUBLAS
using BlasHandle = cublasHandle_t;
#else
using BlasHandle = void*;
#endif

#if COMPAT_HAVE_CUSOLVER
using SolverHandle = cusolverDnHandle_t;
#else
using SolverHandle = void*;
#endif

/* ----------------------------- Handle Management ----------------------------- */

/**
 * @brief Create a cuBLAS handle.
 * @param h Pointer to receive the handle.
 * @return Status code.
 * @note RT-UNSAFE: Allocates GPU resources.
 */
inline std::uint8_t createBlasHandle(BlasHandle* h) noexcept {
#if COMPAT_HAVE_CUBLAS
  if (!h)
    return mapInvalidValue();
  return toUint8(cublasCreate(h));
#else
  (void)h;
  return mapUnsupported();
#endif
}

/**
 * @brief Destroy a cuBLAS handle.
 * @param h The handle to destroy.
 * @return Status code.
 * @note RT-UNSAFE: Deallocates GPU resources.
 */
inline std::uint8_t destroyBlasHandle(BlasHandle h) noexcept {
#if COMPAT_HAVE_CUBLAS
  return toUint8(cublasDestroy(h));
#else
  (void)h;
  return mapUnsupported();
#endif
}

/**
 * @brief Create a cuSOLVER handle.
 * @param h Pointer to receive the handle.
 * @return Status code.
 * @note RT-UNSAFE: Allocates GPU resources.
 */
inline std::uint8_t createSolverHandle(SolverHandle* h) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (!h)
    return mapInvalidValue();
  return toUint8(cusolverDnCreate(h));
#else
  (void)h;
  return mapUnsupported();
#endif
}

/**
 * @brief Destroy a cuSOLVER handle.
 * @param h The handle to destroy.
 * @return Status code.
 * @note RT-UNSAFE: Deallocates GPU resources.
 */
inline std::uint8_t destroySolverHandle(SolverHandle h) noexcept {
#if COMPAT_HAVE_CUSOLVER
  return toUint8(cusolverDnDestroy(h));
#else
  (void)h;
  return mapUnsupported();
#endif
}

/* ----------------------------- Stream Management ----------------------------- */

/**
 * @brief Set the CUDA stream for a cuBLAS handle.
 * @param h cuBLAS handle.
 * @param cudaStream CUDA stream (as void*).
 * @return Status code.
 * @note RT-SAFE: Just sets a pointer, no allocation.
 */
inline std::uint8_t setBlasStream(BlasHandle h, void* cudaStream) noexcept {
#if COMPAT_HAVE_CUBLAS
  return toUint8(cublasSetStream(h, static_cast<cudaStream_t>(cudaStream)));
#else
  (void)h;
  (void)cudaStream;
  return mapUnsupported();
#endif
}

/**
 * @brief Get the CUDA stream from a cuBLAS handle.
 * @param h cuBLAS handle.
 * @param outStream Pointer to receive the stream.
 * @return Status code.
 * @note RT-SAFE: Just retrieves a pointer.
 */
inline std::uint8_t getBlasStream(BlasHandle h, void** outStream) noexcept {
#if COMPAT_HAVE_CUBLAS
  if (!outStream)
    return mapInvalidValue();
  cudaStream_t s{};
  auto rc = cublasGetStream(h, &s);
  *outStream = reinterpret_cast<void*>(s);
  return toUint8(rc);
#else
  (void)h;
  (void)outStream;
  return mapUnsupported();
#endif
}

/**
 * @brief Set the CUDA stream for a cuSOLVER handle.
 * @param h cuSOLVER handle.
 * @param cudaStream CUDA stream (as void*).
 * @return Status code.
 * @note RT-SAFE: Just sets a pointer, no allocation.
 */
inline std::uint8_t setSolverStream(SolverHandle h, void* cudaStream) noexcept {
#if COMPAT_HAVE_CUSOLVER
  return toUint8(cusolverDnSetStream(h, static_cast<cudaStream_t>(cudaStream)));
#else
  (void)h;
  (void)cudaStream;
  return mapUnsupported();
#endif
}

/**
 * @brief Get the CUDA stream from a cuSOLVER handle.
 * @param h cuSOLVER handle.
 * @param outStream Pointer to receive the stream.
 * @return Status code.
 * @note RT-SAFE: Just retrieves a pointer.
 */
inline std::uint8_t getSolverStream(SolverHandle h, void** outStream) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (!outStream)
    return mapInvalidValue();
  cudaStream_t s{};
  auto rc = cusolverDnGetStream(h, &s);
  *outStream = reinterpret_cast<void*>(s);
  return toUint8(rc);
#else
  (void)h;
  (void)outStream;
  return mapUnsupported();
#endif
}

} // namespace cuda
} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_CUDA_BLAS_HPP
