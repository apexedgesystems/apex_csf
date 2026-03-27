/**
 * @file Matrix4.tpp
 * @brief Template implementation for Matrix4.
 *
 * All operations use closed-form formulas optimized for 4x4 matrices.
 * The 4x4 inverse uses the adjugate method with 16 cofactor computations.
 * Each cofactor is a 3x3 determinant.
 */
#ifndef APEX_MATH_LINALG_MATRIX4_TPP
#define APEX_MATH_LINALG_MATRIX4_TPP

#include "src/utilities/math/linalg/inc/ArrayOps.hpp"
#include "src/utilities/math/linalg/inc/Matrix4.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"

#include <cmath>
#include <limits>

namespace apex {
namespace math {
namespace linalg {

/* ---------------------- Constructors / Accessors ------------------------- */

template <typename T> Matrix4<T>::Matrix4(const Array<T>& a) noexcept : a_(a) {
  (void)require4x4();
}

template <typename T>
Matrix4<T>::Matrix4(T* data, Layout layout, std::size_t ld) noexcept
    : a_(data, K_ROWS, K_COLS, layout, ld) {
  (void)require4x4();
}

template <typename T> std::size_t Matrix4<T>::rows() const noexcept { return a_.rows(); }

template <typename T> std::size_t Matrix4<T>::cols() const noexcept { return a_.cols(); }

template <typename T> std::size_t Matrix4<T>::ld() const noexcept { return a_.ld(); }

template <typename T> Layout Matrix4<T>::layout() const noexcept { return a_.layout(); }

template <typename T> T* Matrix4<T>::data() noexcept { return a_.data(); }

template <typename T> const T* Matrix4<T>::data() const noexcept { return a_.data(); }

/* -------------------------- Private Helpers ------------------------------ */

template <typename T> bool Matrix4<T>::is4x4(const Array<T>& v) noexcept {
  return (v.rows() == K_ROWS) && (v.cols() == K_COLS);
}

template <typename T> uint8_t Matrix4<T>::require4x4() const noexcept {
  return is4x4(a_) ? static_cast<uint8_t>(Status::SUCCESS)
                   : static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
}

template <typename T> T Matrix4<T>::det3(T a, T b, T c, T d, T e, T f, T g, T h, T i) noexcept {
  return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

/* --------------------------- Matrix Helpers ------------------------------ */

template <typename T> uint8_t Matrix4<T>::traceInto(T& out) const noexcept {
  const uint8_t S = require4x4();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }
  out = a_(0, 0) + a_(1, 1) + a_(2, 2) + a_(3, 3);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> Matrix4<T> Matrix4<T>::transposeView() const noexcept {
  auto t = a_.transposeView();
  Array<T> v(t.data(), t.rows(), t.cols(), t.layout(), t.ld());
  return Matrix4<T>(v);
}

template <typename T>
uint8_t Matrix4<T>::gemmInto(const Matrix4<T>& b, Matrix4<T>& c, T alpha, T beta) const noexcept {
  if (!is4x4(a_) || !is4x4(b.a_) || !is4x4(c.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Naive 4x4 GEMM using operator() to handle layout
  for (std::size_t i = 0; i < 4; ++i) {
    for (std::size_t j = 0; j < 4; ++j) {
      T sum = T(0);
      for (std::size_t k = 0; k < 4; ++k) {
        sum += a_(i, k) * b.a_(k, j);
      }
      if (beta == T(0)) {
        c.a_(i, j) = alpha * sum;
      } else {
        c.a_(i, j) = alpha * sum + beta * c.a_(i, j);
      }
    }
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Matrix4<T>::determinantInto(T& out) const noexcept {
  const uint8_t S = require4x4();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  // Cache elements for efficiency
  const T M00 = a_(0, 0), M01 = a_(0, 1), M02 = a_(0, 2), M03 = a_(0, 3);
  const T M10 = a_(1, 0), M11 = a_(1, 1), M12 = a_(1, 2), M13 = a_(1, 3);
  const T M20 = a_(2, 0), M21 = a_(2, 1), M22 = a_(2, 2), M23 = a_(2, 3);
  const T M30 = a_(3, 0), M31 = a_(3, 1), M32 = a_(3, 2), M33 = a_(3, 3);

  // Cofactors of first row (each is a 3x3 determinant)
  const T C00 = det3(M11, M12, M13, M21, M22, M23, M31, M32, M33);
  const T C01 = det3(M10, M12, M13, M20, M22, M23, M30, M32, M33);
  const T C02 = det3(M10, M11, M13, M20, M21, M23, M30, M31, M33);
  const T C03 = det3(M10, M11, M12, M20, M21, M22, M30, M31, M32);

  // Determinant via first row expansion (alternating signs)
  out = M00 * C00 - M01 * C01 + M02 * C02 - M03 * C03;

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Matrix4<T>::inverseInPlace() noexcept {
  const uint8_t S = require4x4();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  // Cache all 16 elements
  const T M00 = a_(0, 0), M01 = a_(0, 1), M02 = a_(0, 2), M03 = a_(0, 3);
  const T M10 = a_(1, 0), M11 = a_(1, 1), M12 = a_(1, 2), M13 = a_(1, 3);
  const T M20 = a_(2, 0), M21 = a_(2, 1), M22 = a_(2, 2), M23 = a_(2, 3);
  const T M30 = a_(3, 0), M31 = a_(3, 1), M32 = a_(3, 2), M33 = a_(3, 3);

  // Compute all 16 cofactors (each is a 3x3 determinant with sign)
  // Cofactor C[i][j] = (-1)^(i+j) * det(minor[i][j])
  // Adjugate is transpose of cofactor matrix

  // Row 0 cofactors
  const T C00 = det3(M11, M12, M13, M21, M22, M23, M31, M32, M33);
  const T C01 = -det3(M10, M12, M13, M20, M22, M23, M30, M32, M33);
  const T C02 = det3(M10, M11, M13, M20, M21, M23, M30, M31, M33);
  const T C03 = -det3(M10, M11, M12, M20, M21, M22, M30, M31, M32);

  // Row 1 cofactors
  const T C10 = -det3(M01, M02, M03, M21, M22, M23, M31, M32, M33);
  const T C11 = det3(M00, M02, M03, M20, M22, M23, M30, M32, M33);
  const T C12 = -det3(M00, M01, M03, M20, M21, M23, M30, M31, M33);
  const T C13 = det3(M00, M01, M02, M20, M21, M22, M30, M31, M32);

  // Row 2 cofactors
  const T C20 = det3(M01, M02, M03, M11, M12, M13, M31, M32, M33);
  const T C21 = -det3(M00, M02, M03, M10, M12, M13, M30, M32, M33);
  const T C22 = det3(M00, M01, M03, M10, M11, M13, M30, M31, M33);
  const T C23 = -det3(M00, M01, M02, M10, M11, M12, M30, M31, M32);

  // Row 3 cofactors
  const T C30 = -det3(M01, M02, M03, M11, M12, M13, M21, M22, M23);
  const T C31 = det3(M00, M02, M03, M10, M12, M13, M20, M22, M23);
  const T C32 = -det3(M00, M01, M03, M10, M11, M13, M20, M21, M23);
  const T C33 = det3(M00, M01, M02, M10, M11, M12, M20, M21, M22);

  // Determinant via first row
  const T DET = M00 * C00 + M01 * C01 + M02 * C02 + M03 * C03;

  if (std::abs(DET) < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }

  const T INV_DET = T(1) / DET;

  // A^{-1} = adj(A)^T / det = cofactor^T / det
  // Since we computed cofactor row-by-row, adjugate is its transpose
  a_(0, 0) = C00 * INV_DET;
  a_(0, 1) = C10 * INV_DET;
  a_(0, 2) = C20 * INV_DET;
  a_(0, 3) = C30 * INV_DET;

  a_(1, 0) = C01 * INV_DET;
  a_(1, 1) = C11 * INV_DET;
  a_(1, 2) = C21 * INV_DET;
  a_(1, 3) = C31 * INV_DET;

  a_(2, 0) = C02 * INV_DET;
  a_(2, 1) = C12 * INV_DET;
  a_(2, 2) = C22 * INV_DET;
  a_(2, 3) = C32 * INV_DET;

  a_(3, 0) = C03 * INV_DET;
  a_(3, 1) = C13 * INV_DET;
  a_(3, 2) = C23 * INV_DET;
  a_(3, 3) = C33 * INV_DET;

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Matrix4<T>::solveInto(const Vector<T>& b, Vector<T>& x) const noexcept {
  const uint8_t S = require4x4();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (b.size() != 4 || x.size() != 4) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Compute determinant
  T det = T(0);
  (void)determinantInto(det);

  if (std::abs(det) < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }

  // Cache matrix and vector
  const T M00 = a_(0, 0), M01 = a_(0, 1), M02 = a_(0, 2), M03 = a_(0, 3);
  const T M10 = a_(1, 0), M11 = a_(1, 1), M12 = a_(1, 2), M13 = a_(1, 3);
  const T M20 = a_(2, 0), M21 = a_(2, 1), M22 = a_(2, 2), M23 = a_(2, 3);
  const T M30 = a_(3, 0), M31 = a_(3, 1), M32 = a_(3, 2), M33 = a_(3, 3);

  const T B0 = b.data()[0], B1 = b.data()[1], B2 = b.data()[2], B3 = b.data()[3];

  const T INV_DET = T(1) / det;

  // Cramer's rule: x[i] = det(A_i) / det(A)
  // where A_i is A with column i replaced by b

  // x[0]: Replace column 0 with b
  const T DET0 = B0 * det3(M11, M12, M13, M21, M22, M23, M31, M32, M33) -
                 M01 * det3(B1, M12, M13, B2, M22, M23, B3, M32, M33) +
                 M02 * det3(B1, M11, M13, B2, M21, M23, B3, M31, M33) -
                 M03 * det3(B1, M11, M12, B2, M21, M22, B3, M31, M32);

  // x[1]: Replace column 1 with b
  const T DET1 = M00 * det3(B1, M12, M13, B2, M22, M23, B3, M32, M33) -
                 B0 * det3(M10, M12, M13, M20, M22, M23, M30, M32, M33) +
                 M02 * det3(M10, B1, M13, M20, B2, M23, M30, B3, M33) -
                 M03 * det3(M10, B1, M12, M20, B2, M22, M30, B3, M32);

  // x[2]: Replace column 2 with b
  const T DET2 = M00 * det3(M11, B1, M13, M21, B2, M23, M31, B3, M33) -
                 M01 * det3(M10, B1, M13, M20, B2, M23, M30, B3, M33) +
                 B0 * det3(M10, M11, M13, M20, M21, M23, M30, M31, M33) -
                 M03 * det3(M10, M11, B1, M20, M21, B2, M30, M31, B3);

  // x[3]: Replace column 3 with b
  const T DET3 = M00 * det3(M11, M12, B1, M21, M22, B2, M31, M32, B3) -
                 M01 * det3(M10, M12, B1, M20, M22, B2, M30, M32, B3) +
                 M02 * det3(M10, M11, B1, M20, M21, B2, M30, M31, B3) -
                 B0 * det3(M10, M11, M12, M20, M21, M22, M30, M31, M32);

  x.data()[0] = DET0 * INV_DET;
  x.data()[1] = DET1 * INV_DET;
  x.data()[2] = DET2 * INV_DET;
  x.data()[3] = DET3 * INV_DET;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------- Elementwise Arithmetic -------------------------- */

template <typename T>
uint8_t Matrix4<T>::addInto(const Matrix4<T>& b, Matrix4<T>& out) const noexcept {
  if (!is4x4(a_) || !is4x4(b.a_) || !is4x4(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::addInto(this->a_, b.a_, out.a_);
}

template <typename T>
uint8_t Matrix4<T>::subInto(const Matrix4<T>& b, Matrix4<T>& out) const noexcept {
  if (!is4x4(a_) || !is4x4(b.a_) || !is4x4(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::subInto(this->a_, b.a_, out.a_);
}

template <typename T> uint8_t Matrix4<T>::scaleInto(T alpha, Matrix4<T>& out) const noexcept {
  if (!is4x4(a_) || !is4x4(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::scaleInto(this->a_, alpha, out.a_);
}

template <typename T> uint8_t Matrix4<T>::axpyInto(T alpha, Matrix4<T>& y) const noexcept {
  if (!is4x4(a_) || !is4x4(y.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::axpyInto(alpha, this->a_, y.a_);
}

template <typename T>
uint8_t Matrix4<T>::hadamardInto(const Matrix4<T>& b, Matrix4<T>& out) const noexcept {
  if (!is4x4(a_) || !is4x4(b.a_) || !is4x4(out.a_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return apex::math::linalg::hadamardInto(this->a_, b.a_, out.a_);
}

/* ----------------------- Matrix-Vector Helpers --------------------------- */

template <typename T>
uint8_t Matrix4<T>::multiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept {
  const uint8_t S = require4x4();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (x.orient() != VectorOrient::Col || y.orient() != VectorOrient::Col) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  if (x.size() != 4 || y.size() != 4) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Naive 4x4 * 4x1 multiplication
  const T* xd = x.data();
  T* yd = y.data();

  yd[0] = a_(0, 0) * xd[0] + a_(0, 1) * xd[1] + a_(0, 2) * xd[2] + a_(0, 3) * xd[3];
  yd[1] = a_(1, 0) * xd[0] + a_(1, 1) * xd[1] + a_(1, 2) * xd[2] + a_(1, 3) * xd[3];
  yd[2] = a_(2, 0) * xd[0] + a_(2, 1) * xd[1] + a_(2, 2) * xd[2] + a_(2, 3) * xd[3];
  yd[3] = a_(3, 0) * xd[0] + a_(3, 1) * xd[1] + a_(3, 2) * xd[2] + a_(3, 3) * xd[3];

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Matrix4<T>::transposeMultiplyVecInto(const Vector<T>& x, Vector<T>& y) const noexcept {
  const uint8_t S = require4x4();
  if (S != static_cast<uint8_t>(Status::SUCCESS)) {
    return S;
  }

  if (x.orient() != VectorOrient::Col || y.orient() != VectorOrient::Col) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  if (x.size() != 4 || y.size() != 4) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Naive A^T * x
  const T* xd = x.data();
  T* yd = y.data();

  yd[0] = a_(0, 0) * xd[0] + a_(1, 0) * xd[1] + a_(2, 0) * xd[2] + a_(3, 0) * xd[3];
  yd[1] = a_(0, 1) * xd[0] + a_(1, 1) * xd[1] + a_(2, 1) * xd[2] + a_(3, 1) * xd[3];
  yd[2] = a_(0, 2) * xd[0] + a_(1, 2) * xd[1] + a_(2, 2) * xd[2] + a_(3, 2) * xd[3];
  yd[3] = a_(0, 3) * xd[0] + a_(1, 3) * xd[1] + a_(2, 3) * xd[2] + a_(3, 3) * xd[3];

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_MATRIX4_TPP
