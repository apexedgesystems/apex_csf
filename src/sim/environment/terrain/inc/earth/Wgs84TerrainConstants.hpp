#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_EARTH_WGS84_CONSTANTS_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_EARTH_WGS84_CONSTANTS_HPP
/**
 * @file Wgs84TerrainConstants.hpp
 * @brief WGS84 reference values used by Earth-flavored terrain models.
 *
 * Mirrors `gravity/inc/earth/Wgs84Constants.hpp`. We keep a separate
 * copy here rather than cross-include across modules so the terrain
 * library has no dependency on the gravity library.
 */

namespace sim {
namespace environment {
namespace terrain {
namespace earth {
namespace wgs84 {

/// WGS84 semi-major (equatorial) radius [m].
inline constexpr double R_EQ_M = 6378137.0;

/// WGS84 semi-minor (polar) radius [m].
inline constexpr double R_POL_M = 6356752.3142;

/// Acceptable tolerance on a tile's `ref_radius_m` to treat it as
/// Earth-flavored (allow any radius within ~10 km of the WGS84 mean).
inline constexpr double R_TOLERANCE_M = 1.0e4;

/// Reference surface name SRTM-style products are above.
inline constexpr const char* REF_SURFACE_NAME = "egm96";

} // namespace wgs84
} // namespace earth
} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_EARTH_WGS84_CONSTANTS_HPP
