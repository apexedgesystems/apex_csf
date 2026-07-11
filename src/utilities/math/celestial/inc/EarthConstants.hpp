#ifndef APEX_MATH_CELESTIAL_EARTH_CONSTANTS_HPP
#define APEX_MATH_CELESTIAL_EARTH_CONSTANTS_HPP
/**
 * @file EarthConstants.hpp
 * @brief Canonical Earth geometry and rotation constants (WGS-84).
 *
 * THE definitions for Earth's reference ellipsoid geometry and rotation
 * rate. Values are the WGS-84 defining and derived parameters (NIMA
 * TR8350.2). Gravity-FIELD parameters (GM, J2 tables, normal-gravity
 * coefficients, geoid models) deliberately live with the gravity models --
 * this header carries geometry, not field physics.
 *
 * Derived values are spelled as literals (not computed) so the header stays
 * a table of facts; the unit tests verify the internal consistency
 * (E2 = F(2-F), B = A(1-F)) instead.
 *
 * @note RT-SAFE: constexpr constants only.
 */

namespace apex {
namespace math {
namespace celestial {
namespace earth {

/** @brief Semi-major axis (equatorial radius), meters. WGS-84 defining. */
inline constexpr double A = 6378137.0;

/** @brief Flattening (defining: 1/298.257223563). */
inline constexpr double F = 1.0 / 298.257223563;

/** @brief Semi-minor axis (polar radius), meters. Derived: A(1-F). */
inline constexpr double B = 6356752.3142;

/** @brief First eccentricity squared. Derived: F(2-F). */
inline constexpr double E2 = 6.69437999014e-3;

/** @brief Second eccentricity squared. Derived: E2/(1-E2). */
inline constexpr double EP2 = 6.73949674228e-3;

/** @brief Rotation rate about +Z (inertial), rad/s. WGS-84 nominal. */
inline constexpr double OMEGA = 7.292115e-5;

/** @brief Standard gravity, m/s^2 (defined constant, BIPM). */
inline constexpr double G0 = 9.80665;

} // namespace earth
} // namespace celestial
} // namespace math
} // namespace apex

#endif // APEX_MATH_CELESTIAL_EARTH_CONSTANTS_HPP
