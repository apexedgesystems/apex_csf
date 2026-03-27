#ifndef APEX_UTILITIES_COMPATIBILITY_BLAS_HPP
#define APEX_UTILITIES_COMPATIBILITY_BLAS_HPP
/**
 * @file compat_blas.hpp
 * @brief Lightweight BLAS/LAPACK compatibility shims: headers detection,
 *        layout mapping, and small utilities usable across the project.
 *
 * - Does NOT depend on your array module (generic compat header).
 * - Provides a local Layout enum (RowMajor/ColMajor).
 * - Maps to CBLAS/LAPACK layout constants when headers are available.
 * - Small helpers for tight leading dimension and LAPACK info mapping.
 *
 * Returned status values are raw uint8_t (0 = success). Callers can translate
 * them to project-local enums (e.g., sim::math::array::Status) if desired.
 *
 * @note RT-SAFE: All functions in this header are inline/constexpr and perform
 *       no system calls, memory allocation, or blocking operations. Safe for
 *       real-time contexts.
 */

#include <cstddef>
#include <cstdint>

/* ----------------------------- Header Detection ----------------------------- */

#ifndef COMPAT_HAVE_CBLAS
#if defined(__has_include)
#if __has_include(<cblas.h>)
#define COMPAT_HAVE_CBLAS 1
#else
#define COMPAT_HAVE_CBLAS 0
#endif
#else
#define COMPAT_HAVE_CBLAS 1 // fallback: assume present; override in build if needed
#endif
#endif

#ifndef COMPAT_HAVE_LAPACKE
#if defined(__has_include)
#if __has_include(<lapacke.h>)
#define COMPAT_HAVE_LAPACKE 1
#define COMPAT_LAPACKE_INCLUDE <lapacke.h>
#elif __has_include(<lapacke_utils.h>)
#define COMPAT_HAVE_LAPACKE 1
#define COMPAT_LAPACKE_INCLUDE <lapacke_utils.h>
#else
#define COMPAT_HAVE_LAPACKE 0
#endif
#else
#define COMPAT_HAVE_LAPACKE 1
#define COMPAT_LAPACKE_INCLUDE <lapacke.h>
#endif
#endif

#if COMPAT_HAVE_CBLAS
#include <cblas.h>
#endif

#if COMPAT_HAVE_LAPACKE
#include COMPAT_LAPACKE_INCLUDE
#endif

