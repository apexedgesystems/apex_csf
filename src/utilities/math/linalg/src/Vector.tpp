/**
 * @file Vector.tpp
 * @brief Template implementation for Vector.
 */
#ifndef APEX_MATH_LINALG_VECTOR_TPP
#define APEX_MATH_LINALG_VECTOR_TPP

#include "src/utilities/math/linalg/inc/ArrayOps.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"

#include <cmath>
#include <limits>

namespace apex {
namespace math {
namespace linalg {

/* ----------------------------- Constructors ------------------------------ */

template <typename T>
Vector<T>::Vector(const Array<T>& v, VectorOrient orient) noexcept : v_(v), orient_(orient) {
  (void)checkShape();
}

template <typename T>
Vector<T>::Vector(T* data, std::size_t n, VectorOrient orient, Layout layout,
                  std::size_t ld) noexcept
    : v_(data, (orient == VectorOrient::Col ? n : 1), (orient == VectorOrient::Col ? 1 : n), layout,
         ld),
      orient_(orient) {
  (void)checkShape();
}

template <typename T>
template <class D>
Vector<T>::Vector(const ArrayBase<T, D>& base, VectorOrient orient) noexcept
    : v_(base.data(), base.rows(), base.cols(), base.layout(), base.ld()), orient_(orient) {
  (void)checkShape();
}

/* ------------------------------ Accessors -------------------------------- */

template <typename T> std::size_t Vector<T>::size() const noexcept {
  return (orient_ == VectorOrient::Col) ? v_.rows() : v_.cols();
}

template <typename T> std::size_t Vector<T>::length() const noexcept { return size(); }

template <typename T> VectorOrient Vector<T>::orient() const noexcept { return orient_; }

template <typename T> Layout Vector<T>::layout() const noexcept { return v_.layout(); }

template <typename T> std::size_t Vector<T>::rows() const noexcept { return v_.rows(); }

template <typename T> std::size_t Vector<T>::cols() const noexcept { return v_.cols(); }

template <typename T> std::size_t Vector<T>::ld() const noexcept { return v_.ld(); }

template <typename T> T* Vector<T>::data() noexcept { return v_.data(); }

template <typename T> const T* Vector<T>::data() const noexcept { return v_.data(); }

/* --------------------------- Private Helpers ----------------------------- */

template <typename T> uint8_t Vector<T>::checkShape() const noexcept {
  if (!isVectorShape(v_)) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Vector<T>::checkCompat(const Vector<T>& other) const noexcept {
  if (this->size() != other.size()) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  if (this->orient_ != other.orient_) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_LAYOUT);
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> bool Vector<T>::isVectorShape(const Array<T>& a) noexcept {
  return (a.rows() == 1 || a.cols() == 1);
}

/* -------------------------- Vector Operations ---------------------------- */

template <typename T> uint8_t Vector<T>::dotInto(const Vector<T>& y, T& out) const noexcept {
  uint8_t chk = checkCompat(y);
  if (chk != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk;
  }

  out = T(0);
  for (std::size_t i = 0; i < size(); ++i) {
    out += this->data()[i] * y.data()[i];
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Vector<T>::norm2Into(T& out) const noexcept {
  out = T(0);
  for (std::size_t i = 0; i < size(); ++i) {
    const T VAL = this->data()[i];
    out += VAL * VAL;
  }
  out = std::sqrt(out);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Vector<T>::normalizeInPlace() noexcept {
  T nrm = T(0);
  (void)norm2Into(nrm);
  if (nrm == T(0)) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_VALUE);
  }

  for (std::size_t i = 0; i < size(); ++i) {
    this->data()[i] /= nrm;
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Vector<T>::axpyInto(T alpha, Vector<T>& y) const noexcept {
  uint8_t chk = checkCompat(y);
  if (chk != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk;
  }
  return apex::math::linalg::axpyInto(alpha, this->v_, y.v_);
}

template <typename T>
uint8_t Vector<T>::hadamardInto(const Vector<T>& y, Vector<T>& z) const noexcept {
  uint8_t chk1 = checkCompat(y);
  uint8_t chk2 = checkCompat(z);
  if (chk1 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk1;
  }
  if (chk2 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk2;
  }
  return apex::math::linalg::hadamardInto(this->v_, y.v_, z.v_);
}

template <typename T>
uint8_t Vector<T>::addInto(const Vector<T>& y, Vector<T>& out) const noexcept {
  uint8_t chk1 = checkCompat(y);
  uint8_t chk2 = checkCompat(out);
  if (chk1 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk1;
  }
  if (chk2 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk2;
  }
  return apex::math::linalg::addInto(this->v_, y.v_, out.v_);
}

template <typename T>
uint8_t Vector<T>::subInto(const Vector<T>& y, Vector<T>& out) const noexcept {
  uint8_t chk1 = checkCompat(y);
  uint8_t chk2 = checkCompat(out);
  if (chk1 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk1;
  }
  if (chk2 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk2;
  }
  return apex::math::linalg::subInto(this->v_, y.v_, out.v_);
}

template <typename T> uint8_t Vector<T>::scaleInto(T alpha, Vector<T>& out) const noexcept {
  uint8_t chk = checkCompat(out);
  if (chk != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk;
  }
  return apex::math::linalg::scaleInto(this->v_, alpha, out.v_);
}

template <typename T>
uint8_t Vector<T>::crossInto(const Vector<T>& y, Vector<T>& out) const noexcept {
  if (size() != 3 || y.size() != 3 || out.size() != 3) {
    return static_cast<uint8_t>(Status::ERROR_UNSUPPORTED_OP);
  }
  uint8_t chk1 = checkCompat(y);
  uint8_t chk2 = checkCompat(out);
  if (chk1 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk1;
  }
  if (chk2 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk2;
  }

  const T* a = this->data();
  const T* b = y.data();
  T* c = out.data();

  c[0] = a[1] * b[2] - a[2] * b[1];
  c[1] = a[2] * b[0] - a[0] * b[2];
  c[2] = a[0] * b[1] - a[1] * b[0];
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Vector<T>::projectOntoInto(const Vector<T>& y, Vector<T>& out) const noexcept {
  uint8_t chk1 = checkCompat(y);
  uint8_t chk2 = checkCompat(out);
  if (chk1 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk1;
  }
  if (chk2 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk2;
  }

  T denom = T(0);
  (void)this->dotInto(*this, denom);
  if (denom == T(0)) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_VALUE);
  }

  T numer = T(0);
  (void)this->dotInto(y, numer);

  T scale = numer / denom;
  for (std::size_t i = 0; i < size(); ++i) {
    out.data()[i] = scale * this->data()[i];
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Vector<T>::angleBetweenInto(const Vector<T>& y, T& outRadians) const noexcept {
  uint8_t chk = checkCompat(y);
  if (chk != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk;
  }

  T dotVal = T(0);
  T nx = T(0);
  T ny = T(0);
  (void)this->dotInto(y, dotVal);
  (void)this->norm2Into(nx);
  (void)y.norm2Into(ny);

  if (nx == T(0) || ny == T(0)) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_VALUE);
  }

  T cosTheta = dotVal / (nx * ny);
  if (cosTheta > T(1)) {
    cosTheta = T(1);
  }
  if (cosTheta < T(-1)) {
    cosTheta = T(-1);
  }

  outRadians = std::acos(cosTheta);
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------- Elementwise Operations -------------------------- */

template <typename T> uint8_t Vector<T>::absInto(Vector<T>& out) const noexcept {
  uint8_t chk = checkCompat(out);
  if (chk != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk;
  }

  for (std::size_t i = 0; i < size(); ++i) {
    out.data()[i] = std::abs(this->data()[i]);
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Vector<T>::minInto(const Vector<T>& y, Vector<T>& out) const noexcept {
  uint8_t chk1 = checkCompat(y);
  uint8_t chk2 = checkCompat(out);
  if (chk1 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk1;
  }
  if (chk2 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk2;
  }

  for (std::size_t i = 0; i < size(); ++i) {
    const T A = this->data()[i];
    const T B = y.data()[i];
    out.data()[i] = (A < B) ? A : B;
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Vector<T>::maxInto(const Vector<T>& y, Vector<T>& out) const noexcept {
  uint8_t chk1 = checkCompat(y);
  uint8_t chk2 = checkCompat(out);
  if (chk1 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk1;
  }
  if (chk2 != static_cast<uint8_t>(Status::SUCCESS)) {
    return chk2;
  }

  for (std::size_t i = 0; i < size(); ++i) {
    const T A = this->data()[i];
    const T B = y.data()[i];
    out.data()[i] = (A > B) ? A : B;
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------------- Reductions ------------------------------ */

template <typename T> uint8_t Vector<T>::sumInto(T& out) const noexcept {
  out = T(0);
  for (std::size_t i = 0; i < size(); ++i) {
    out += this->data()[i];
  }
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Vector<T>::meanInto(T& out) const noexcept {
  if (size() == 0) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_VALUE);
  }

  T sum = T(0);
  (void)sumInto(sum);
  out = sum / static_cast<T>(size());
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_VECTOR_TPP
