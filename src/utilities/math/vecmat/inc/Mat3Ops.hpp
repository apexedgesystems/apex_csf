#ifndef APEX_MATH_VECMAT_MAT3_OPS_HPP
#define APEX_MATH_VECMAT_MAT3_OPS_HPP
/**
 * @file Mat3Ops.hpp
 * @brief Fixed-size 3x3 matrix operations over raw row-major T[9] arrays.
 *
 * Same raw-array discipline as Vec3Ops: free functions, caller-owned storage,
 * row-major layout matching Quaternion<T>::toRotationMatrixInto. Covers the
 * small dense algebra a rigid body needs per tick (inertia multiply/solve)
 * without pulling the BLAS-backed linalg Array.
 *
 * @note RT-SAFE: All functions noexcept, no allocation; constexpr where the
 *       math is libm-free.
 */

#include "src/utilities/compatibility/inc/compat_math.hpp"
#include "src/utilities/math/vecmat/inc/VecmatStatus.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace vecmat {

/** @brief m = I. */
template <typename T> constexpr void identity(T* m) noexcept {
  m[0] = T(1);
  m[1] = T(0);
  m[2] = T(0);
  m[3] = T(0);
  m[4] = T(1);
  m[5] = T(0);
  m[6] = T(0);
  m[7] = T(0);
  m[8] = T(1);
}

/**
 * @brief out = m^T.
 * @note `out` must not alias `m`.
 */
template <typename T> constexpr void transposeInto(const T* m, T* out) noexcept {
  out[0] = m[0];
  out[1] = m[3];
  out[2] = m[6];
  out[3] = m[1];
  out[4] = m[4];
  out[5] = m[7];
  out[6] = m[2];
  out[7] = m[5];
  out[8] = m[8];
}

/**
 * @brief out = m * v.
 * @note `out` must not alias `v`.
 */
template <typename T> constexpr void multiplyVec(const T* m, const T* v, T* out) noexcept {
  out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
  out[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
  out[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}

/**
 * @brief out = a * b.
 * @note `out` must not alias `a` or `b`.
 */
template <typename T> constexpr void multiplyMat(const T* a, const T* b, T* out) noexcept {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[r * 3 + c] =
          a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
    }
  }
}

/** @brief det(m) by cofactor expansion. */
template <typename T> constexpr T det(const T* m) noexcept {
  return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
         m[2] * (m[3] * m[7] - m[4] * m[6]);
}

/**
 * @brief out = m^{-1} (adjugate / det); ERROR_SINGULAR below the epsilon guard.
 * @note `out` must not alias `m`.
 */
template <typename T> inline uint8_t inverseInto(const T* m, T* out) noexcept {
  const T D = det(m);
  if (apex::compat::fabs(D) < apex::compat::epsilon<T>()) {
    return static_cast<uint8_t>(Status::ERROR_SINGULAR);
  }
  const T ID = T(1) / D;
  out[0] = (m[4] * m[8] - m[5] * m[7]) * ID;
  out[1] = (m[2] * m[7] - m[1] * m[8]) * ID;
  out[2] = (m[1] * m[5] - m[2] * m[4]) * ID;
  out[3] = (m[5] * m[6] - m[3] * m[8]) * ID;
  out[4] = (m[0] * m[8] - m[2] * m[6]) * ID;
  out[5] = (m[2] * m[3] - m[0] * m[5]) * ID;
  out[6] = (m[3] * m[7] - m[4] * m[6]) * ID;
  out[7] = (m[1] * m[6] - m[0] * m[7]) * ID;
  out[8] = (m[0] * m[4] - m[1] * m[3]) * ID;
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief Solve m * x = b for symmetric positive-definite-ish m (inertia
 *        tensors); falls back to the general inverse path.
 * @note `x` must not alias `b`.
 */
template <typename T> inline uint8_t solveInto(const T* m, const T* b, T* x) noexcept {
  T inv[9];
  const uint8_t RC = inverseInto(m, inv);
  if (RC != static_cast<uint8_t>(Status::SUCCESS)) {
    return RC;
  }
  multiplyVec(inv, b, x);
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace vecmat
} // namespace math
} // namespace apex

#endif // APEX_MATH_VECMAT_MAT3_OPS_HPP
