#ifndef APEX_MATH_LINALG_ARRAY_HPP
#define APEX_MATH_LINALG_ARRAY_HPP
/**
 * @file Array.hpp
 * @brief Non-owning 2D array view with accelerated operations.
 *
 * Thin wrapper over ArrayBase<T> that exposes optimized operations.
 *  - Non-owning (no allocations except where explicitly documented).
 *  - Supports T = float, double (explicitly instantiated in the .cpp).
 *  - Returns uint8_t status codes (see ArrayStatus.hpp).
 *  - No exceptions on hot paths.
 *  - Uses BLAS/LAPACK when available; falls back to naive loops otherwise.
 *
 * @note RT-SAFE: Core operations are noexcept. Some operations (inverse,
 *       determinant) use temporary allocations internally.
 */

#include "src/utilities/compatibility/inc/compat_blas.hpp"
#include "src/utilities/math/linalg/inc/ArrayBase.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace apex {
namespace math {
namespace linalg {

/* --------------------------------- Array ---------------------------------- */

/**
 * @brief Accelerated 2D array view.
 *
 * Uses BLAS/LAPACK when available and beneficial; falls back to naive
 * implementations for small matrices or when libraries are unavailable.
 *
 * @tparam T Element type (float or double).
 *
 * @note RT-SAFE for view operations. GEMM/transpose are RT-safe.
 *       Inverse/determinant allocate internally (NOT RT-safe).
 */
template <typename T> class Array final : public ArrayBase<T, Array<T>> {
  static_assert(std::is_same<T, float>::value || std::is_same<T, double>::value,
                "Array<T> supports only float or double.");

public:
  using Base = ArrayBase<T, Array<T>>;
  using ValueType = T;

  using Base::Base; // Inherit constructors.

  /* ----------------------------- GEMM ------------------------------------ */

  /**
   * @brief C = alpha * A * B + beta * C (this is A), no transpose.
   *
   * Shapes: A(m x k), B(k x n), C(m x n).
   * All operands must share the same layout.
   *
   * @note RT-SAFE: No allocation.
   */
  uint8_t gemmInto(const Array& B, Array& C, T alpha = T(1), T beta = T(0)) const noexcept;

  /**
   * @brief C = alpha * op(A) * op(B) + beta * C with explicit transpose flags.
   *
   * @param transA NoTrans or Trans for A.
   * @param B Right-hand operand.
   * @param transB NoTrans or Trans for B.
   * @param C Output matrix (also input if beta != 0).
   * @param alpha Scalar multiplier for op(A) * op(B).
   * @param beta Scalar multiplier for C.
   *
   * @note RT-SAFE: No allocation.
   */
  uint8_t gemmIntoTrans(Transpose transA, const Array& B, Transpose transB, Array& C,
                        T alpha = T(1), T beta = T(0)) const noexcept;

  /* --------------------------- Transpose --------------------------------- */

  /**
   * @brief dst = transpose(A) (out-of-place).
   *
   * Requirements: dst shape is (cols x rows), both tight.
   *
   * @note RT-SAFE: No allocation.
   */
  uint8_t transposeInto(Array& dst) const noexcept;

  /* ----------------------------- Inverse --------------------------------- */

  /**
   * @brief In-place matrix inverse (square only).
   *
   * Uses LU factorization via LAPACKE when available; naive for small matrices.
   *
   * @note NOT RT-SAFE: Allocates pivot array internally.
   */
  uint8_t inverseInPlace() noexcept;

  /**
   * @brief dst = inverse(A) (out-of-place, leaves A unchanged).
   *
   * @note NOT RT-SAFE: Allocates temporary buffer and pivot array.
   */
  uint8_t inverseInto(Array& dst) const noexcept;

  /* -------------------------- Determinant -------------------------------- */

  /**
   * @brief det(A) via LU factorization (square only).
   *
   * Uses LAPACKE when available; naive for small matrices.
   *
   * @note NOT RT-SAFE: Allocates temporary buffer for large matrices.
   */
  uint8_t determinant(T& out) const noexcept;

  /* ----------------------------- Trace ----------------------------------- */

  /**
   * @brief trace(A) (square only, layout-agnostic).
   *
   * @note RT-SAFE: No allocation.
   */
  uint8_t trace(T& out) const noexcept;
};

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_ARRAY_HPP
