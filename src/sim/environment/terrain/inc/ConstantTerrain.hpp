#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_CONSTANT_TERRAIN_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_CONSTANT_TERRAIN_HPP
/**
 * @file ConstantTerrain.hpp
 * @brief Constant-elevation terrain model: H = h0 everywhere within coverage.
 *
 * Body-agnostic baseline. Useful as a fallback (when no DEM is available)
 * or for systems testing terrain-following at a fixed altitude reference.
 *
 * Mirrors `gravity/ConstantGravityModel.hpp` in the spirit of the
 * fidelity ladder: simplest possible model, header-only, RT-safe.
 */

#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"

#include <cmath>
#include <cstdint>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- Constants ----------------------------- */

/// Default reference height [m]: sea level (above whatever ref surface).
constexpr double DEFAULT_H0 = 0.0;

/* ----------------------------- ConstantTerrain ----------------------------- */

/// Returns a fixed elevation regardless of position. Coverage is global
/// (all lat/lon).
class ConstantTerrain final : public TerrainModelBase {
public:
  ConstantTerrain() noexcept = default;
  explicit ConstantTerrain(double h0) noexcept : h0_(h0) {}

  /// Update the constant elevation in place.
  void setElevation(double h0) noexcept { h0_ = h0; }
  [[nodiscard]] double elevation() const noexcept { return h0_; }

  /* ----------------------------- TerrainModelBase API ----------------------------- */

  [[nodiscard]] Status elevationAt(double /*latRad*/, double /*lonRad*/,
                                   double& H) const noexcept override {
    H = h0_;
    return Status::SUCCESS;
  }

  [[nodiscard]] Status elevationAtEcef(const double /*ecef*/[3],
                                       double& H) const noexcept override {
    H = h0_;
    return Status::SUCCESS;
  }

  [[nodiscard]] double minLatRad() const noexcept override { return -1.5707963267948966; }
  [[nodiscard]] double maxLatRad() const noexcept override { return 1.5707963267948966; }
  [[nodiscard]] double minLonRad() const noexcept override { return -3.141592653589793; }
  [[nodiscard]] double maxLonRad() const noexcept override { return 3.141592653589793; }
  [[nodiscard]] double resolutionMeters() const noexcept override { return 0.0; }

private:
  double h0_ = DEFAULT_H0;
};

} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_CONSTANT_TERRAIN_HPP
