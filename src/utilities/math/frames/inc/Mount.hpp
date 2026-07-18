#ifndef APEX_MATH_FRAMES_MOUNT_HPP
#define APEX_MATH_FRAMES_MOUNT_HPP
/**
 * @file Mount.hpp
 * @brief Sensor mounts as frames: sugar over addStatic.
 *
 * A sensor mount is not new machinery -- it is a static child frame of the
 * body: a lever arm (the mount origin in body coordinates) and the sensor
 * frame's attitude in the body. Once mounted, every conversion a consumer
 * needs falls out of resolve: body->mount, mount->ECEF, and the
 * control-decision chain
 *
 *   r_target/CG = resolve(mount, cgFrame, t) applied to (d * a_hat_sensor)
 *
 * with transformPoint for the sensed target position (lever arms apply) and
 * rotateVector for the ray direction (they do not). Per-arm sensor arrays
 * later are just several mounts.
 *
 * @note RT-SAFE: All operations noexcept, no allocation.
 */

#include "src/utilities/math/frames/inc/FrameGraph.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace frames {

/* --------------------------------- Mount ---------------------------------- */

/**
 * @brief A sensor's pose on the body: flat POD, tprm/bus-streamable.
 *
 * @tparam T Element type (float or double).
 */
template <typename T> struct Mount {
  T leverArmM[3] = {T(0), T(0), T(0)}; ///< mount origin in body coordinates [m]
  T q[4] = {T(1), T(0), T(0), T(0)};   ///< sensor-to-body rotation [w,x,y,z]
                                       ///< (the sensor frame's attitude in the body)
};

static_assert(sizeof(Mount<float>) == 7 * sizeof(float), "Mount<float> must be flat");
static_assert(sizeof(Mount<double>) == 7 * sizeof(double), "Mount<double> must be flat");

/** @brief The mount's child-to-parent (sensor-to-body) Transform. */
template <typename T> inline Transform<T> mountEdge(const Mount<T>& m) noexcept {
  Transform<T> x;
  for (int i = 0; i < 4; ++i) {
    x.q[i] = m.q[i];
  }
  for (int i = 0; i < 3; ++i) {
    x.t[i] = m.leverArmM[i];
  }
  return x;
}

/** @brief Attach a mount to a body frame; returns the sensor frame's id. */
template <typename T, size_t CAPACITY>
inline uint8_t addMount(FrameGraph<T, CAPACITY>& g, FrameId body, const Mount<T>& m,
                        const char* name, FrameId& out) noexcept {
  return g.addStatic(body, mountEdge(m), name, out);
}

} // namespace frames
} // namespace math
} // namespace apex

#endif // APEX_MATH_FRAMES_MOUNT_HPP
