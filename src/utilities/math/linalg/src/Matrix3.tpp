/**
 * @file Matrix3.tpp
 * @brief Template implementation for Matrix3.
 *
 * All operations use naive implementations optimized for 3x3 matrices.
 * BLAS/LAPACK overhead is not worth it for fixed small sizes.
 */
#ifndef APEX_MATH_LINALG_MATRIX3_TPP
#define APEX_MATH_LINALG_MATRIX3_TPP

#include "src/utilities/math/linalg/inc/ArrayOps.hpp"
#include "src/utilities/math/linalg/inc/Matrix3.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"

#include <cmath>
#include <limits>

namespace apex {
namespace math {
namespace linalg {

/* ---------------------- Constructors / Accessors ------------------------- */

template <typename T> Matrix3<T>::Matrix3(const Array<T>& a) noexcept : a_(a) {
  (void)require3x3();
}

template <typename T>
Matrix3<T>::Matrix3(T* data, Layout layout, std::size_t ld) noexcept
    : a_(data, K_ROWS, K_COLS, layout, ld) {
  (void)require3x3();
}

template <typename T> std::size_t Matrix3<T>::rows() const noexcept { return a_.rows(); }

template <typename T> std::size_t Matrix3<T>::cols() const noexcept { return a_.cols(); }

template <typename T> std::size_t Matrix3<T>::ld() const noexcept { return a_.ld(); }

template <typename T> Layout Matrix3<T>::layout() const noexcept { return a_.layout(); }

template <typename T> T* Matrix3<T>::data() noexcept { return a_.data(); }

template <typename T> const T* Matrix3<T>::data() const noexcept { return a_.data(); }

/* -------------------------- Private Helpers ------------------------------ */

template <typename T> bool Matrix3<T>::is3x3(const Array<T>& v) noexcept {
  return (v.rows() == K_ROWS) && (v.cols() == K_COLS);
}

template <typename T> uint8_t Matrix3<T>::require3x3() const noexcept {
  return is3x3(a_) ? static_cast<uint8_t>(Status::SUCCESS)
                   : static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
}

/* --------------------------- Matrix Helpers ------------------------------ */

template <typename T> uint8_t Matrix3<T>::traceInto(T& out) const noexcept {
  const uint8_t S = require3x3();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }
  out = a_(0, 0) + a_(1, 1) + a_(2, 2);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> Matrix3<T> Matrix3<T>::transposeView() const noexcept {
  auto t = a_.transposeView();
  Array<T> v(t.data(), t.rows(), t.cols(), t.layout(), t.ld());
  return Matrix3<T>(v);
}

template <typename T>
uint8_t Matrix3<T>::gemmInto(const Matrix3<T>& b, Matrix3<T>& c, T alpha, T beta) const noexcept {
  if (!is3x3(a_) || !is3x3(b.a_) || !is3x3(c.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Fully unrolled 3x3 GEMM with direct pointer access
  // Avoids loop overhead and operator() calls for maximum performance
  const T* A = a_.data();
  const T* B = b.a_.data();
  T* C = c.a_.data();

  // Cache A values (9 loads, reused 3x each)
  const T A00 = A[0], A01 = A[1], A02 = A[2];
  const T A10 = A[3], A11 = A[4], A12 = A[5];
  const T A20 = A[6], A21 = A[7], A22 = A[8];

  // Cache B values (9 loads, reused 3x each)
  const T B00 = B[0], B01 = B[1], B02 = B[2];
  const T B10 = B[3], B11 = B[4], B12 = B[5];
  const T B20 = B[6], B21 = B[7], B22 = B[8];

  if (beta == T(0)) {
    // Common case: C = alpha * A * B (no accumulate)
    C[0] = alpha * (A00 * B00 + A01 * B10 + A02 * B20);
    C[1] = alpha * (A00 * B01 + A01 * B11 + A02 * B21);
    C[2] = alpha * (A00 * B02 + A01 * B12 + A02 * B22);

    C[3] = alpha * (A10 * B00 + A11 * B10 + A12 * B20);
    C[4] = alpha * (A10 * B01 + A11 * B11 + A12 * B21);
    C[5] = alpha * (A10 * B02 + A11 * B12 + A12 * B22);

    C[6] = alpha * (A20 * B00 + A21 * B10 + A22 * B20);
    C[7] = alpha * (A20 * B01 + A21 * B11 + A22 * B21);
    C[8] = alpha * (A20 * B02 + A21 * B12 + A22 * B22);
  } else {
    // General case: C = alpha * A * B + beta * C
    C[0] = alpha * (A00 * B00 + A01 * B10 + A02 * B20) + beta * C[0];
    C[1] = alpha * (A00 * B01 + A01 * B11 + A02 * B21) + beta * C[1];
    C[2] = alpha * (A00 * B02 + A01 * B12 + A02 * B22) + beta * C[2];

    C[3] = alpha * (A10 * B00 + A11 * B10 + A12 * B20) + beta * C[3];
    C[4] = alpha * (A10 * B01 + A11 * B11 + A12 * B21) + beta * C[4];
    C[5] = alpha * (A10 * B02 + A11 * B12 + A12 * B22) + beta * C[5];

    C[6] = alpha * (A20 * B00 + A21 * B10 + A22 * B20) + beta * C[6];
    C[7] = alpha * (A20 * B01 + A21 * B11 + A22 * B21) + beta * C[7];
    C[8] = alpha * (A20 * B02 + A21 * B12 + A22 * B22) + beta * C[8];
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Matrix3<T>::determinantInto(T& out) const noexcept {
  const uint8_t S = require3x3();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  // Naive 3x3 determinant formula
  out = a_(0, 0) * (a_(1, 1) * a_(2, 2) - a_(1, 2) * a_(2, 1)) -
        a_(0, 1) * (a_(1, 0) * a_(2, 2) - a_(1, 2) * a_(2, 0)) +
        a_(0, 2) * (a_(1, 0) * a_(2, 1) - a_(1, 1) * a_(2, 0));

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Matrix3<T>::inverseInPlace() noexcept {
  const uint8_t S = require3x3();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  // Direct pointer access for better cache behavior
  T* M = a_.data();

  // Cache original values (single memory read per element)
  const T A00 = M[0], A01 = M[1], A02 = M[2];
  const T A10 = M[3], A11 = M[4], A12 = M[5];
  const T A20 = M[6], A21 = M[7], A22 = M[8];

  // Compute cofactors (reused in both determinant and adjugate)
  const T C00 = A11 * A22 - A12 * A21;
  const T C01 = A12 * A20 - A10 * A22;
  const T C02 = A10 * A21 - A11 * A20;

  // Determinant via first row expansion
  const T DET = A00 * C00 + A01 * C01 + A02 * C02;

  if (std::abs(DET) < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }

  const T INV_DET = T(1) / DET;

  // Compute adjugate transpose / det using direct array access
  M[0] = C00 * INV_DET;
  M[1] = (A02 * A21 - A01 * A22) * INV_DET;
  M[2] = (A01 * A12 - A02 * A11) * INV_DET;
  M[3] = C01 * INV_DET;
  M[4] = (A00 * A22 - A02 * A20) * INV_DET;
  M[5] = (A02 * A10 - A00 * A12) * INV_DET;
  M[6] = C02 * INV_DET;
  M[7] = (A01 * A20 - A00 * A21) * INV_DET;
  M[8] = (A00 * A11 - A01 * A10) * INV_DET;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------- Elementwise Arithmetic -------------------------- */

template <typename T>
uint8_t Matrix3<T>::addInto(const Matrix3<T>& b, Matrix3<T>& out) const noexcept {
  if (!is3x3(a_) || !is3x3(b.a_) || !is3x3(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::addInto(this->a_, b.a_, out.a_);
}

template <typename T>
uint8_t Matrix3<T>::subInto(const Matrix3<T>& b, Matrix3<T>& out) const noexcept {
  if (!is3x3(a_) || !is3x3(b.a_) || !is3x3(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::subInto(this->a_, b.a_, out.a_);
}

template <typename T> uint8_t Matrix3<T>::scaleInto(T alpha, Matrix3<T>& out) const noexcept {
  if (!is3x3(a_) || !is3x3(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::scaleInto(this->a_, alpha, out.a_);
}

template <typename T> uint8_t Matrix3<T>::axpyInto(T alpha, Matrix3<T>& y) const noexcept {
  if (!is3x3(a_) || !is3x3(y.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::axpyInto(alpha, this->a_, y.a_);
}

template <typename T>
uint8_t Matrix3<T>::hadamardInto(const Matrix3<T>& b, Matrix3<T>& out) const noexcept {
  if (!is3x3(a_) || !is3x3(b.a_) || !is3x3(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::hadamardInto(this->a_, b.a_, out.a_);
}

/* ----------------------- Matrix-Vector Helpers --------------------------- */

template <typename T>
uint8_t Matrix3<T>::multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept {
  const uint8_t S = require3x3();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (x.orient() != VectorOrient::Col || y.orient() != VectorOrient::Col) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  if (x.size() != 3 || y.size() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Naive 3x3 * 3x1 multiplication
  const T* xd = x.data();
  T* yd = y.data();

  yd[0] = a_(0, 0) * xd[0] + a_(0, 1) * xd[1] + a_(0, 2) * xd[2];
  yd[1] = a_(1, 0) * xd[0] + a_(1, 1) * xd[1] + a_(1, 2) * xd[2];
  yd[2] = a_(2, 0) * xd[0] + a_(2, 1) * xd[1] + a_(2, 2) * xd[2];

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Matrix3<T>::transposeMultiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept {
  const uint8_t S = require3x3();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (x.orient() != VectorOrient::Col || y.orient() != VectorOrient::Col) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  if (x.size() != 3 || y.size() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Naive A^T * x
  const T* xd = x.data();
  T* yd = y.data();

  yd[0] = a_(0, 0) * xd[0] + a_(1, 0) * xd[1] + a_(2, 0) * xd[2];
  yd[1] = a_(0, 1) * xd[0] + a_(1, 1) * xd[1] + a_(2, 1) * xd[2];
  yd[2] = a_(0, 2) * xd[0] + a_(1, 2) * xd[1] + a_(2, 2) * xd[2];

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_MATRIX3_TPP
