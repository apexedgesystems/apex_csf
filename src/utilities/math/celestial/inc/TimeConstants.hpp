#ifndef APEX_MATH_CELESTIAL_TIME_CONSTANTS_HPP
#define APEX_MATH_CELESTIAL_TIME_CONSTANTS_HPP
/**
 * @file TimeConstants.hpp
 * @brief Canonical epoch and day-length constants.
 *
 * The J2000.0 reference epoch and the Earth sidereal-angle anchors for the
 * rung-1 inertial realization: Greenwich mean sidereal angle as a LINEAR
 * function of days since J2000 (the classical linear GMST term; higher-order
 * terms and precession/nutation are explicitly beyond rung 1).
 *
 * @note RT-SAFE: constexpr constants only.
 */

namespace apex {
namespace math {
namespace celestial {

/** @brief Julian date of the J2000.0 epoch (2000-01-01 12:00 TT). */
inline constexpr double JD_J2000 = 2451545.0;

/** @brief Seconds per day (SI). */
inline constexpr double SECONDS_PER_DAY = 86400.0;

namespace earth {

/** @brief Greenwich mean sidereal angle at J2000.0, radians (280.46061837 deg). */
inline constexpr double GMST_AT_J2000_RAD = 4.894961212823756;

/** @brief GMST advance per day, radians (360.98564736629 deg/day). */
inline constexpr double GMST_RATE_RAD_PER_DAY = 6.300388098984891;

} // namespace earth

} // namespace celestial
} // namespace math
} // namespace apex

#endif // APEX_MATH_CELESTIAL_TIME_CONSTANTS_HPP
