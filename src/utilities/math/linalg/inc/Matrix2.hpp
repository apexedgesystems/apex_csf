#ifndef APEX_MATH_LINALG_MATRIX2_HPP
#define APEX_MATH_LINALG_MATRIX2_HPP
/**
 * @file Matrix2.hpp
 * @brief Strong 2x2 matrix (composition over Array) for 2D operations.
 *
 * Design:
 *  - Wraps an Array<T> view, APIs require 2x2 at call sites.
 *  - Adds matrix helpers optimized for 2x2 (always uses naive formulas).
 *  - Useful for 2D transformations, planar control, and small covariance.
 *
 * @note RT-SAFE: All operations noexcept. Inverse/determinant use closed-form
 *       formulas (no LAPACK overhead for 2x2).
 */

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace linalg {

// Forward declaration.
template <typename T> class Vector;

/* -------------------------------- Matrix2 --------------------------------- */

/**
 * @brief Strong 2x2 matrix type wrapping Array.
 *
 * @tparam T Element type (float or double).
 *
 * @note RT-SAFE: All operations noexcept, no allocations (uses naive 2x2 ops).
 */
template <typename T> class Matrix2 {
public:
  using value_type = T;

  static constexpr std::size_t K_ROWS = 2;
  static constexpr std::size_t K_COLS = 2;

  /* --------------------------- Construction ------------------------------ */

  /** @brief Construct from an existing Array view. Enforces 2x2 at call-time. */
  explicit Matrix2(const Array<T>& a) noexcept;

  /** @brief Construct from raw pointer; shape is fixed to 2x2. */
  Matrix2(T* data, Layout layout, std::size_t ld = 0) noexcept;

  /* ----------------------------- Accessors ------------------------------- */

  std::size_t rows() const noexcept;
  std::size_t cols() const noexcept;
  std::size_t ld() const noexcept;
  Layout layout() const noexcept;
  T* data() noexcept;
  const T* data() const noexcept;

  /** @brief Underlying Array view (non-owning). */
  Array<T>& view() noexcept { return a_; }
  const Array<T>& view() const noexcept { return a_; }

  /* -------------------------- Matrix Helpers ----------------------------- */

  /** @brief out = trace(A); requires 2x2. RT-SAFE. */
  uint8_t traceInto(T& out) const noexcept;

  /** @brief out := A^T (view-only transpose). RT-SAFE. */
  Matrix2<T> transposeView() const noexcept;

  /** @brief C = alpha * A * B + beta * C (naive 2x2 GEMM). RT-SAFE. */
  uint8_t gemmInto(const Matrix2<T>& b, Matrix2<T>& c, T alpha = T(1),
                   T beta = T(0)) const noexcept;

  /** @brief det(A) = a*d - b*c. RT-SAFE. */
  uint8_t determinantInto(T& out) const noexcept;

  /** @brief A := A^{-1} via closed-form 2x2 formula. RT-SAFE. */
  uint8_t inverseInPlace() noexcept;

  /** @brief Solve Ax = b via Cramer's rule. x and b are 2-vectors. RT-SAFE. */
  uint8_t solveInto(const Vector<T>& b, Vector<T>& x) const noexcept;

  /* --------------------- Elementwise Arithmetic -------------------------- */

  /** @brief out := A + B (elementwise). RT-SAFE. */
  uint8_t addInto(const Matrix2<T>& b, Matrix2<T>& out) const noexcept;

  /** @brief out := A - B (elementwise). RT-SAFE. */
  uint8_t subInto(const Matrix2<T>& b, Matrix2<T>& out) const noexcept;

  /** @brief out := alpha * A (elementwise scale). RT-SAFE. */
  uint8_t scaleInto(T alpha, Matrix2<T>& out) const noexcept;

  /** @brief Y := alpha * A + Y (AXPY-style). RT-SAFE. */
  uint8_t axpyInto(T alpha, Matrix2<T>& y) const noexcept;

  /** @brief out := A * B (Hadamard/elementwise product). RT-SAFE. */
  uint8_t hadamardInto(const Matrix2<T>& b, Matrix2<T>& out) const noexcept;

  /* ----------------------- Matrix-Vector Helpers ------------------------- */

  /** @brief y := A * x (column-vector form). RT-SAFE. */
  uint8_t multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;

  /** @brief y := A^T * x (column-vector form). RT-SAFE. */
  uint8_t transposeMultiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;

private:
  static inline bool is2x2(const Array<T>& v) noexcept;
  uint8_t require2x2() const noexcept;

  Array<T> a_;
};

} // namespace linalg
} // namespace math
} // namespace apex

#include "src/utilities/math/linalg/src/Matrix2.tpp"

#endif // APEX_MATH_LINALG_MATRIX2_HPP
