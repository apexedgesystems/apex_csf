#ifndef APEX_MATH_LINALG_ARRAY_OPS_HPP
#define APEX_MATH_LINALG_ARRAY_OPS_HPP
/**
 * @file ArrayOps.hpp
 * @brief Header-only arithmetic and utility operations on ArrayBase/Array views.
 *
 * Design goals:
 *  - Header-only, inline, no allocations, no exceptions.
 *  - Return uint8_t status codes.
 *  - Layout-agnostic (works for row/col, tight or padded).
 *  - Works with any ArrayBase<T, Derived>.
 *
 * Conventions:
 *  - Aliasing: Elementwise ops permit output to alias inputs.
 *  - Zero-size: setIdentity(0x0) succeeds (no-op); trace(0x0)=0; det(0x0)=1.
 *
 * @note RT-SAFE: All functions are inline, no allocations.
 */

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayBase.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace apex {
namespace math {
namespace linalg {

/* ------------------------------ Type Guard ------------------------------- */

template <typename T> inline constexpr void requireArithmetic() noexcept {
  static_assert(std::is_arithmetic_v<T>, "ArrayOps requires arithmetic T");
}

/* ----------------------------- Shape Check ------------------------------- */

template <typename A, typename B> constexpr bool sameShape(const A& a, const B& b) noexcept {
  return (a.rows() == b.rows()) && (a.cols() == b.cols());
}

/* --------------------------------- Copy ---------------------------------- */

/**
 * @brief C = A (element-wise copy).
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DA, class DC>
inline uint8_t copyInto(const ArrayBase<T, DA>& src, ArrayBase<T, DC>& dst) noexcept {
  requireArithmetic<T>();
  if (!sameShape(src, dst)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  for (std::size_t r = 0; r < src.rows(); ++r) {
    for (std::size_t c = 0; c < src.cols(); ++c) {
      dst(r, c) = src(r, c);
    }
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------------- Set Operations ---------------------------- */

/**
 * @brief dst = constant.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DD> inline uint8_t setConstant(ArrayBase<T, DD>& dst, T v) noexcept {
  requireArithmetic<T>();
  for (std::size_t r = 0; r < dst.rows(); ++r) {
    for (std::size_t c = 0; c < dst.cols(); ++c) {
      dst(r, c) = v;
    }
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief dst = 0.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DD> inline uint8_t setZeros(ArrayBase<T, DD>& dst) noexcept {
  requireArithmetic<T>();
  return setConstant(dst, T(0));
}

/**
 * @brief dst = 1.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DD> inline uint8_t setOnes(ArrayBase<T, DD>& dst) noexcept {
  requireArithmetic<T>();
  return setConstant(dst, T(1));
}

/**
 * @brief dst = Identity (square only).
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DD> inline uint8_t setIdentity(ArrayBase<T, DD>& dst) noexcept {
  requireArithmetic<T>();
  if (dst.rows() != dst.cols()) {
    return static_cast<uint8_t>(Status::ERROR_NOT_SQUARE);
  }
  (void)setZeros(dst);
  for (std::size_t i = 0; i < dst.rows(); ++i) {
    dst(i, i) = T(1);
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------- Elementwise Arithmetic ------------------------ */

/**
 * @brief C = A + B (element-wise).
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DA, class DB, class DC>
inline uint8_t addInto(const ArrayBase<T, DA>& a, const ArrayBase<T, DB>& b,
                       ArrayBase<T, DC>& c) noexcept {
  requireArithmetic<T>();
  if (!sameShape(a, b) || !sameShape(a, c)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  for (std::size_t r = 0; r < a.rows(); ++r) {
    for (std::size_t col = 0; col < a.cols(); ++col) {
      c(r, col) = a(r, col) + b(r, col);
    }
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief C = A - B (element-wise).
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DA, class DB, class DC>
inline uint8_t subInto(const ArrayBase<T, DA>& a, const ArrayBase<T, DB>& b,
                       ArrayBase<T, DC>& c) noexcept {
  requireArithmetic<T>();
  if (!sameShape(a, b) || !sameShape(a, c)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  for (std::size_t r = 0; r < a.rows(); ++r) {
    for (std::size_t col = 0; col < a.cols(); ++col) {
      c(r, col) = a(r, col) - b(r, col);
    }
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief B = alpha * A (element-wise scale).
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DA, class DB>
inline uint8_t scaleInto(const ArrayBase<T, DA>& a, T alpha, ArrayBase<T, DB>& b) noexcept {
  requireArithmetic<T>();
  if (!sameShape(a, b)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  for (std::size_t r = 0; r < a.rows(); ++r) {
    for (std::size_t col = 0; col < a.cols(); ++col) {
      b(r, col) = alpha * a(r, col);
    }
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief Y := alpha * X + Y (element-wise AXPY).
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DX, class DY>
inline uint8_t axpyInto(T alpha, const ArrayBase<T, DX>& x, ArrayBase<T, DY>& y) noexcept {
  requireArithmetic<T>();
  if (!sameShape(x, y)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  for (std::size_t r = 0; r < x.rows(); ++r) {
    for (std::size_t c = 0; c < x.cols(); ++c) {
      y(r, c) = static_cast<T>(alpha * x(r, c) + y(r, c));
    }
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief C = A * B (Hadamard/element-wise product).
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DA, class DB, class DC>
inline uint8_t hadamardInto(const ArrayBase<T, DA>& a, const ArrayBase<T, DB>& b,
                            ArrayBase<T, DC>& c) noexcept {
  requireArithmetic<T>();
  if (!sameShape(a, b) || !sameShape(a, c)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  for (std::size_t r = 0; r < a.rows(); ++r) {
    for (std::size_t col = 0; col < a.cols(); ++col) {
      c(r, col) = a(r, col) * b(r, col);
    }
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------------- Matrix Multiply --------------------------- */

/**
 * @brief C = alpha * A * B + beta * C (thin wrapper over GEMM).
 *
 * Shapes: A(m x k), B(k x n), C(m x n).
 *
 * @note RT-SAFE: No allocation.
 */
template <typename T>
inline uint8_t matmulInto(const Array<T>& a, const Array<T>& b, Array<T>& c, T alpha = T(1),
                          T beta = T(0)) noexcept {
  requireArithmetic<T>();
  return a.gemmInto(b, c, alpha, beta);
}

/* --------------------------------- Trace --------------------------------- */

/**
 * @brief trace(A) -> out.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class D> inline uint8_t traceInto(const ArrayBase<T, D>& a, T& out) noexcept {
  requireArithmetic<T>();
  if (a.rows() != a.cols()) {
    return static_cast<uint8_t>(Status::ERROR_NOT_SQUARE);
  }
  T t = T(0);
  for (std::size_t i = 0; i < a.rows(); ++i) {
    t += a(i, i);
  }
  out = t;
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------------ Determinant ------------------------------ */

/**
 * @brief determinant(A) -> out (fallback without LAPACK; only 0x0 and 1x1).
 *
 * For n > 1, prefer Array<T>::determinant.
 *
 * @note RT-SAFE: No allocation.
 */
template <typename T, class D>
inline uint8_t determinantInto(const ArrayBase<T, D>& a, T& out) noexcept {
  requireArithmetic<T>();
  if (a.rows() != a.cols()) {
    return static_cast<uint8_t>(Status::ERROR_NOT_SQUARE);
  }
  if (a.rows() == 0) {
    out = T(1);
    return static_cast<uint8_t>(Status::SUCCESS);
  }
  if (a.rows() == 1) {
    out = a(0, 0);
    return static_cast<uint8_t>(Status::SUCCESS);
  }
  (void)out;
  return static_cast<uint8_t>(Status::ERROR_LIB_FAILURE);
}

/* --------------------------- Skew-Symmetric Matrix ------------------------ */

/**
 * @brief Construct skew-symmetric matrix from 3-vector: out = [v]_x.
 *
 * Given v = [x, y, z], produces the 3x3 matrix:
 *   [  0  -z   y ]
 *   [  z   0  -x ]
 *   [ -y   x   0 ]
 *
 * Such that [v]_x * u = v x u (cross product).
 *
 * @param v 3-element array (vector).
 * @param out 3x3 output matrix.
 * @return Status::SUCCESS on success, ERROR_SIZE_MISMATCH otherwise.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DV, class DM>
inline uint8_t skew3Into(const ArrayBase<T, DV>& v, ArrayBase<T, DM>& out) noexcept {
  requireArithmetic<T>();

  // Validate vector is 3 elements (3x1 or 1x3)
  const std::size_t VSIZE = v.rows() * v.cols();
  if (VSIZE != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Validate output is 3x3
  if (out.rows() != 3 || out.cols() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Extract vector components
  const T X = (v.rows() == 3) ? v(0, 0) : v(0, 0);
  const T Y = (v.rows() == 3) ? v(1, 0) : v(0, 1);
  const T Z = (v.rows() == 3) ? v(2, 0) : v(0, 2);

  // Build skew-symmetric matrix
  out(0, 0) = T(0);
  out(0, 1) = -Z;
  out(0, 2) = Y;
  out(1, 0) = Z;
  out(1, 1) = T(0);
  out(1, 2) = -X;
  out(2, 0) = -Y;
  out(2, 1) = X;
  out(2, 2) = T(0);

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------------ Outer Product ----------------------------- */

/**
 * @brief Outer product: C = a * b^T.
 *
 * Given column vectors a (m x 1) and b (n x 1), produces m x n matrix.
 * Also works with row vectors (1 x m) and (1 x n).
 *
 * @param a First vector (m elements).
 * @param b Second vector (n elements).
 * @param c Output matrix (m x n).
 * @return Status::SUCCESS on success, ERROR_SIZE_MISMATCH otherwise.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class DA, class DB, class DC>
inline uint8_t outerInto(const ArrayBase<T, DA>& a, const ArrayBase<T, DB>& b,
                         ArrayBase<T, DC>& c) noexcept {
  requireArithmetic<T>();

  // Get vector sizes (handle both row and column vectors)
  const std::size_t M = a.rows() * a.cols();
  const std::size_t N = b.rows() * b.cols();

  // Validate output dimensions
  if (c.rows() != M || c.cols() != N) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Access vectors linearly
  const bool A_COL = (a.cols() == 1);
  const bool B_COL = (b.cols() == 1);

  for (std::size_t i = 0; i < M; ++i) {
    const T AI = A_COL ? a(i, 0) : a(0, i);
    for (std::size_t j = 0; j < N; ++j) {
      const T BJ = B_COL ? b(j, 0) : b(0, j);
      c(i, j) = AI * BJ;
    }
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------------ Matrix Norms ------------------------------ */

/**
 * @brief Frobenius norm: ||A||_F = sqrt(sum of A_ij^2).
 *
 * @param a Input matrix.
 * @param out Output scalar (Frobenius norm).
 * @return Status::SUCCESS.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class D>
inline uint8_t frobeniusNormInto(const ArrayBase<T, D>& a, T& out) noexcept {
  requireArithmetic<T>();

  T sum = T(0);
  for (std::size_t r = 0; r < a.rows(); ++r) {
    for (std::size_t c = 0; c < a.cols(); ++c) {
      const T VAL = a(r, c);
      sum += VAL * VAL;
    }
  }
  out = std::sqrt(sum);

  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief Infinity norm: ||A||_inf = max row sum of |A_ij|.
 *
 * @param a Input matrix.
 * @param out Output scalar (infinity norm).
 * @return Status::SUCCESS.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class D>
inline uint8_t infNormInto(const ArrayBase<T, D>& a, T& out) noexcept {
  requireArithmetic<T>();

  T maxRowSum = T(0);
  for (std::size_t r = 0; r < a.rows(); ++r) {
    T rowSum = T(0);
    for (std::size_t c = 0; c < a.cols(); ++c) {
      rowSum += std::abs(a(r, c));
    }
    if (rowSum > maxRowSum) {
      maxRowSum = rowSum;
    }
  }
  out = maxRowSum;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief One norm: ||A||_1 = max column sum of |A_ij|.
 *
 * @param a Input matrix.
 * @param out Output scalar (one norm).
 * @return Status::SUCCESS.
 * @note RT-SAFE: No allocation.
 */
template <typename T, class D>
inline uint8_t oneNormInto(const ArrayBase<T, D>& a, T& out) noexcept {
  requireArithmetic<T>();

  T maxColSum = T(0);
  for (std::size_t c = 0; c < a.cols(); ++c) {
    T colSum = T(0);
    for (std::size_t r = 0; r < a.rows(); ++r) {
      colSum += std::abs(a(r, c));
    }
    if (colSum > maxColSum) {
      maxColSum = colSum;
    }
  }
  out = maxColSum;

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_ARRAY_OPS_HPP
