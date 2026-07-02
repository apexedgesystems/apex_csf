#ifndef APEX_SIM_SENSORS_BOX_CLEARANCE_LIDAR_HPP
#define APEX_SIM_SENSORS_BOX_CLEARANCE_LIDAR_HPP
/**
 * @file BoxClearanceLidar.hpp
 * @brief Six-axis clearance lidar against an axis-aligned box.
 *
 * Models a fixed six-beam proximity lidar at a sensor point inside an
 * axis-aligned box centered at the origin: it reports the clearance from the
 * sensor to each of the six walls (+/-X, +/-Y, +/-Z). For an axis-aligned box
 * the clearance is closed-form -- no ray-march, no mesh -- because the wall
 * planes are at the box half-extents:
 *
 *   clr_pos_axis = half_axis - sensor_axis      (distance to the +wall)
 *   clr_neg_axis = half_axis + sensor_axis      (distance to the -wall)
 *
 * Clearances are non-negative while the sensor stays inside the box; a caller
 * that keeps the body within the box bounds always gets positive readings.
 * Optional zero-mean Gaussian range noise (per beam, deterministic via the
 * SensorBase sampler) models a noisy lidar; the default is ideal.
 */

#include "src/sim/sensors/inc/SensorBase.hpp"

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
