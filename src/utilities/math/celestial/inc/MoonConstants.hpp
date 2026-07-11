#ifndef APEX_MATH_CELESTIAL_MOON_CONSTANTS_HPP
#define APEX_MATH_CELESTIAL_MOON_CONSTANTS_HPP
/**
 * @file MoonConstants.hpp
 * @brief Canonical Moon geometry and rotation constants.
 *
 * Geometry per the IAU 2015 recommended values. The GRAIL gravity-model
 * normalization radius (1738000 m) is NOT here by design: it parameterizes
 * a spherical-harmonic expansion, not the body's geometry, and lives with
 * the gravity model. Mixing the two produced the tree's two-lunar-radii
 * split; this header is the geometry side of that resolution.
 *
 * The Moon is tidally locked: the rotation period equals the sidereal
 * orbital period, so OMEGA = 2*pi / T_SIDEREAL drives the MCI->MCMF frame
 * edge.
 *
 * @note RT-SAFE: constexpr constants only.
 */

namespace apex {
namespace math {
namespace celestial {
namespace moon {

/** @brief Mean radius, meters (IAU 2015). */
inline constexpr double R_MEAN = 1737400.0;

/** @brief Sidereal rotation/orbital period, seconds (27.321661 days). */
inline constexpr double T_SIDEREAL = 2360591.5;

/** @brief Rotation rate, rad/s. Derived: 2*pi / T_SIDEREAL. */
inline constexpr double OMEGA = 2.6617e-6;

} // namespace moon
} // namespace celestial
} // namespace math
} // namespace apex

#endif // APEX_MATH_CELESTIAL_MOON_CONSTANTS_HPP
