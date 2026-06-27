#ifndef APEX_SIM_SENSORS_PITOT_HPP
#define APEX_SIM_SENSORS_PITOT_HPP
/**
 * @file Pitot.hpp
 * @brief Pitot tube indicated-airspeed model.
 *
 * A Pitot-static system measures dynamic pressure q = 0.5 * rho * V^2. The air
 * data computer derives indicated airspeed (IAS) by assuming sea-level density:
 *
 *   V_indicated = sqrt(2 * q_measured / rho_SL)
 *
 * Because true density falls with altitude, at cruise the measured q is lower
 * than sea-level intuition expects, so IAS reads well below true airspeed (an
 * airliner at ~240 m/s true at ~12 km shows only ~119 m/s indicated). This is
 * the airspeed a pressure-based controller naturally sees, which is why the model
 * reports IAS rather than true airspeed.
 *
 * Error model on the measured dynamic pressure:
 *   - multiplicative noise ~ 1% of q (typical Pitot accuracy);
 *   - additive calibration bias (a few Pa).
 */

#include "src/sim/sensors/inc/SensorBase.hpp"

#include <cmath>
#include <cstdint>

namespace sim::sensors {

struct PitotParams {
  double rho_SL_kg_m3 = 1.225; // sea-level reference density
  double noise_q_pct = 0.01;   // multiplicative noise, fraction of true q
  double bias_q_Pa = 0.0;      // additive bias on dynamic pressure
  std::uint32_t seed = 0x12340001u;
};

class Pitot : public SensorBase {
public:
  explicit Pitot(const PitotParams& p = {}) noexcept
      : SensorBase(SensorKind::AirData, "pitot", p.seed), p_(p) {}

  /**
   * @brief Indicated airspeed from true airspeed + local density.
   * @return IAS in m/s, clamped to non-negative.
   */
  [[nodiscard]] double indicatedAirspeed(double V_true_m_s, double rho_kg_m3) noexcept {
    const double q_true = 0.5 * rho_kg_m3 * V_true_m_s * V_true_m_s;
    const double noise_q = sampler_.gaussian() * p_.noise_q_pct * q_true;
    const double q_meas = q_true + noise_q + p_.bias_q_Pa;
    if (q_meas <= 0.0 || p_.rho_SL_kg_m3 <= 0.0) {
      return 0.0;
    }
    return std::sqrt(2.0 * q_meas / p_.rho_SL_kg_m3);
  }

  [[nodiscard]] const PitotParams& params() const noexcept { return p_; }

private:
  PitotParams p_;
};

} // namespace sim::sensors

#endif // APEX_SIM_SENSORS_PITOT_HPP
