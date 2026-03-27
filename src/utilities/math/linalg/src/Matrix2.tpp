/**
 * @file Matrix2.tpp
 * @brief Template implementation for Matrix2.
 *
 * All operations use closed-form formulas optimized for 2x2 matrices.
 * BLAS/LAPACK overhead is not worth it for fixed small sizes.
 */
#ifndef APEX_MATH_LINALG_MATRIX2_TPP
#define APEX_MATH_LINALG_MATRIX2_TPP

#include "src/utilities/math/linalg/inc/ArrayOps.hpp"
#include "src/utilities/math/linalg/inc/Matrix2.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"

#include <cmath>
#include <limits>

namespace apex {
namespace math {
namespace linalg {

/* ---------------------- Constructors / Accessors ------------------------- */

template <typename T> Matrix2<T>::Matrix2(const Array<T>& a) noexcept : a_(a) {
  (void)require2x2();
}

template <typename T>
Matrix2<T>::Matrix2(T* data, Layout layout, std::size_t ld) noexcept
    : a_(data, K_ROWS, K_COLS, layout, ld) {
  (void)require2x2();
}

template <typename T> std::size_t Matrix2<T>::rows() const noexcept { return a_.rows(); }

template <typename T> std::size_t Matrix2<T>::cols() const noexcept { return a_.cols(); }

template <typename T> std::size_t Matrix2<T>::ld() const noexcept { return a_.ld(); }

template <typename T> Layout Matrix2<T>::layout() const noexcept { return a_.layout(); }

template <typename T> T* Matrix2<T>::data() noexcept { return a_.data(); }

template <typename T> const T* Matrix2<T>::data() const noexcept { return a_.data(); }

/* -------------------------- Private Helpers ------------------------------ */

template <typename T> bool Matrix2<T>::is2x2(const Array<T>& v) noexcept {
  return (v.rows() == K_ROWS) && (v.cols() == K_COLS);
}

template <typename T> uint8_t Matrix2<T>::require2x2() const noexcept {
  return is2x2(a_) ? static_cast<uint8_t>(Status::SUCCESS)
                   : static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
}

/* --------------------------- Matrix Helpers ------------------------------ */

template <typename T> uint8_t Matrix2<T>::traceInto(T& out) const noexcept {
  const uint8_t S = require2x2();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }
  out = a_(0, 0) + a_(1, 1);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> Matrix2<T> Matrix2<T>::transposeView() const noexcept {
  auto t = a_.transposeView();
  Array<T> v(t.data(), t.rows(), t.cols(), t.layout(), t.ld());
  return Matrix2<T>(v);
}

template <typename T>
uint8_t Matrix2<T>::gemmInto(const Matrix2<T>& b, Matrix2<T>& c, T alpha, T beta) const noexcept {
  if (!is2x2(a_) || !is2x2(b.a_) || !is2x2(c.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Fully unrolled 2x2 GEMM with direct pointer access
  const T* A = a_.data();
  const T* B = b.a_.data();
  T* C = c.a_.data();

  // Cache A values (4 loads, reused 2x each)
  const T A00 = A[0], A01 = A[1];
  const T A10 = A[2], A11 = A[3];

  // Cache B values (4 loads, reused 2x each)
  const T B00 = B[0], B01 = B[1];
  const T B10 = B[2], B11 = B[3];

  if (beta == T(0)) {
    // Common case: C = alpha * A * B (no accumulate)
    C[0] = alpha * (A00 * B00 + A01 * B10);
    C[1] = alpha * (A00 * B01 + A01 * B11);
    C[2] = alpha * (A10 * B00 + A11 * B10);
    C[3] = alpha * (A10 * B01 + A11 * B11);
  } else {
    // General case: C = alpha * A * B + beta * C
    C[0] = alpha * (A00 * B00 + A01 * B10) + beta * C[0];
    C[1] = alpha * (A00 * B01 + A01 * B11) + beta * C[1];
    C[2] = alpha * (A10 * B00 + A11 * B10) + beta * C[2];
    C[3] = alpha * (A10 * B01 + A11 * B11) + beta * C[3];
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Matrix2<T>::determinantInto(T& out) const noexcept {
  const uint8_t S = require2x2();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  // 2x2 determinant: ad - bc
  out = a_(0, 0) * a_(1, 1) - a_(0, 1) * a_(1, 0);

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Matrix2<T>::inverseInPlace() noexcept {
  const uint8_t S = require2x2();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  // Direct pointer access for better cache behavior
  T* M = a_.data();

  // Cache original values (single memory read per element)
  const T A = M[0], B = M[1];
  const T C = M[2], D = M[3];

  // Determinant: ad - bc
  const T DET = A * D - B * C;

  if (std::abs(DET) < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }

  const T INV_DET = T(1) / DET;

  // 2x2 inverse formula: (1/det) * [d, -b; -c, a]
  M[0] = D * INV_DET;
  M[1] = -B * INV_DET;
  M[2] = -C * INV_DET;
  M[3] = A * INV_DET;

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Matrix2<T>::solveInto(const Vector<T>& b, Vector<T>& x) const noexcept {
  const uint8_t S = require2x2();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (b.size() != 2 || x.size() != 2) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Solve Ax = b via Cramer's rule (no allocation, RT-safe)
  const T* A = a_.data();
  const T* bv = b.data();
  T* xv = x.data();

  // Matrix elements
  const T A00 = A[0], A01 = A[1];
  const T A10 = A[2], A11 = A[3];

  // Determinant
  const T DET = A00 * A11 - A01 * A10;

  if (std::abs(DET) < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }

  const T INV_DET = T(1) / DET;

  // Cramer's rule: x_i = det(A_i) / det(A)
  // x[0] = (b[0]*A11 - b[1]*A01) / det
  // x[1] = (A00*b[1] - A10*b[0]) / det
  xv[0] = (bv[0] * A11 - bv[1] * A01) * INV_DET;
  xv[1] = (A00 * bv[1] - A10 * bv[0]) * INV_DET;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------- Elementwise Arithmetic -------------------------- */

template <typename T>
uint8_t Matrix2<T>::addInto(const Matrix2<T>& b, Matrix2<T>& out) const noexcept {
  if (!is2x2(a_) || !is2x2(b.a_) || !is2x2(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::addInto(this->a_, b.a_, out.a_);
}

template <typename T>
uint8_t Matrix2<T>::subInto(const Matrix2<T>& b, Matrix2<T>& out) const noexcept {
  if (!is2x2(a_) || !is2x2(b.a_) || !is2x2(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::subInto(this->a_, b.a_, out.a_);
}

template <typename T> uint8_t Matrix2<T>::scaleInto(T alpha, Matrix2<T>& out) const noexcept {
  if (!is2x2(a_) || !is2x2(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::scaleInto(this->a_, alpha, out.a_);
}

template <typename T> uint8_t Matrix2<T>::axpyInto(T alpha, Matrix2<T>& y) const noexcept {
  if (!is2x2(a_) || !is2x2(y.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::axpyInto(alpha, this->a_, y.a_);
}

template <typename T>
uint8_t Matrix2<T>::hadamardInto(const Matrix2<T>& b, Matrix2<T>& out) const noexcept {
  if (!is2x2(a_) || !is2x2(b.a_) || !is2x2(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::hadamardInto(this->a_, b.a_, out.a_);
}

/* ----------------------- Matrix-Vector Helpers --------------------------- */

template <typename T>
uint8_t Matrix2<T>::multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept {
  const uint8_t S = require2x2();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (x.orient() != VectorOrient::Col || y.orient() != VectorOrient::Col) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  if (x.size() != 2 || y.size() != 2) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Naive 2x2 * 2x1 multiplication
  const T* xd = x.data();
  T* yd = y.data();

  yd[0] = a_(0, 0) * xd[0] + a_(0, 1) * xd[1];
  yd[1] = a_(1, 0) * xd[0] + a_(1, 1) * xd[1];

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Matrix2<T>::transposeMultiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept {
  const uint8_t S = require2x2();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (x.orient() != VectorOrient::Col || y.orient() != VectorOrient::Col) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  if (x.size() != 2 || y.size() != 2) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Naive A^T * x
  const T* xd = x.data();
  T* yd = y.data();

  yd[0] = a_(0, 0) * xd[0] + a_(1, 0) * xd[1];
  yd[1] = a_(0, 1) * xd[0] + a_(1, 1) * xd[1];

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_MATRIX2_TPP
