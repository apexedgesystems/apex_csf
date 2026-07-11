#ifndef APEX_SIM_SENSORS_BOX_CLEARANCE_LIDAR_HPP
#define APEX_SIM_SENSORS_BOX_CLEARANCE_LIDAR_HPP
/**
 * @file BoxClearanceLidar.hpp
 * @brief Six-beam clearance lidar against an axis-aligned box.
 *
 * Two measurement modes over the same closed-form geometry (no ray-march, no
 * mesh -- the wall planes are the box half-extents):
 *
 * 1. `measure` -- a point sensor ranging along the WORLD axes:
 *      clr_pos_axis = half_axis - sensor_axis
 *      clr_neg_axis = half_axis + sensor_axis
 *    Yaw-independent; non-negative while the point stays inside the box.
 *
 * 2. `measureMounted` -- six pods mounted at `mount_radius` from the body
 *    center, each ranging outward along its own BODY axis (the X/Y pairs yaw
 *    with the body; Z stays vertical). Each reading is the slab ray-to-wall
 *    distance minus the mount offset -- "pod tip to wall", reaching 0 when a
 *    pod touches its wall. `rayToWall` exposes the underlying slab primitive
 *    for arbitrary interior rays.
 *
 * Optional zero-mean Gaussian range noise (per beam, deterministic via the
 * SensorBase sampler) models a noisy lidar; the default is ideal.
 */

#include "src/sim/sensors/inc/SensorBase.hpp"

#include <cmath>
#include <cstdint>

namespace sim::sensors {

/** Axis-aligned box half-extents (meters), centered at the origin. */
struct BoxExtents {
  double half_x = 1.0;
  double half_y = 1.0;
  double half_z = 1.0;
};

/** Flat six-axis clearance measurement (meters). */
struct BoxClearanceMeasurement {
  double pos_x = 0.0;
  double neg_x = 0.0;
  double pos_y = 0.0;
  double neg_y = 0.0;
  double pos_z = 0.0;
  double neg_z = 0.0;
};

struct BoxClearanceLidarParams {
  double sigma_m = 0.0; ///< per-beam range noise (1-sigma, m); 0 = ideal
  std::uint32_t seed = 0x12340004u;
};

class BoxClearanceLidar : public SensorBase {
public:
  explicit BoxClearanceLidar(const BoxClearanceLidarParams& p = {}) noexcept
      : SensorBase(SensorKind::Lidar, "box_clearance_lidar", p.seed), p_(p) {}

  /**
   * @brief Clearance from the sensor point to each of the six box walls.
   * @param sx,sy,sz sensor position (box-local meters)
   * @param box      box half-extents
   */
  [[nodiscard]] BoxClearanceMeasurement measure(double sx, double sy, double sz,
                                                const BoxExtents& box) noexcept {
    BoxClearanceMeasurement m;
    m.pos_x = clamp(box.half_x - sx + noise());
    m.neg_x = clamp(box.half_x + sx + noise());
    m.pos_y = clamp(box.half_y - sy + noise());
    m.neg_y = clamp(box.half_y + sy + noise());
    m.pos_z = clamp(box.half_z - sz + noise());
    m.neg_z = clamp(box.half_z + sz + noise());
    return m;
  }

  /**
   * @brief Wall distance along an arbitrary ray from an interior point (slab form).
   *
   * For each axis with a non-negligible direction component, the ray meets the
   * wall plane the component points at in t_i = (sign(d_i)*half_i - p_i) / d_i;
   * the ray-to-wall distance is the minimum over the axes. Noise-free and
   * mount-free: the geometric primitive the mounted measure composes.
   *
   * @param px,py,pz interior ray origin (box-local meters)
   * @param dx,dy,dz unit ray direction
   * @param box      box half-extents
   */
  [[nodiscard]] static double rayToWall(double px, double py, double pz, double dx, double dy,
                                        double dz, const BoxExtents& box) noexcept {
    constexpr double kEps = 1e-12;
    constexpr double kHuge = 1e300;
    double t = kHuge;
    if (dx > kEps || dx < -kEps) {
      const double ti = ((dx > 0.0 ? box.half_x : -box.half_x) - px) / dx;
      t = ti < t ? ti : t;
    }
    if (dy > kEps || dy < -kEps) {
      const double ti = ((dy > 0.0 ? box.half_y : -box.half_y) - py) / dy;
      t = ti < t ? ti : t;
    }
    if (dz > kEps || dz < -kEps) {
      const double ti = ((dz > 0.0 ? box.half_z : -box.half_z) - pz) / dz;
      t = ti < t ? ti : t;
    }
    return t;
  }

  /**
   * @brief Mounted, body-fixed six-beam measurement.
   *
   * Models six sensor pods mounted at `mount_radius_m` from the body center
   * along the body axes, each ranging outward along its own (body-fixed) axis.
   * The X/Y pairs yaw with the body (bx = (cos yaw, sin yaw, 0),
   * by = (-sin yaw, cos yaw, 0)); the Z pair stays vertical. Each reading is
   * the slab ray-to-wall distance minus the mount offset -- "pod tip to wall",
   * reaching 0 when a pod touches its wall. Optional per-beam noise as in
   * `measure`.
   *
   * @param sx,sy,sz       body center (box-local meters)
   * @param yaw_rad        body yaw about box +Z
   * @param mount_radius_m sensor mount offset from the body center
   * @param box            box half-extents
   */
  [[nodiscard]] BoxClearanceMeasurement measureMounted(double sx, double sy, double sz,
                                                       double yaw_rad, double mount_radius_m,
                                                       const BoxExtents& box) noexcept {
    const double C = std::cos(yaw_rad);
    const double S = std::sin(yaw_rad);
    const auto BEAM = [&](double dx, double dy, double dz) noexcept {
      return clamp(rayToWall(sx, sy, sz, dx, dy, dz, box) - mount_radius_m + noise());
    };
    BoxClearanceMeasurement m;
    m.pos_x = BEAM(C, S, 0.0);   // body +X
    m.neg_x = BEAM(-C, -S, 0.0); // body -X
    m.pos_y = BEAM(-S, C, 0.0);  // body +Y
    m.neg_y = BEAM(S, -C, 0.0);  // body -Y
    m.pos_z = BEAM(0.0, 0.0, 1.0);
    m.neg_z = BEAM(0.0, 0.0, -1.0);
    return m;
  }

  [[nodiscard]] const BoxClearanceLidarParams& params() const noexcept { return p_; }

private:
  [[nodiscard]] double noise() noexcept {
    return p_.sigma_m > 0.0 ? sampler_.gaussian() * p_.sigma_m : 0.0;
  }

  [[nodiscard]] static double clamp(double v) noexcept { return v < 0.0 ? 0.0 : v; }

  BoxClearanceLidarParams p_;
};

} // namespace sim::sensors

#endif // APEX_SIM_SENSORS_BOX_CLEARANCE_LIDAR_HPP
