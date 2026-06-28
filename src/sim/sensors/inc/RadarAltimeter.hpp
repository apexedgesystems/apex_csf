#ifndef APEX_SIM_SENSORS_RADAR_ALTIMETER_HPP
#define APEX_SIM_SENSORS_RADAR_ALTIMETER_HPP
/**
 * @file RadarAltimeter.hpp
 * @brief Radar altimeter (height above ground) model.
 *
 * A radar altimeter reflects a signal off the terrain directly below the vehicle
 * and derives above-ground-level (AGL) altitude from the time of flight. Two error
 * sources dominate:
 *   1. multiplicative noise (~1% of true AGL) from terrain roughness and
 *      angle-of-arrival uncertainty;
 *   2. a range limit -- a civil radio altimeter reads only to roughly 760 m
 *      (~2500 ft); above that the return is invalid.
 *
 * The measurement carries an explicit validity flag rather than overloading the
 * altitude with an out-of-range sentinel.
 */

#include "src/sim/sensors/inc/SensorBase.hpp"

#include <cstdint>

namespace sim::sensors {

struct RadarAltimeterParams {
  double max_range_m = 760.0; // civil radio-altimeter range limit
  double noise_pct = 0.01;    // multiplicative noise, fraction of true AGL
  double bias_m = 0.0;        // additive bias
  double min_floor_m = 0.0;   // measurement never reads below this
  std::uint32_t seed = 0x12340002u;
};

/** @brief Flat measurement: AGL altitude plus a validity flag. */
struct RadarAltimeterMeasurement {
  double agl_m = 0.0;
  bool valid = false; ///< false when true AGL exceeds the range limit
};

class RadarAltimeter : public SensorBase {
public:
  explicit RadarAltimeter(const RadarAltimeterParams& p = {}) noexcept
      : SensorBase(SensorKind::Altimeter, "radar_altimeter", p.seed), p_(p) {}

  /** @brief Measure AGL; valid == false when true AGL is beyond max range. */
  [[nodiscard]] RadarAltimeterMeasurement measureAGL(double agl_true_m) noexcept {
    if (agl_true_m > p_.max_range_m) {
      return RadarAltimeterMeasurement{0.0, false};
    }
    if (agl_true_m < 0.0) {
      agl_true_m = 0.0;
    }
    const double noise = sampler_.gaussian() * p_.noise_pct * agl_true_m;
    double meas = agl_true_m + noise + p_.bias_m;
    if (meas < p_.min_floor_m) {
      meas = p_.min_floor_m;
    }
    return RadarAltimeterMeasurement{meas, true};
  }

  [[nodiscard]] const RadarAltimeterParams& params() const noexcept { return p_; }

private:
  RadarAltimeterParams p_;
};

} // namespace sim::sensors

#endif // APEX_SIM_SENSORS_RADAR_ALTIMETER_HPP
