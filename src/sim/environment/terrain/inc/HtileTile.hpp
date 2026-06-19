#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_HTILE_TILE_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_HTILE_TILE_HPP
/**
 * @file HtileTile.hpp
 * @brief Terrain model backed by a horizon-format `.htile` file.
 *
 * Loads a single htile file and exposes the standard
 * `TerrainModelBase` query interface (geodetic / ECEF elevation, coverage
 * bounds, resolution).
 *
 * Bilinear interpolation between samples. Voids are treated as
 * "no elevation available" (`elevationAt` returns `WARN_VOID_DATA`).
 * Heights returned are `sample * scale_m_per_dn` from the htile header
 * (meters above the body's reference surface).
 *
 * @note NOT RT-safe at load(); RT-safe O(1) for elevation queries.
 */

#include "src/sim/environment/terrain/inc/Htile.hpp"
#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- HtileTile ----------------------------- */

class HtileTile : public TerrainModelBase {
public:
  HtileTile() noexcept = default;
  ~HtileTile() override = default;

  HtileTile(const HtileTile&) = delete;
  HtileTile& operator=(const HtileTile&) = delete;

  /// Load an htile file. Returns `Status::SUCCESS` on success; otherwise:
  ///   - `ERROR_DATA_PATH_INVALID` if the path is missing/unreadable;
  ///   - `ERROR_FILE_FORMAT_INVALID` on header/body structural failure
  ///     (including a void_value not representable as the sample type);
  ///   - `ERROR_SAMPLE_TYPE_UNSUPPORTED` for sample types this consumer
  ///     does not handle (currently only int16 is supported, per htile v1);
  ///   - `ERROR_ALLOC_FAIL` if the sample buffer cannot be allocated.
  [[nodiscard]] Status load(const std::string& path) noexcept;

  /// Free internal buffers; reset to default state.
  void close() noexcept;

  [[nodiscard]] bool isLoaded() const noexcept { return !samples_.empty(); }

  /// The htile header that backs this tile. Caller can read body name,
  /// ref_surface, ref_radius_m, lat/lon bounds, etc.
  [[nodiscard]] const HtileHeader& header() const noexcept { return header_; }

  /* ----------------------------- TerrainModelBase API ----------------------------- */

  [[nodiscard]] Status elevationAt(double latRad, double lonRad, double& H) const noexcept override;
  [[nodiscard]] Status elevationAtEcef(const double ecef[3], double& H) const noexcept override;
  [[nodiscard]] double minLatRad() const noexcept override;
  [[nodiscard]] double maxLatRad() const noexcept override;
  [[nodiscard]] double minLonRad() const noexcept override;
  [[nodiscard]] double maxLonRad() const noexcept override;
  [[nodiscard]] double resolutionMeters() const noexcept override { return resolution_m_; }

  /// Coverage test that honors a wraparound (0..360) longitude window: the
  /// query longitude is normalized into the tile's declared range modulo
  /// 360 deg before comparison. Shadows `TerrainModelBase::isInCoverage`.
  [[nodiscard]] bool isInCoverage(double latRad, double lonRad) const noexcept;

private:
  HtileHeader header_{};
  std::vector<std::int16_t> samples_; ///< Row-major, N->S, host-endian.
  double resolution_m_ = 0.0;         ///< Mean ground spacing (m at the equator).
};

} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_HTILE_TILE_HPP
