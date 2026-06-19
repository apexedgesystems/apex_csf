#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_MOON_LUNAR_CONSTANTS_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_MOON_LUNAR_CONSTANTS_HPP
/**
 * @file LunarTerrainConstants.hpp
 * @brief Reference values used by Moon-flavored terrain models.
 *
 * Mirrors `gravity/inc/moon/LunarConstants.hpp`. Local copy keeps the
 * terrain library independent of gravity at the link level.
 */

namespace sim {
namespace environment {
namespace terrain {
namespace moon {
namespace lunar {

/// LOLA / SLDEM2015 reference sphere radius [m]. Used by both LOLA's
/// global ldem product and SLDEM2015's higher-res tiles.
inline constexpr double R_REF_M = 1737400.0;

/// Acceptable tolerance on a tile's `ref_radius_m` to treat it as
/// Moon-flavored.
inline constexpr double R_TOLERANCE_M = 1.0e4;

/// Reference surface name LOLA / SLDEM2015 products are above.
inline constexpr const char* REF_SURFACE_NAME = "sphere";

} // namespace lunar
} // namespace moon
} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_MOON_LUNAR_CONSTANTS_HPP
