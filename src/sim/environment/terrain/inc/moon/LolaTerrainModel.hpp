#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_MOON_LOLA_TERRAIN_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_MOON_LOLA_TERRAIN_MODEL_HPP
/**
 * @file LolaTerrainModel.hpp
 * @brief Moon LOLA-flavored terrain model (htile-backed).
 *
 * Moon-specific wrapper around HtileTile that bakes in:
 *  - Default tile path under data/moon/lola/.
 *  - Validation that the loaded htile's ref_radius is within tolerance
 *    of the lunar reference sphere + ref_surface matches "sphere".
 *
 * The htile contents are NOT restricted to NASA LOLA data -- any PDS
 * IMG-derived (or even procedurally-generated) Moon-flavored tile works
 * as long as it carries lunar-class metadata.
 *
 * Mirror of `gravity/moon/GrailModel.hpp` in the gravity hierarchy.
 */

#include "src/sim/environment/terrain/inc/HtileTile.hpp"
#include "src/sim/environment/terrain/inc/moon/LunarTerrainConstants.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace sim {
namespace environment {
namespace terrain {
namespace moon {

/* ----------------------------- LolaTerrainModel ----------------------------- */

/// Default path to the Moon-flavored htile (relative to apex_csf workspace
/// root). Override with `load(path)`.
inline constexpr const char* DEFAULT_LOLA_HTILE_PATH =
    "src/sim/environment/terrain/data/moon/lola_global.htile";

class LolaTerrainModel final : public HtileTile {
public:
  LolaTerrainModel() noexcept = default;

  /// Load the default Moon tile path. Returns the load Status (see
  /// `loadMoon`).
  [[nodiscard]] Status loadDefault() noexcept { return loadMoon(DEFAULT_LOLA_HTILE_PATH); }

  /// Load a Moon-flavored htile from `path`. Performs the standard
  /// HtileTile load + validates header metadata is lunar-class:
  ///   - ref_radius_m within lunar::R_TOLERANCE_M of lunar::R_REF_M
  ///   - ref_surface starts with lunar::REF_SURFACE_NAME ("sphere")
  /// Propagates the HtileTile::load Status; on a successful load whose
  /// metadata is not lunar-class, closes the tile and returns
  /// `ERROR_FILE_FORMAT_INVALID`.
  [[nodiscard]] Status loadMoon(const std::string& path) noexcept {
    const Status ST = HtileTile::load(path);
    if (!isSuccess(ST)) {
      return ST;
    }
    if (!isMoonValid()) {
      close();
      return Status::ERROR_FILE_FORMAT_INVALID;
    }
    return Status::SUCCESS;
  }

  /// Returns true iff the currently-loaded htile passes Moon-flavored
  /// metadata checks.
  [[nodiscard]] bool isMoonValid() const noexcept {
    if (!isLoaded())
      return false;
    if (std::abs(header().ref_radius_m - lunar::R_REF_M) > lunar::R_TOLERANCE_M) {
      return false;
    }
    const std::size_t LEN = std::strlen(lunar::REF_SURFACE_NAME);
    if (std::strncmp(header().ref_surface, lunar::REF_SURFACE_NAME, LEN) != 0) {
      return false;
    }
    return true;
  }
};

} // namespace moon
} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_MOON_LOLA_TERRAIN_MODEL_HPP
