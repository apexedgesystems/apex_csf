#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_EARTH_SRTM_TERRAIN_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_EARTH_SRTM_TERRAIN_MODEL_HPP
/**
 * @file SrtmTerrainModel.hpp
 * @brief Earth SRTM-flavored terrain model (htile-backed).
 *
 * Earth-specific wrapper around HtileTile that bakes in:
 *  - Default tile path under data/earth/srtm/.
 *  - Validation that the loaded htile's ref_radius is within tolerance
 *    of WGS84 + ref_surface matches "egm96".
 *
 * The htile contents are NOT restricted to NASA SRTM data -- any
 * .hgt-derived (or even procedurally-generated) Earth-flavored tile
 * works as long as it carries Earth-class metadata. This wrapper just
 * makes the "I want Earth terrain" intent discoverable + adds defensive
 * checks against accidentally loading a Mars or fictional-body tile.
 *
 * Mirror of `gravity/earth/Egm2008Model.hpp` in the spirit of the
 * gravity hierarchy.
 */

#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/earth/Wgs84TerrainConstants.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace sim {
namespace environment {
namespace terrain {
namespace earth {

/* ----------------------------- SrtmTerrainModel ----------------------------- */

/// Default path to the Earth-flavored htile (relative to apex_csf workspace
/// root). Override with `load(path)`.
inline constexpr const char* DEFAULT_SRTM_HTILE_PATH =
    "src/sim/environment/terrain/data/earth/srtm_global.htile";

class SrtmTerrainModel final : public HtileTile {
public:
  SrtmTerrainModel() noexcept = default;

  /// Load the default Earth tile path. Returns the load Status (see
  /// `loadEarth`).
  [[nodiscard]] Status loadDefault() noexcept { return loadEarth(DEFAULT_SRTM_HTILE_PATH); }

  /// Load an Earth-flavored htile from `path`. Performs the standard
  /// HtileTile load + validates header metadata is Earth-class:
  ///   - ref_radius_m within wgs84::R_TOLERANCE_M of wgs84::R_EQ_M
  ///   - ref_surface starts with wgs84::REF_SURFACE_NAME ("egm96")
  /// Propagates the HtileTile::load Status; on a successful load whose
  /// metadata is not Earth-class, closes the tile and returns
  /// `ERROR_FILE_FORMAT_INVALID`.
  [[nodiscard]] Status loadEarth(const std::string& path) noexcept {
    const Status ST = HtileTile::load(path);
    if (!isSuccess(ST)) {
      return ST;
    }
    if (!isEarthValid()) {
      close();
      return Status::ERROR_FILE_FORMAT_INVALID;
    }
    return Status::SUCCESS;
  }

  /// Returns true iff the currently-loaded htile passes Earth-flavored
  /// metadata checks. False if not loaded or metadata mismatched.
  [[nodiscard]] bool isEarthValid() const noexcept {
    if (!isLoaded())
      return false;
    if (std::abs(header().ref_radius_m - wgs84::R_EQ_M) > wgs84::R_TOLERANCE_M) {
      return false;
    }
    // Compare ref_surface (NUL-terminated, max 16 bytes) against expected.
    const std::size_t LEN = std::strlen(wgs84::REF_SURFACE_NAME);
    if (std::strncmp(header().ref_surface, wgs84::REF_SURFACE_NAME, LEN) != 0) {
      return false;
    }
    return true;
  }
};

} // namespace earth
} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_EARTH_SRTM_TERRAIN_MODEL_HPP
