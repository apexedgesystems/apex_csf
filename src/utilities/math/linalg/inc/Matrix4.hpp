#ifndef APEX_MATH_LINALG_MATRIX4_HPP
#define APEX_MATH_LINALG_MATRIX4_HPP
/**
 * @file Matrix4.hpp
 * @brief Strong 4x4 matrix (composition over Array) for homogeneous transforms.
 *
 * Design:
 *  - Wraps an Array<T> view, APIs require 4x4 at call sites.
 *  - Adds matrix helpers optimized for 4x4 (always uses naive formulas).
 *  - Useful for homogeneous transformations (robotics, graphics, navigation).
 *
 * @note RT-SAFE: All operations noexcept. Inverse/determinant use closed-form
 *       formulas (no LAPACK overhead for 4x4).
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

/* -------------------------------- Matrix4 --------------------------------- */

/**
 * @brief Strong 4x4 matrix type wrapping Array.
 *
 * @tparam T Element type (float or double).
 *
 * @note RT-SAFE: All operations noexcept, no allocations (uses naive 4x4 ops).
 */
template <typename T> class Matrix4 {
public:
  using value_type = T;

  static constexpr std::size_t K_ROWS = 4;
  static constexpr std::size_t K_COLS = 4;

  /* --------------------------- Construction ------------------------------ */

  /** @brief Construct from an existing Array view. Enforces 4x4 at call-time. */
  explicit Matrix4(const Array<T>& a) noexcept;

  /** @brief Construct from raw pointer; shape is fixed to 4x4. */
  Matrix4(T* data, Layout layout, std::size_t ld = 0) noexcept;

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

  /** @brief out = trace(A); requires 4x4. RT-SAFE. */
  uint8_t traceInto(T& out) const noexcept;

  /** @brief out := A^T (view-only transpose). RT-SAFE. */
  Matrix4<T> transposeView() const noexcept;

  /** @brief C = alpha * A * B + beta * C (naive 4x4 GEMM). RT-SAFE. */
  uint8_t gemmInto(const Matrix4<T>& b, Matrix4<T>& c, T alpha = T(1),
                   T beta = T(0)) const noexcept;

  /** @brief det(A) via cofactor expansion. RT-SAFE. */
  uint8_t determinantInto(T& out) const noexcept;

  /** @brief A := A^{-1} via adjugate method. RT-SAFE. */
  uint8_t inverseInPlace() noexcept;

  /** @brief Solve Ax = b via Cramer's rule. x and b are 4-vectors. RT-SAFE. */
  uint8_t solveInto(const Vector<T>& b, Vector<T>& x) const noexcept;

  /* --------------------- Elementwise Arithmetic -------------------------- */

  /** @brief out := A + B (elementwise). RT-SAFE. */
  uint8_t addInto(const Matrix4<T>& b, Matrix4<T>& out) const noexcept;

  /** @brief out := A - B (elementwise). RT-SAFE. */
  uint8_t subInto(const Matrix4<T>& b, Matrix4<T>& out) const noexcept;

  /** @brief out := alpha * A (elementwise scale). RT-SAFE. */
  uint8_t scaleInto(T alpha, Matrix4<T>& out) const noexcept;

  /** @brief Y := alpha * A + Y (AXPY-style). RT-SAFE. */
  uint8_t axpyInto(T alpha, Matrix4<T>& y) const noexcept;

  /** @brief out := A * B (Hadamard/elementwise product). RT-SAFE. */
  uint8_t hadamardInto(const Matrix4<T>& b, Matrix4<T>& out) const noexcept;

  /* ----------------------- Matrix-Vector Helpers ------------------------- */

  /** @brief y := A * x (column-vector form). RT-SAFE. */
  uint8_t multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;

  /** @brief y := A^T * x (column-vector form). RT-SAFE. */
  uint8_t transposeMultiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept;

private:
  static inline bool is4x4(const Array<T>& v) noexcept;
  uint8_t require4x4() const noexcept;

  /** @brief Compute 3x3 determinant from elements. */
  static inline T det3(T a, T b, T c, T d, T e, T f, T g, T h, T i) noexcept;

  Array<T> a_;
};

} // namespace linalg
} // namespace math
} // namespace apex

#include "src/utilities/math/linalg/src/Matrix4.tpp"

#endif // APEX_MATH_LINALG_MATRIX4_HPP
