#ifndef APEX_MATH_FRAMES_TRANSFORM_HPP
#define APEX_MATH_FRAMES_TRANSFORM_HPP
/**
 * @file Transform.hpp
 * @brief SE(3) rigid transform as a flat POD, with the point/vector split.
 *
 * SEMANTICS (the one convention everything composes on): a Transform is the
 * pose of a CHILD frame expressed in its PARENT -- it maps child coordinates
 * into the parent frame:
 *
 *   p_parent = R(q) * p_child + t
 *
 * where q is the child-to-parent rotation (scalar-first Hamilton, unit) and
 * t is the child origin expressed in parent coordinates.
 *
 * The apply API splits deliberately:
 *  - transformPointInto : rotate + translate. POSITIONS -- a mount origin,
 *    an obstacle location -- pick up the lever arm.
 *  - rotateVectorInto   : rotate only. DIRECTIONS -- a sensor ray, a
 *    velocity axis -- have no position, so no lever arm.
 * Feeding a direction through the point path is the classic frame bug this
 * split exists to prevent.
 *
 * Flat POD: {T q[4]; T t[3]}, identity by default, trivially copyable --
 * a Transform streams over a bus or shm slot byte-for-byte, and the q field
 * is directly viewable by Quaternion<T> for rotation-only math.
 *
 * @note RT-SAFE: All operations noexcept, no allocation.
 */

#include "src/utilities/math/frames/inc/FramesStatus.hpp"
#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/vecmat/inc/Vec3Ops.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace frames {

/* -------------------------------- Transform ------------------------------- */

/**
 * @brief Child-to-parent rigid transform, flat POD.
 *
 * @tparam T Element type (float or double).
 */
template <typename T> struct Transform {
  T q[4] = {T(1), T(0), T(0), T(0)}; ///< child-to-parent rotation [w,x,y,z]
  T t[3] = {T(0), T(0), T(0)};       ///< child origin in parent coordinates

  /** @brief Mutable rotation view over the owned q storage. */
  [[nodiscard]] quaternion::Quaternion<T> rotation() noexcept {
    return quaternion::Quaternion<T>(q);
  }
};

static_assert(sizeof(Transform<float>) == 7 * sizeof(float), "Transform<float> must be flat");
static_assert(sizeof(Transform<double>) == 7 * sizeof(double), "Transform<double> must be flat");

/* ------------------------------- Apply ------------------------------------ */

/**
 * @brief p_parent = R(q) * p_child + t  (positions: lever arm applies).
 * @note `out` must not alias `pIn`.
 */
template <typename T>
inline uint8_t transformPointInto(const Transform<T>& x, const T* pIn, T* out) noexcept {
  // Rotation via the quaternion view (const-safe: rotate reads q only).
  // The rotate/multiply/conjugate quaternion ops are total on valid input
  // (unconditional SUCCESS), so their returns are not re-checked here; the
  // fallible quaternion ops (normalize, inverse) are not on these paths.
  quaternion::Quaternion<T> r(const_cast<T*>(x.q));
  (void)r.rotateVectorInto(pIn, out);
  vecmat::add(out, x.t, out);
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief v_parent = R(q) * v_child  (directions: rotation only, no lever arm).
 * @note `out` must not alias `vIn`.
 */
template <typename T>
inline uint8_t rotateVectorInto(const Transform<T>& x, const T* vIn, T* out) noexcept {
  quaternion::Quaternion<T> r(const_cast<T*>(x.q));
  return r.rotateVectorInto(vIn, out);
}

/* ------------------------------ Compose / invert -------------------------- */

/**
 * @brief out = a compose b: apply b (child->mid), then a (mid->parent).
 *
 * R_out = R_a * R_b;  t_out = R_a * t_b + t_a. Composing a grandchild's
 * transform up a tree is composeInto(parentEdge, childEdge, out).
 * @note `out` must not alias `a` or `b`.
 */
template <typename T>
inline uint8_t composeInto(const Transform<T>& a, const Transform<T>& b,
                           Transform<T>& out) noexcept {
  quaternion::Quaternion<T> qa(const_cast<T*>(a.q));
  quaternion::Quaternion<T> qb(const_cast<T*>(b.q));
  quaternion::Quaternion<T> qo(out.q);
  (void)qa.multiplyInto(qb, qo);
  (void)qa.rotateVectorInto(b.t, out.t);
  vecmat::add(out.t, a.t, out.t);
  return static_cast<uint8_t>(Status::SUCCESS);
}

/**
 * @brief out = x^{-1}: the parent-to-child transform.
 *
 * R' = R^{-1} (conjugate for unit q);  t' = -(R^{-1} * t).
 * @note `out` must not alias `x`.
 */
template <typename T>
inline uint8_t inverseInto(const Transform<T>& x, Transform<T>& out) noexcept {
  quaternion::Quaternion<T> qx(const_cast<T*>(x.q));
  quaternion::Quaternion<T> qo(out.q);
  (void)qx.conjugateInto(qo);
  (void)qo.rotateVectorInto(x.t, out.t);
  vecmat::scale(out.t, T(-1), out.t);
  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace frames
} // namespace math
} // namespace apex

#endif // APEX_MATH_FRAMES_TRANSFORM_HPP
