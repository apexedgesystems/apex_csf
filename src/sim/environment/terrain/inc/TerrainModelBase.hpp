#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_TERRAIN_MODEL_BASE_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_TERRAIN_MODEL_BASE_HPP
/**
 * @file TerrainModelBase.hpp
 * @brief Abstract interface for terrain elevation models.
 *
 * Terrain models provide elevation queries at geodetic positions on a
 * given celestial body. Heights are reported in meters above the body's
 * declared reference surface (typically a geoid for Earth, a sphere for
 * the Moon -- the model itself doesn't enforce a particular convention;
 * the file format / consumer must agree).
 *
 * Implementations range from analytic (constant, ellipsoid) to gridded
 * (htile, future SrtmTile / LolaTile equivalents).
 */

#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- TerrainModelBase ----------------------------- */

/// Abstract base class. Derived terrain models supply geodetic and ECEF
/// elevation lookup plus coverage bounds + nominal sample resolution.
class TerrainModelBase {
public:
  virtual ~TerrainModelBase() = default;

  /// Get terrain elevation at geodetic position (lat/lon in radians).
  /// `H` is the height above the body's reference surface in meters.
  /// Returns `Status::SUCCESS` on a valid sample; a warning when the
  /// position is outside coverage (`WARN_OUTSIDE_COVERAGE`) or the sample
  /// is the void marker (`WARN_VOID_DATA`); `ERROR_NOT_INITIALIZED` when
  /// the model has no data loaded. On any non-SUCCESS result `H` is left
  /// unmodified. RT-safety depends on the implementation.
  [[nodiscard]] virtual Status elevationAt(double latRad, double lonRad,
                                           double& H) const noexcept = 0;

  /// Convenience: elevation at an ECEF position. The base implementation
  /// converts to geodetic via a spherical approximation; overrides may
  /// use a body-specific datum. Same Status semantics as `elevationAt`;
  /// a null `ecef` yields `ERROR_PARAM_BUFFER_NULL`.
  [[nodiscard]] virtual Status elevationAtEcef(const double ecef[3], double& H) const noexcept = 0;

  /// Coverage bounds in radians. RT-safe O(1) accessors.
  [[nodiscard]] virtual double minLatRad() const noexcept = 0;
  [[nodiscard]] virtual double maxLatRad() const noexcept = 0;
  [[nodiscard]] virtual double minLonRad() const noexcept = 0;
  [[nodiscard]] virtual double maxLonRad() const noexcept = 0;

  /// Approximate sample resolution in meters. Tile-based models report
  /// the per-cell ground spacing; analytic models return 0.
  [[nodiscard]] virtual double resolutionMeters() const noexcept = 0;

  /// Convenience: is this geodetic position within coverage?
  [[nodiscard]] bool isInCoverage(double latRad, double lonRad) const noexcept {
    return latRad >= minLatRad() && latRad <= maxLatRad() && lonRad >= minLonRad() &&
           lonRad <= maxLonRad();
  }
};

} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_TERRAIN_MODEL_BASE_HPP
