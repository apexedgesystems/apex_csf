#ifndef APEX_MATH_VECMAT_VEC3_OPS_HPP
#define APEX_MATH_VECMAT_VEC3_OPS_HPP
/**
 * @file Vec3Ops.hpp
 * @brief Fixed-size 3-vector operations over raw T[3] arrays.
 *
 * Free functions over caller-owned storage, matching the quaternion library's
 * raw-pointer view discipline: no vector type to adopt, no ownership -- a
 * state-block field, a wire-frame slot, and a local array all work directly.
 * The one shared home for the cross/dot/norm arithmetic the sim libraries
 * carry locally today.
 *
 * @note RT-SAFE: All functions noexcept, no allocation; constexpr where the
 *       math is allocation- and libm-free.
 */

#include "src/utilities/compatibility/inc/compat_math.hpp"
#include "src/utilities/math/vecmat/inc/VecmatStatus.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace vecmat {

/** @brief out = [x, y, z]. */
template <typename T> constexpr void set(T* out, T x, T y, T z) noexcept {
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

/** @brief out = v. */
template <typename T> constexpr void copy(const T* v, T* out) noexcept {
  out[0] = v[0];
  out[1] = v[1];
  out[2] = v[2];
}

/** @brief out = a + b (aliasing-safe). */
template <typename T> constexpr void add(const T* a, const T* b, T* out) noexcept {
  out[0] = a[0] + b[0];
  out[1] = a[1] + b[1];
  out[2] = a[2] + b[2];
}

/** @brief out = a - b (aliasing-safe). */
template <typename T> constexpr void sub(const T* a, const T* b, T* out) noexcept {
  out[0] = a[0] - b[0];
  out[1] = a[1] - b[1];
  out[2] = a[2] - b[2];
}

/** @brief out = s * v (aliasing-safe). */
template <typename T> constexpr void scale(const T* v, T s, T* out) noexcept {
  out[0] = s * v[0];
  out[1] = s * v[1];
  out[2] = s * v[2];
}

/** @brief a . b */
template <typename T> constexpr T dot(const T* a, const T* b) noexcept {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/**
 * @brief out = a x b.
 * @note `out` must not alias `a` or `b` (the components cross-read).
 */
template <typename T> constexpr void cross(const T* a, const T* b, T* out) noexcept {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

/** @brief |v|^2 */
template <typename T> constexpr T normSq(const T* v) noexcept { return dot(v, v); }

/** @brief |v| */
template <typename T> inline T norm(const T* v) noexcept { return apex::compat::sqrt(normSq(v)); }

/**
 * @brief out = v / |v|; ERROR_INVALID_VALUE when |v| is below epsilon.
 * @note Aliasing-safe (out may be v).
 */
template <typename T> inline uint8_t normalizeInto(const T* v, T* out) noexcept {
  const T N = norm(v);
  if (N < apex::compat::epsilon<T>()) {
    return static_cast<uint8_t>(Status::ERROR_INVALID_VALUE);
  }
  scale(v, T(1) / N, out);
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace vecmat
} // namespace math
} // namespace apex

#endif // APEX_MATH_VECMAT_VEC3_OPS_HPP
