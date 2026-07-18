#ifndef APEX_SIM_SENSORS_GPS_HPP
#define APEX_SIM_SENSORS_GPS_HPP
/**
 * @file GPS.hpp
 * @brief GNSS receiver model -- geodetic position + NED velocity with bias and noise.
 *
 * Models a civil aviation-grade GNSS receiver. It reports geodetic position
 * (lat, lon, alt) and inertial NED velocity, each corrupted by a constant bias
 * and zero-mean Gaussian noise. Representative civil-receiver error magnitudes:
 *   - horizontal position: sigma ~ 3 m, bias ~ 1 m;
 *   - vertical position:   sigma ~ 5 m, bias ~ 1 m (worse than horizontal because
 *     vertical geometry / dilution of precision is weaker);
 *   - velocity: sigma ~ 0.1 m/s per axis (Doppler-derived, very accurate).
 *
 * Position error is generated in meters and converted to degrees with a local-flat
 * approximation: R_earth * pi/180 meters per degree of latitude, scaled by cos(lat)
 * for longitude (meridian convergence).
 */

#include "src/sim/sensors/inc/SensorBase.hpp"
#include "src/utilities/math/vecmat/inc/Angles.hpp"

#include <cmath>
#include <cstdint>

namespace sim::sensors {

struct GPSParams {
  // Position errors (1-sigma) and constant biases.
  double sigma_horizontal_m = 3.0;
  double sigma_vertical_m = 5.0;
  double bias_horizontal_m = 1.0;
  double bias_vertical_m = 1.0;

  // Velocity error (1-sigma, per axis).
  double sigma_velocity_m_s = 0.1;

  // Reference radius for the degree<->meter conversion.
  double earth_radius_m = 6378137.0;

  std::uint32_t seed = 0x12340003u;
};

/** @brief Flat measurement: geodetic position + NED velocity. */
struct GPSMeasurement {
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  double alt_m = 0.0;
  double V_north_m_s = 0.0;
  double V_east_m_s = 0.0;
  double V_down_m_s = 0.0;
};

class GPS : public SensorBase {
public:
  explicit GPS(const GPSParams& p = {}) noexcept
      : SensorBase(SensorKind::Gnss, "gps", p.seed), p_(p) {}

  /**
   * @brief Sample one measurement from true position + NED velocity.
   * @note Velocity inputs are inertial-frame NED (V_down positive when descending).
   */
  [[nodiscard]] GPSMeasurement measure(double lat_true_deg, double lon_true_deg, double alt_true_m,
                                       double V_north_true, double V_east_true,
                                       double V_down_true) noexcept {
    constexpr double kDegToRad = apex::math::vecmat::DEG_TO_RAD;
    const double m_per_dlat = p_.earth_radius_m * kDegToRad;
    const double m_per_dlon = p_.earth_radius_m * std::cos(lat_true_deg * kDegToRad) * kDegToRad;

    // Position errors in meters (north carries the horizontal bias), then to degrees.
    const double err_north_m = sampler_.gaussian() * p_.sigma_horizontal_m + p_.bias_horizontal_m;
    const double err_east_m = sampler_.gaussian() * p_.sigma_horizontal_m;
    const double err_alt_m = sampler_.gaussian() * p_.sigma_vertical_m + p_.bias_vertical_m;

    GPSMeasurement m{};
    m.lat_deg = lat_true_deg + (m_per_dlat > 0.0 ? err_north_m / m_per_dlat : 0.0);
    m.lon_deg = lon_true_deg + (m_per_dlon > 0.0 ? err_east_m / m_per_dlon : 0.0);
    m.alt_m = alt_true_m + err_alt_m;

    m.V_north_m_s = V_north_true + sampler_.gaussian() * p_.sigma_velocity_m_s;
    m.V_east_m_s = V_east_true + sampler_.gaussian() * p_.sigma_velocity_m_s;
    m.V_down_m_s = V_down_true + sampler_.gaussian() * p_.sigma_velocity_m_s;
    return m;
  }

  [[nodiscard]] const GPSParams& params() const noexcept { return p_; }

private:
  GPSParams p_;
};

} // namespace sim::sensors

#endif // APEX_SIM_SENSORS_GPS_HPP