namespace apex {
namespace compat {
namespace blas {

/* ----------------------------- Types ----------------------------- */

/**
 * @brief Generic layout tag for matrix storage order.
 * @note RT-SAFE: No allocations or system calls.
 */
enum class Layout : std::uint8_t { RowMajor = 0, ColMajor = 1 };

/**
 * @brief Transpose operation flag.
 * @note RT-SAFE: No allocations or system calls.
 */
enum class Transpose : std::uint8_t { NoTrans = 0, Trans = 1 };

/**
 * @brief BLAS/LAPACK dimension pack for GEMM operations.
 *
 * Use to compute and pass m/n/k and leading dimensions coherently.
 * For C = alpha * A(mxk) * B(kxn) + beta * C(mxn).
 *
 * @note RT-SAFE: POD type, no allocations.
 */
struct GemmDims {
  int m;   ///< rows of A and C
  int n;   ///< cols of B and C
  int k;   ///< cols of A = rows of B
  int lda; ///< leading dimension of A (in elements)
  int ldb; ///< leading dimension of B (in elements)
  int ldc; ///< leading dimension of C (in elements)
};

/* ----------------------------- Layout Mapping ----------------------------- */

/**
 * @brief Map apex::compat::blas::Layout to CBLAS layout enum value.
 * @param layout The layout to convert.
 * @return CBLAS layout constant, or -1 if CBLAS unavailable.
 * @note RT-SAFE: Pure constexpr, no side effects.
 */
inline constexpr int toCblasLayout(Layout layout) noexcept {
#if COMPAT_HAVE_CBLAS
  return (layout == Layout::RowMajor) ? CblasRowMajor : CblasColMajor;
#else
  (void)layout;
  return -1;
#endif
}

/**
 * @brief Map apex::compat::blas::Layout to LAPACKE layout macro value.
 * @param layout The layout to convert.
 * @return LAPACKE layout constant, or -1 if LAPACKE unavailable.
 * @note RT-SAFE: Pure constexpr, no side effects.
 */
inline constexpr int toLapackeLayout(Layout layout) noexcept {
#if COMPAT_HAVE_LAPACKE
  return (layout == Layout::RowMajor) ? LAPACK_ROW_MAJOR : LAPACK_COL_MAJOR;
#else
  (void)layout;
  return -1;
#endif
}

/* ----------------------------- Dimension Helpers ----------------------------- */

/**
 * @brief Compute leading dimension for a tightly packed matrix.
 *
 * RowMajor -> ld = cols; ColMajor -> ld = rows.
 *
 * @param rows Number of rows.
 * @param cols Number of columns.
 * @param layout Storage layout.
 * @return The tight leading dimension.
 * @note RT-SAFE: Pure constexpr, no side effects.
 */
inline constexpr std::size_t tightLd(std::size_t rows, std::size_t cols, Layout layout) noexcept {
  return (layout == Layout::RowMajor) ? cols : rows;
}

/**
 * @brief Check if matrix dimensions describe a tight/contiguous matrix.
 * @param rows Number of rows.
 * @param cols Number of columns.
 * @param ld Leading dimension.
 * @param layout Storage layout.
 * @return True if the matrix is tightly packed.
 * @note RT-SAFE: Pure constexpr, no side effects.
 */
inline constexpr bool isTight(std::size_t rows, std::size_t cols, std::size_t ld,
                              Layout layout) noexcept {
  return ld == tightLd(rows, cols, layout);
}

/* ----------------------------- Status Mapping ----------------------------- */

/**
 * @brief Map LAPACK info to raw uint8_t status code.
 *
 * Conventions (generic; callers can translate to their own enums):
 *  - info == 0  -> 0 (success)
 *  - info  < 0  -> 7 (library failure / illegal arg)
 *  - info  > 0  -> 6 (singular / zero pivot)
 *
 * @param info LAPACK return value.
 * @return Mapped status code.
 * @note RT-SAFE: Pure constexpr, no side effects.
 */
inline constexpr std::uint8_t lapackInfoToUint8(int info) noexcept {
  if (info == 0)
    return 0u; // success
  if (info > 0)
    return 6u; // singular
  return 7u;   // lib failure
}

/* ----------------------------- GEMM Helpers ----------------------------- */

/**
 * @brief Compute GemmDims from shapes and layout (A: mxk, B: kxn, C: mxn).
 *
 * @param m Rows of A and C.
 * @param n Cols of B and C.
 * @param k Cols of A = rows of B.
 * @param lda_tight_rows Rows of A for tight ld calculation.
 * @param lda_tight_cols Cols of A for tight ld calculation.
 * @param ldb_tight_rows Rows of B for tight ld calculation.
 * @param ldb_tight_cols Cols of B for tight ld calculation.
 * @param ldc_tight_rows Rows of C for tight ld calculation.
 * @param ldc_tight_cols Cols of C for tight ld calculation.
 * @param layout Storage layout.
 * @return GemmDims structure with computed dimensions.
 * @note RT-SAFE: Pure constexpr, no side effects.
 */
inline constexpr GemmDims makeGemmDims(std::size_t m, std::size_t n, std::size_t k,
                                       std::size_t lda_tight_rows, std::size_t lda_tight_cols,
                                       std::size_t ldb_tight_rows, std::size_t ldb_tight_cols,
                                       std::size_t ldc_tight_rows, std::size_t ldc_tight_cols,
                                       Layout layout) noexcept {
  // If callers use tight matrices, they can pass lda_tight_rows = rows(A),
  // lda_tight_cols = cols(A), etc., and this helper will pick correct ld.
  const int LDA = static_cast<int>((layout == Layout::RowMajor) ? lda_tight_cols : lda_tight_rows);
  const int LDB = static_cast<int>((layout == Layout::RowMajor) ? ldb_tight_cols : ldb_tight_rows);
  const int LDC = static_cast<int>((layout == Layout::RowMajor) ? ldc_tight_cols : ldc_tight_rows);
  return GemmDims{static_cast<int>(m), static_cast<int>(n), static_cast<int>(k), LDA, LDB, LDC};
}

} // namespace blas
} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_BLAS_HPP
