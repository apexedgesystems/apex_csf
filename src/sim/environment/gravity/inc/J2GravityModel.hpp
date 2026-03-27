#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_J2_GRAVITY_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_J2_GRAVITY_MODEL_HPP
/**
 * @file J2GravityModel.hpp
 * @brief J2-only gravity model for fast oblateness-corrected gravity.
 *
 * The J2 term captures ~99% of a body's oblateness effect on gravity.
 * This model is O(1) and ideal for orbit propagation where full spherical
 * harmonic fidelity is not required.
 *
 * Body-agnostic: Requires explicit parameters for the target body.
 * For Earth, use values from earth/Wgs84Constants.hpp.
 * For Moon, use values from moon/LunarConstants.hpp.
 */

#include "src/sim/environment/gravity/inc/GravityModelBase.hpp"

#include <cmath>
#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- J2Params ----------------------------- */

/**
 * @brief Parameters for J2 gravity model.
 *
 * All parameters required - no defaults provided. Use body-specific
 * constants (e.g., wgs84::GM, lunar::GM) when initializing.
 */
struct J2Params {
  double GM = 0.0; ///< Gravitational constant [m^3/s^2]. Required.
  double a = 0.0;  ///< Reference radius (equatorial) [m]. Required.
  double J2 = 0.0; ///< Un-normalized J2 coefficient. Required.
};

/* ----------------------------- J2GravityModel ----------------------------- */

/**
 * @brief Fast J2-only gravity model.
 *
 * Computes gravitational potential and acceleration including only the
 * central term and the J2 (oblateness) perturbation. Body-agnostic:
 * works for any body with provided GM, reference radius, and J2.
 *
 * Potential:
 *   V = (GM/r) * [1 - J2*(a/r)^2 * P2(sin(phi))]
 *
 * where P2(x) = (3x^2 - 1)/2 is the Legendre polynomial of degree 2,
 * and sin(phi) = z/r is the sine of body-centric latitude.
 *
 * @note RT-safe: No allocation, O(1) operations after init().
 */
class J2GravityModel final : public GravityModelBase {
public:
  J2GravityModel() noexcept = default;

  /**
   * @brief Construct with explicit parameters.
   * @param params J2 model parameters (GM, a, J2 required).
   * @note RT-safe: No allocation.
   */
  explicit J2GravityModel(const J2Params& params) noexcept
      : GM_(params.GM), a_(params.a), J2_(params.J2) {}

  /**
   * @brief Initialize with parameters.
   * @param params J2 model parameters (GM, a, J2 required).
   * @return Always true.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] bool init(const J2Params& params) noexcept {
    GM_ = params.GM;
    a_ = params.a;
    J2_ = params.J2;
    return true;
  }

  /**
   * @brief Compute J2 gravitational potential.
   *
   * V = (GM/r) * [1 - J2*(a/r)^2 * (3*sin^2(phi) - 1)/2]
   *
   * @param r Position in body-fixed frame [m].
   * @param V Output potential [m^2/s^2].
   * @return true unless |r| == 0.
   * @note RT-safe: No allocation, O(1).
   */
  bool potential(const double r[3], double& V) const noexcept override {
    const double R2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
    if (R2 == 0.0) {
      V = 0.0;
      return false;
    }

    const double RMAG = std::sqrt(R2);
    const double INVR = 1.0 / RMAG;
    const double A_OVER_R = a_ * INVR;
    const double A_OVER_R2 = A_OVER_R * A_OVER_R;

    // sin(phi) = z/r
    const double SIN_PHI = r[2] * INVR;
    const double SIN_PHI2 = SIN_PHI * SIN_PHI;

    // P2(sin(phi)) = (3*sin^2(phi) - 1) / 2
    const double P2 = (3.0 * SIN_PHI2 - 1.0) * 0.5;

    // V = (GM/r) * [1 - J2*(a/r)^2 * P2]
    V = (GM_ * INVR) * (1.0 - J2_ * A_OVER_R2 * P2);

    return true;
  }

  /**
   * @brief Compute J2 gravitational acceleration.
   *
   * Closed-form acceleration in body-fixed frame:
   *   ax = -(GM*x/r^3) * [1 + 1.5*J2*(a/r)^2 * (1 - 5*(z/r)^2)]
   *   ay = -(GM*y/r^3) * [1 + 1.5*J2*(a/r)^2 * (1 - 5*(z/r)^2)]
   *   az = -(GM*z/r^3) * [1 + 1.5*J2*(a/r)^2 * (3 - 5*(z/r)^2)]
   *
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

    const double RMAG = std::sqrt(R2);
    const double INVR = 1.0 / RMAG;
    const double INVR2 = INVR * INVR;
    const double INVR3 = INVR2 * INVR;
    const double A_OVER_R2 = (a_ * a_) * INVR2;

    // (z/r)^2
    const double Z_OVER_R2 = (r[2] * r[2]) * INVR2;

    // Common J2 factor: 1.5 * J2 * (a/r)^2
    const double J2_FACTOR = 1.5 * J2_ * A_OVER_R2;

    // Radial and axial factors
    const double RADIAL_FACTOR = 1.0 + J2_FACTOR * (1.0 - 5.0 * Z_OVER_R2);
    const double AXIAL_FACTOR = 1.0 + J2_FACTOR * (3.0 - 5.0 * Z_OVER_R2);

    // Base acceleration: -GM/r^3 * r
    const double GM_INVR3 = -GM_ * INVR3;

    a[0] = GM_INVR3 * r[0] * RADIAL_FACTOR;
    a[1] = GM_INVR3 * r[1] * RADIAL_FACTOR;
    a[2] = GM_INVR3 * r[2] * AXIAL_FACTOR;

    return true;
  }

  /**
   * @brief Returns 2 (uses degree 2 zonal harmonic).
   * @note RT-safe: O(1).
   */
  int16_t maxDegree() const noexcept override { return 2; }

  /// @brief Get current GM value [m^3/s^2].
  /// @note RT-safe: O(1).
  double GM() const noexcept { return GM_; }

  /// @brief Get current reference radius [m].
  /// @note RT-safe: O(1).
  double refRadius() const noexcept { return a_; }

  /// @brief Get current J2 coefficient.
  /// @note RT-safe: O(1).
  double J2() const noexcept { return J2_; }

private:
  double GM_ = 0.0;
  double a_ = 0.0;
  double J2_ = 0.0;
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_J2_GRAVITY_MODEL_HPP
