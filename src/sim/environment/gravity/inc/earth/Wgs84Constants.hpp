#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_WGS84_CONSTANTS_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_WGS84_CONSTANTS_HPP
/**
 * @file Wgs84Constants.hpp
 * @brief WGS84 and EGM2008 geodetic and gravitational constants.
 *
 * Reference: NIMA TR8350.2 (WGS84 Implementation Manual)
 */

#include "src/utilities/math/celestial/inc/EarthConstants.hpp"

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- WGS84 Ellipsoid ----------------------------- */

namespace wgs84 {

/// Semi-major axis (equatorial radius) [m].
constexpr double A = apex::math::celestial::earth::A;

/// Semi-minor axis (polar radius) [m].
constexpr double B = apex::math::celestial::earth::B;

/// Flattening f = (a - b) / a.
constexpr double F = apex::math::celestial::earth::F;

/// First eccentricity squared e^2 = (a^2 - b^2) / a^2.
constexpr double E2 = apex::math::celestial::earth::E2;

/// Second eccentricity squared e'^2 = (a^2 - b^2) / b^2.
constexpr double EP2 = apex::math::celestial::earth::EP2;

/// Earth's gravitational constant GM [m^3/s^2].
constexpr double GM = apex::math::celestial::earth::GM;

/// Earth's angular velocity [rad/s].
constexpr double OMEGA = apex::math::celestial::earth::OMEGA;

/// Normal gravity at equator [m/s^2].
constexpr double GAMMA_E = 9.7803253359;

/// Normal gravity at poles [m/s^2].
constexpr double GAMMA_P = 9.8321849378;

/// Somigliana formula constant k = (b*gamma_p - a*gamma_e) / (a*gamma_e).
constexpr double SOMIGLIANA_K = 0.00193185265241;

} // namespace wgs84

/* ----------------------------- EGM2008 Zonal Harmonics ----------------------------- */

namespace egm2008 {

/// Fully-normalized C20 coefficient (J2 = -sqrt(5)*C20).
constexpr double C20 = apex::math::celestial::earth::C20;

/// Fully-normalized C30 coefficient.
constexpr double C30 = apex::math::celestial::earth::C30;

/// Fully-normalized C40 coefficient.
constexpr double C40 = apex::math::celestial::earth::C40;

/// Fully-normalized C50 coefficient.
constexpr double C50 = apex::math::celestial::earth::C50;

/// Fully-normalized C60 coefficient.
constexpr double C60 = apex::math::celestial::earth::C60;

/// Un-normalized J2 = -sqrt(5) * C20 (EGM2008 tide-free, canonical).
constexpr double J2 = apex::math::celestial::earth::J2;

/// Un-normalized J3 = -sqrt(7) * C30 (EGM2008 tide-free, canonical).
constexpr double J3 = apex::math::celestial::earth::J3;

/// Un-normalized J4 = -3 * C40 (EGM2008 tide-free, canonical).
constexpr double J4 = apex::math::celestial::earth::J4;

} // namespace egm2008

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_WGS84_CONSTANTS_HPP
