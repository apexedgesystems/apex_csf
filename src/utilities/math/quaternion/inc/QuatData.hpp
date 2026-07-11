#ifndef APEX_MATH_QUATERNION_QUAT_DATA_HPP
#define APEX_MATH_QUATERNION_QUAT_DATA_HPP
/**
 * @file QuatData.hpp
 * @brief Owned flat-POD quaternion storage with a Quaternion<T> view accessor.
 *
 * Quaternion<T> is a non-owning view; something must own the four scalars.
 * QuatData<T> is that owner: a trivially-copyable aggregate laid out
 * scalar-first [w, x, y, z], identity by default, streamable byte-for-byte
 * (a bus frame or shm slot can carry it directly), and convertible to the
 * view for math:
 *
 *   QuatData<double> q;                  // identity
 *   q.view().setFromAngleAxis(a, 0, 0, 1);
 *
 * @note RT-SAFE: Aggregate POD, no allocation.
 */

#include "src/utilities/math/quaternion/inc/Quaternion.hpp"

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace math {
namespace quaternion {

/* -------------------------------- QuatData -------------------------------- */

/**
 * @brief Owned 4-scalar quaternion storage, scalar-first [w, x, y, z].
 *
 * @tparam T Element type (float or double).
 */
template <typename T> struct QuatData {
  T d[4] = {T(1), T(0), T(0), T(0)}; ///< [w, x, y, z]; identity by default.

  /** @brief Mutable math view over the owned storage. */
  [[nodiscard]] Quaternion<T> view() noexcept { return Quaternion<T>(d); }

  T w() const noexcept { return d[0]; }
  T x() const noexcept { return d[1]; }
  T y() const noexcept { return d[2]; }
  T z() const noexcept { return d[3]; }
};

static_assert(sizeof(QuatData<float>) == 4 * sizeof(float), "QuatData<float> must be flat");
static_assert(sizeof(QuatData<double>) == 4 * sizeof(double), "QuatData<double> must be flat");

} // namespace quaternion
} // namespace math
} // namespace apex

#endif // APEX_MATH_QUATERNION_QUAT_DATA_HPP
