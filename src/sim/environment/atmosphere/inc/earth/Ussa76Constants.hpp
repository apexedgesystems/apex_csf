#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_EARTH_USSA76_CONSTANTS_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_EARTH_USSA76_CONSTANTS_HPP
/**
 * @file Ussa76Constants.hpp
 * @brief Earth thermodynamic + USSA76 layer-table constants.
 *
 * The 1976 US Standard Atmosphere (NASA TM-X-74335) baseline. Defines
 * 7 layers from sea level to 86 km geopotential altitude. Reproduced
 * widely in aerospace texts; values here are the canonical published
 * numbers.
 *
 * Mirrors the role of `gravity/earth/Wgs84Constants.hpp` in the gravity
 * hierarchy and `terrain/earth/Wgs84TerrainConstants.hpp` in the terrain
 * hierarchy.
 */

#include <cstddef>

namespace sim {
namespace environment {
namespace atmosphere {
namespace earth {

/* ----------------------------- Thermodynamic constants ----------------------------- */

/// Specific gas constant for dry air [J/(kg*K)] (USSA76 = R_universal/M0).
inline constexpr double R_SPECIFIC = 287.058;

/// Ratio of specific heats (cp/cv) for air, dimensionless.
inline constexpr double GAMMA = 1.4;

/// Standard surface gravity at the geoid [m/s^2].
inline constexpr double G0 = 9.80665;

/* ----------------------------- USSA76 layer table ----------------------------- */

/// One layer of the piecewise-defined atmosphere.
struct Ussa76Layer {
  double base_alt_m;    ///< Geopotential altitude at the layer base.
  double base_T_K;      ///< Temperature at the base.
  double base_P_Pa;     ///< Pressure at the base.
  double lapse_K_per_m; ///< dT/dh within the layer (signed).
};

/// The 7 layers of USSA76 from sea level (0 km) to mesosphere top (86 km).
/// Order: troposphere -> tropopause -> stratosphere (3 sub-layers) ->
/// mesosphere (2 sub-layers).
inline constexpr Ussa76Layer LAYERS[] = {
    {0.0, 288.15, 101325.0, -0.0065},     // 0 -- troposphere
    {11000.0, 216.65, 22632.06, 0.0},     // 1 -- tropopause (isothermal)
    {20000.0, 216.65, 5474.889, 0.001},   // 2 -- stratosphere lower
    {32000.0, 228.65, 868.0187, 0.0028},  // 3 -- stratosphere upper
    {47000.0, 270.65, 110.9063, 0.0},     // 4 -- stratopause (isothermal)
    {51000.0, 270.65, 66.93887, -0.0028}, // 5 -- mesosphere lower
    {71000.0, 214.65, 3.95642, -0.002},   // 6 -- mesosphere upper (to 86 km)
};

inline constexpr std::size_t NUM_LAYERS = sizeof(LAYERS) / sizeof(LAYERS[0]);

/// USSA76 is documented up to 86 km geopotential. Above this empirical
/// thermospheric models (NRLMSISE-00 et al) are required.
inline constexpr double TOP_OF_TABLE_M = 86000.0;

} // namespace earth
} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_EARTH_USSA76_CONSTANTS_HPP
