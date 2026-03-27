#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_HEIGHT_CONVERSIONS_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_HEIGHT_CONVERSIONS_HPP
/**
 * @file HeightConversions.hpp
 * @brief Height conversion utilities between ellipsoid, geoid, and orthometric heights.
 *
 * Height relationships:
 *   h = ellipsoid height (GPS measurement)
 *   H = orthometric height (height above geoid / MSL)
 *   N = geoid undulation (geoid - ellipsoid separation)
 *
 *   h = H + N
 *
 * For terrain/altitude calculations:
 *   - GPS gives ellipsoid height (h)
 *   - To get altitude above sea level (H): H = h - N
 *   - Geoid undulation (N) is computed by GeoidModel
 */

#include "src/sim/environment/gravity/inc/earth/Geodetic.hpp"

#include <cmath>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Height Conversions ----------------------------- */

/**
 * @brief Convert ellipsoid height to orthometric height.
 *
 * @param h Ellipsoid height (GPS) [m].
 * @param N Geoid undulation at position [m].
 * @return Orthometric height (above MSL) [m].
 * @note RT-safe: O(1), no allocation.
 */
inline double ellipsoidToOrthometric(double h, double N) noexcept { return h - N; }

/**
 * @brief Convert orthometric height to ellipsoid height.
 *
 * @param H Orthometric height (above MSL) [m].
 * @param N Geoid undulation at position [m].
 * @return Ellipsoid height (GPS) [m].
 * @note RT-safe: O(1), no allocation.
 */
inline double orthometricToEllipsoid(double H, double N) noexcept { return H + N; }

/**
 * @brief Compute orthometric height from ECEF position.
 *
 * Converts ECEF to geodetic, then subtracts geoid undulation.
 *
 * @param ecef ECEF position [m] (x, y, z).
 * @param N Geoid undulation at the position [m].
 * @return Orthometric height (above MSL) [m].
 * @note RT-safe: O(1), no allocation.
 */
inline double ecefToOrthometricHeight(const double ecef[3], double N) noexcept {
  double lat = 0.0, lon = 0.0, h = 0.0;
  ecefToGeodetic(ecef, lat, lon, h);
  return ellipsoidToOrthometric(h, N);
}

/**
 * @brief Compute ellipsoid height from ECEF position.
 *
 * Convenience wrapper around ecefToGeodetic.
 *
 * @param ecef ECEF position [m] (x, y, z).
 * @return Ellipsoid height [m].
 * @note RT-safe: O(1), no allocation.
 */
inline double ecefToEllipsoidHeight(const double ecef[3]) noexcept {
  double lat = 0.0, lon = 0.0, h = 0.0;
  ecefToGeodetic(ecef, lat, lon, h);
  return h;
}

/**
 * @brief Compute altitude above ground level (AGL).
 *
 * @param H Orthometric height of object [m].
 * @param terrainH Terrain elevation (orthometric) at position [m].
 * @return Altitude above ground [m].
 * @note RT-safe: O(1), no allocation.
 */
inline double orthometricToAgl(double H, double terrainH) noexcept { return H - terrainH; }

/**
 * @brief Compute orthometric height from AGL and terrain.
 *
 * @param agl Altitude above ground [m].
 * @param terrainH Terrain elevation (orthometric) at position [m].
 * @return Orthometric height [m].
 * @note RT-safe: O(1), no allocation.
 */
inline double aglToOrthometric(double agl, double terrainH) noexcept { return agl + terrainH; }

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_HEIGHT_CONVERSIONS_HPP
