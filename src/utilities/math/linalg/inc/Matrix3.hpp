#ifndef APEX_MATH_LINALG_MATRIX3_HPP
#define APEX_MATH_LINALG_MATRIX3_HPP
/**
 * @file Matrix3.hpp
 * @brief Strong 3x3 matrix (composition over Array) for DCM operations.
 *
 * Design:
 *  - Wraps an Array<T> view, APIs require 3x3 at call sites.
 *  - Adds matrix helpers optimized for 3x3 (always uses naive loops).
 *  - Useful for DCM (Direction Cosine Matrix) operations.
 *
 * @note RT-SAFE: All operations noexcept. Inverse/determinant use naive
 *       implementations (no LAPACK overhead for 3x3).
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

/* -------------------------------- Matrix3 --------------------------------- */

/**
 * @brief Strong 3x3 matrix type wrapping Array.
 *
 * @tparam T Element type (float or double).
 *
 * @note RT-SAFE: All operations noexcept, no allocations (uses naive 3x3 ops).
 */
template <typename T> class Matrix3 {
public:
  using value_type = T;

  static constexpr std::size_t K_ROWS = 3;
  static constexpr std::size_t K_COLS = 3;

  /* --------------------------- Construction ------------------------------ */

  /** @brief Construct from an existing Array view. Enforces 3x3 at call-time. */
  explicit Matrix3(const Array<T>& a) noexcept;

  /** @brief Construct from raw pointer; shape is fixed to 3x3. */
  Matrix3(T* data, Layout layout, std::size_t ld = 0) noexcept;

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

  /** @brief out = trace(A); requires 3x3. RT-SAFE. */
  uint8_t traceInto(T& out) const noexcept;

  /** @brief out := A^T (view-only transpose). RT-SAFE. */
  Matrix3<T> transposeView() const noexcept;

  /** @brief C = alpha * A * B + beta * C (naive 3x3 GEMM). RT-SAFE. */
  uint8_t gemmInto(const Matrix3<T>& b, Matrix3<T>& c, T alpha = T(1),
                   T beta = T(0)) const noexcept;

  /** @brief det(A) via naive 3x3 formula. RT-SAFE. */
  uint8_t determinantInto(T& out) const noexcept;

  /** @brief A := A^{-1} via naive 3x3 formula. RT-SAFE. */
  uint8_t inverseInPlace() noexcept;

  /* --------------------- Elementwise Arithmetic -------------------------- */

  /** @brief out := A + B (elementwise). RT-SAFE. */
  uint8_t addInto(const Matrix3<T>& b, Matrix3<T>& out) const noexcept;

  /** @brief out := A - B (elementwise). RT-SAFE. */
  uint8_t subInto(const Matrix3<T>& b, Matrix3<T>& out) const noexcept;

  /** @brief out := alpha * A (elementwise scale). RT-SAFE. */
  uint8_t scaleInto(T alpha, Matrix3<T>& out) const noexcept;

  /** @brief Y := alpha * A + Y (AXPY-style). RT-SAFE. */
  uint8_t axpyInto(T alpha, Matrix3<T>& y) const noexcept;

  /** @brief out := A * B (Hadamard/elementwise product). RT-SAFE. */
  uint8_t hadamardInto(const Matrix3<T>& b, Matrix3<T>& out) const noexcept;

  /* ----------------------- Matrix-Vector Helpers ------------------------- */

  /** @brief y := A * x (column-vector form). RT-SAFE. */
  uint8_t multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;

  /** @brief y := A^T * x (column-vector form). RT-SAFE. */
  uint8_t transposeMultiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;

private:
  static inline bool is3x3(const Array<T>& v) noexcept;
  uint8_t require3x3() const noexcept;

  Array<T> a_;
};

} // namespace linalg
} // namespace math
} // namespace apex

#include "src/utilities/math/linalg/src/Matrix3.tpp"

#endif // APEX_MATH_LINALG_MATRIX3_HPP
