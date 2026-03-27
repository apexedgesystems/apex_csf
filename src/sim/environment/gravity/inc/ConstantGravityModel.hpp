#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_CONSTANT_GRAVITY_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_CONSTANT_GRAVITY_MODEL_HPP
/**
 * @file ConstantGravityModel.hpp
 * @brief Constant radial gravity model: a = -g0 * r_hat, V = g0 * |r|.
 *
 * Body-agnostic: Works for any celestial body.
 */

#include "src/sim/environment/gravity/inc/GravityModelBase.hpp"

#include <cmath>
#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Constants ----------------------------- */

/// Default gravitational acceleration [m/s^2].
constexpr double DEFAULT_G0 = 9.80665;

/* ----------------------------- ConstantGravityModel ----------------------------- */

/**
 * @brief Simple constant-g radial gravity model.
 *
 * Acceleration is directed radially inward with constant magnitude g0.
 * Useful as a baseline, fallback, or for systems not requiring harmonics.
 *
 * @note RT-safe: No allocation, O(1) operations.
 */
class ConstantGravityModel final : public GravityModelBase {
public:
  ConstantGravityModel() noexcept = default;
  explicit ConstantGravityModel(double g0) noexcept : g0_(g0) {}

  /**
   * @brief Initialize with a specific g0 value.
   * @param g0 Gravitational acceleration magnitude [m/s^2].
   * @return Always true.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] bool init(double g0) noexcept {
    g0_ = g0;
    return true;
  }

  /**
   * @brief Compute potential V = g0 * |r|.
   * @param r Position in body-fixed frame [m].
   * @param V Output potential [m^2/s^2].
   * @return Always true.
   * @note RT-safe: No allocation, O(1).
   */
  bool potential(const double r[3], double& V) const noexcept override {
    const double RMAG = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    V = g0_ * RMAG;
    return true;
  }

  /**
   * @brief Compute acceleration a = -g0 * r_hat.
   * @param r Position in body-fixed frame [m].
   * @param a Output acceleration [m/s^2].
   * @return true unless |r| == 0.
   * @note RT-safe: No allocation, O(1).
   */
  bool acceleration(const double r[3], double a[3]) const noexcept override {
    const double R2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
    if (R2 == 0.0) {
      a[0] = a[1] = a[2] = 0.0;
      return false;
    }
    const double INVR = 1.0 / std::sqrt(R2);
    const double S = -g0_ * INVR;
    a[0] = S * r[0];
    a[1] = S * r[1];
    a[2] = S * r[2];
    return true;
  }

  /**
   * @brief Returns 0 (no spherical harmonics).
   * @note RT-safe: O(1).
   */
  int16_t maxDegree() const noexcept override { return 0; }

private:
  double g0_ = DEFAULT_G0;
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_CONSTANT_GRAVITY_MODEL_HPP
