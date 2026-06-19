#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_SPHERE_TERRAIN_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_SPHERE_TERRAIN_HPP
/**
 * @file SphereTerrain.hpp
 * @brief Spherical-body terrain model: H = 0 (samples lie on the sphere).
 *
 * Body-agnostic. Treats the body as a perfect sphere of radius `r_eq_m`;
 * elevation is therefore zero everywhere within coverage. Useful as the
 * "no terrain detail" baseline for spherical bodies (analogous to
 * gravity's J2GravityModel: a step up from constant, still O(1)).
 *
 * For ECEF queries, validates the radial distance against the configured
 * sphere radius (within a tolerance) so callers can detect "below
 * surface" gracefully.
 */

#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"

#include <cmath>
#include <cstdint>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- SphereTerrain ----------------------------- */

class SphereTerrain final : public TerrainModelBase {
public:
  SphereTerrain() noexcept = default;
  explicit SphereTerrain(double r_eq_m) noexcept : r_eq_m_(r_eq_m) {}

  void setRadius(double r_eq_m) noexcept { r_eq_m_ = r_eq_m; }
  [[nodiscard]] double radius() const noexcept { return r_eq_m_; }

  /* ----------------------------- TerrainModelBase API ----------------------------- */

  /// Geodetic query: surface is at H=0 by definition.
  [[nodiscard]] Status elevationAt(double /*latRad*/, double /*lonRad*/,
                                   double& H) const noexcept override {
    H = 0.0;
    return Status::SUCCESS;
  }

  /// ECEF query: returns the radial offset from the configured sphere.
  /// Positive H means above the sphere; negative means below.
  [[nodiscard]] Status elevationAtEcef(const double ecef[3], double& H) const noexcept override {
    if (ecef == nullptr) {
      return Status::ERROR_PARAM_BUFFER_NULL;
    }
    const double R = std::sqrt(ecef[0] * ecef[0] + ecef[1] * ecef[1] + ecef[2] * ecef[2]);
    H = R - r_eq_m_;
    return Status::SUCCESS;
  }

  [[nodiscard]] double minLatRad() const noexcept override { return -1.5707963267948966; }
  [[nodiscard]] double maxLatRad() const noexcept override { return 1.5707963267948966; }
  [[nodiscard]] double minLonRad() const noexcept override { return -3.141592653589793; }
  [[nodiscard]] double maxLonRad() const noexcept override { return 3.141592653589793; }
  [[nodiscard]] double resolutionMeters() const noexcept override { return 0.0; }

private:
  double r_eq_m_ = 1.0;
};

} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_SPHERE_TERRAIN_HPP
