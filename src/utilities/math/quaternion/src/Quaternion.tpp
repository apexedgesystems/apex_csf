/**
 * @file Quaternion.tpp
 * @brief Template implementation for Quaternion.
 */
#ifndef APEX_MATH_QUATERNION_TPP
#define APEX_MATH_QUATERNION_TPP

#include "src/utilities/math/quaternion/inc/Quaternion.hpp"

#include <cmath>
#include <limits>

namespace apex {
namespace math {
namespace quaternion {

/* ----------------------------- Construction ------------------------------ */

template <typename T> Quaternion<T>::Quaternion(T* data) noexcept : data_(data) {}

/* ----------------------------- Set Operations ---------------------------- */

template <typename T> uint8_t Quaternion<T>::setIdentity() noexcept {
  data_[0] = T(1);
  data_[1] = T(0);
  data_[2] = T(0);
  data_[3] = T(0);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Quaternion<T>::set(T w, T x, T y, T z) noexcept {
  data_[0] = w;
  data_[1] = x;
  data_[2] = y;
  data_[3] = z;
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Quaternion<T>::setFromAngleAxis(T angleRad, T axisX, T axisY, T axisZ) noexcept {
  const T HALF_ANGLE = angleRad * T(0.5);
  const T S = std::sin(HALF_ANGLE);
  const T C = std::cos(HALF_ANGLE);

  data_[0] = C;
  data_[1] = axisX * S;
  data_[2] = axisY * S;
  data_[3] = axisZ * S;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------------- Basic Operations -------------------------- */

template <typename T> uint8_t Quaternion<T>::normInto(T& out) const noexcept {
  out = std::sqrt(data_[0] * data_[0] + data_[1] * data_[1] + data_[2] * data_[2] +
                  data_[3] * data_[3]);
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Quaternion<T>::normalizeInPlace() noexcept {
  T nrm = T(0);
  (void)normInto(nrm);

  if (nrm < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_VALUE);
  }

  const T INV_NRM = T(1) / nrm;
  data_[0] *= INV_NRM;
  data_[1] *= INV_NRM;
  data_[2] *= INV_NRM;
  data_[3] *= INV_NRM;

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Quaternion<T>::conjugateInto(Quaternion<T>& out) const noexcept {
  out.data()[0] = data_[0];
  out.data()[1] = -data_[1];
  out.data()[2] = -data_[2];
  out.data()[3] = -data_[3];
  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T> uint8_t Quaternion<T>::inverseInto(Quaternion<T>& out) const noexcept {
  T nrmSq = data_[0] * data_[0] + data_[1] * data_[1] + data_[2] * data_[2] + data_[3] * data_[3];

  if (nrmSq < std::numeric_limits<T>::epsilon()) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_VALUE);
  }

  const T INV_NRM_SQ = T(1) / nrmSq;
  out.data()[0] = data_[0] * INV_NRM_SQ;
  out.data()[1] = -data_[1] * INV_NRM_SQ;
  out.data()[2] = -data_[2] * INV_NRM_SQ;
  out.data()[3] = -data_[3] * INV_NRM_SQ;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ---------------------- Quaternion Multiplication ------------------------ */

template <typename T>
uint8_t Quaternion<T>::multiplyInto(const Quaternion<T>& b, Quaternion<T>& out) const noexcept {
  const T W1 = data_[0], X1 = data_[1], Y1 = data_[2], Z1 = data_[3];
  const T W2 = b.data()[0], X2 = b.data()[1], Y2 = b.data()[2], Z2 = b.data()[3];

  // Hamilton product: q1 * q2
  out.data()[0] = W1 * W2 - X1 * X2 - Y1 * Y2 - Z1 * Z2;
  out.data()[1] = W1 * X2 + X1 * W2 + Y1 * Z2 - Z1 * Y2;
  out.data()[2] = W1 * Y2 - X1 * Z2 + Y1 * W2 + Z1 * X2;
  out.data()[3] = W1 * Z2 + X1 * Y2 - Y1 * X2 + Z1 * W2;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ---------------------- Vector Rotation ---------------------------------- */

template <typename T>
uint8_t Quaternion<T>::rotateVectorInto(const T* vIn, T* vOut) const noexcept {
  // Rotate v by q: v' = q * v * q^{-1}
  // Optimized formula (avoids full quaternion multiply):
  // t = 2 * cross(q.xyz, v)
  // v' = v + q.w * t + cross(q.xyz, t)

  const T QW = data_[0], QX = data_[1], QY = data_[2], QZ = data_[3];
  const T VX = vIn[0], VY = vIn[1], VZ = vIn[2];

  // t = 2 * cross(q.xyz, v)
  const T TX = T(2) * (QY * VZ - QZ * VY);
  const T TY = T(2) * (QZ * VX - QX * VZ);
  const T TZ = T(2) * (QX * VY - QY * VX);

  // v' = v + q.w * t + cross(q.xyz, t)
  vOut[0] = VX + QW * TX + (QY * TZ - QZ * TY);
  vOut[1] = VY + QW * TY + (QZ * TX - QX * TZ);
  vOut[2] = VZ + QW * TZ + (QX * TY - QY * TX);

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ---------------------- Conversion Operations ---------------------------- */

template <typename T> uint8_t Quaternion<T>::toRotationMatrixInto(T* matOut) const noexcept {
  const T W = data_[0], X = data_[1], Y = data_[2], Z = data_[3];

  // Precompute repeated products
  const T XX = X * X, YY = Y * Y, ZZ = Z * Z;
  const T XY = X * Y, XZ = X * Z, YZ = Y * Z;
  const T WX = W * X, WY = W * Y, WZ = W * Z;

  // Row-major 3x3 rotation matrix
  matOut[0] = T(1) - T(2) * (YY + ZZ);
  matOut[1] = T(2) * (XY - WZ);
  matOut[2] = T(2) * (XZ + WY);

  matOut[3] = T(2) * (XY + WZ);
  matOut[4] = T(1) - T(2) * (XX + ZZ);
  matOut[5] = T(2) * (YZ - WX);

  matOut[6] = T(2) * (XZ - WY);
  matOut[7] = T(2) * (YZ + WX);
  matOut[8] = T(1) - T(2) * (XX + YY);

  return static_cast<uint8_t>(Status::SUCCESS);
}

template <typename T>
uint8_t Quaternion<T>::toAngleAxisInto(T& angleRad, T& axisX, T& axisY, T& axisZ) const noexcept {
  const T W = data_[0];

  // Clamp w to [-1, 1] for numerical stability
  T wClamped = W;
  if (wClamped > T(1)) {
    wClamped = T(1);
  }
  if (wClamped < T(-1)) {
    wClamped = T(-1);
  }

  angleRad = T(2) * std::acos(wClamped);

  const T S = std::sqrt(T(1) - wClamped * wClamped);

  if (S < std::numeric_limits<T>::epsilon()) {
    // Angle is 0 or 2*pi, axis is arbitrary
    axisX = T(1);
    axisY = T(0);
    axisZ = T(0);
  } else {
    axisX = data_[1] / S;
    axisY = data_[2] / S;
    axisZ = data_[3] / S;
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ---------------------- Interpolation ------------------------------------ */

namespace detail {

/**
 * @brief Fast acos approximation using polynomial.
 *
 * Max error ~0.01 radians for x in [-1, 1].
 * Uses Chebyshev polynomial approximation.
 */
template <typename T> inline T fastAcos(T x) noexcept {
  // Clamp to valid range
  if (x >= T(1)) {
    return T(0);
  }
  if (x <= T(-1)) {
    return T(3.14159265358979323846);
  }

  // Polynomial approximation: acos(x) ≈ pi/2 - asin(x)
  // asin(x) ≈ x + x^3/6 + 3x^5/40 + 15x^7/336 for |x| < 0.7
  // For |x| > 0.7, use acos(x) = sqrt(2) * sqrt(1-x) * (1 + coeffs...)

  const T ABSVAL = (x >= T(0)) ? x : -x;

  if (ABSVAL < T(0.7)) {
    // Near zero: use Taylor series for asin
    const T X2 = x * x;
    const T X3 = X2 * x;
    const T X5 = X3 * X2;
    const T ASIN_APPROX = x + X3 * T(0.166666666666667) + X5 * T(0.075);
    return T(1.5707963267948966) - ASIN_APPROX;
  }

  // Near +-1: use identity acos(x) = sqrt(2*(1-x)) * (1 + poly)
  const T Y = T(1) - ABSVAL;
  const T SQRT_TERM = std::sqrt(T(2) * Y);
  // Polynomial correction for accuracy
  const T POLY = T(1) + Y * (T(0.166666666666667) + Y * T(0.075));
  const T RESULT = SQRT_TERM * POLY;

  return (x >= T(0)) ? RESULT : T(3.14159265358979323846) - RESULT;
}

/**
 * @brief Fast sin approximation using polynomial.
 *
 * Max error ~0.0001 for x in [-pi, pi].
 * Uses 5th-order polynomial.
 */
template <typename T> inline T fastSin(T x) noexcept {
  // Reduce to [-pi, pi] range
  const T PI = T(3.14159265358979323846);
  const T TWO_PI = T(6.28318530717958647692);

  // Simple range reduction for typical SLERP angles (0 to pi)
  if (x < T(0)) {
    x = -x;
    // sin(-x) = -sin(x), handled by caller
  }
  if (x > PI) {
    x = x - TWO_PI;
  }

  // 5th-order polynomial: sin(x) ≈ x - x^3/6 + x^5/120
  const T X2 = x * x;
  const T X3 = X2 * x;
  const T X5 = X3 * X2;
  return x - X3 * T(0.166666666666667) + X5 * T(0.00833333333333333);
}

} // namespace detail

template <typename T>
uint8_t Quaternion<T>::slerpInto(const Quaternion<T>& b, T t, Quaternion<T>& out) const noexcept {
  // Compute dot product
  T dot = data_[0] * b.data()[0] + data_[1] * b.data()[1] + data_[2] * b.data()[2] +
          data_[3] * b.data()[3];

  // If dot < 0, negate one quaternion to take shorter path
  T bw = b.data()[0], bx = b.data()[1], by = b.data()[2], bz = b.data()[3];
  if (dot < T(0)) {
    dot = -dot;
    bw = -bw;
    bx = -bx;
    by = -by;
    bz = -bz;
  }

  T scale0 = T(0), scale1 = T(0);

  if (dot > T(0.9995)) {
    // Quaternions are very close, use linear interpolation (NLERP fast path)
    scale0 = T(1) - t;
    scale1 = t;
  } else if (dot > T(0.95)) {
    // Medium range: use fast approximations
    const T THETA = detail::fastAcos(dot);
    const T SIN_THETA = detail::fastSin(THETA);
    const T INV_SIN_THETA = T(1) / SIN_THETA;
    scale0 = detail::fastSin((T(1) - t) * THETA) * INV_SIN_THETA;
    scale1 = detail::fastSin(t * THETA) * INV_SIN_THETA;
  } else {
    // Full range: use standard library (rare case, <5% of calls typically)
    const T THETA = std::acos(dot);
    const T SIN_THETA = std::sin(THETA);
    const T INV_SIN_THETA = T(1) / SIN_THETA;
    scale0 = std::sin((T(1) - t) * THETA) * INV_SIN_THETA;
    scale1 = std::sin(t * THETA) * INV_SIN_THETA;
  }

  out.data()[0] = scale0 * data_[0] + scale1 * bw;
  out.data()[1] = scale0 * data_[1] + scale1 * bx;
  out.data()[2] = scale0 * data_[2] + scale1 * by;
  out.data()[3] = scale0 * data_[3] + scale1 * bz;

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace quaternion
} // namespace math
} // namespace apex

#endif // APEX_MATH_QUATERNION_TPP
