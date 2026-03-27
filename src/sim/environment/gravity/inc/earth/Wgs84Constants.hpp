#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_WGS84_CONSTANTS_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_WGS84_CONSTANTS_HPP
/**
 * @file Wgs84Constants.hpp
 * @brief WGS84 and EGM2008 geodetic and gravitational constants.
 *
 * Reference: NIMA TR8350.2 (WGS84 Implementation Manual)
 */

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- WGS84 Ellipsoid ----------------------------- */

namespace wgs84 {

/// Semi-major axis (equatorial radius) [m].
constexpr double A = 6378137.0;

/// Semi-minor axis (polar radius) [m].
constexpr double B = 6356752.3142;

/// Flattening f = (a - b) / a.
constexpr double F = 1.0 / 298.257223563;

/// First eccentricity squared e^2 = (a^2 - b^2) / a^2.
constexpr double E2 = 6.69437999014e-3;

/// Second eccentricity squared e'^2 = (a^2 - b^2) / b^2.
constexpr double EP2 = 6.73949674228e-3;

/// Earth's gravitational constant GM [m^3/s^2].
constexpr double GM = 3.986004418e14;

/// Earth's angular velocity [rad/s].
constexpr double OMEGA = 7.292115e-5;

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
constexpr double C20 = -0.484165143790815e-3;

/// Fully-normalized C30 coefficient.
constexpr double C30 = 0.957161207093473e-6;

/// Fully-normalized C40 coefficient.
constexpr double C40 = 0.539965866638991e-6;

/// Fully-normalized C50 coefficient.
constexpr double C50 = 0.686702913736681e-7;

/// Fully-normalized C60 coefficient.
constexpr double C60 = -0.149953927978527e-6;

/// Un-normalized J2 = -sqrt(5) * C20.
constexpr double J2 = 1.0826359e-3;

/// Un-normalized J3 = -sqrt(7) * C30.
constexpr double J3 = -2.5327e-6;

/// Un-normalized J4 = -3 * C40.
constexpr double J4 = -1.6196e-6;

} // namespace egm2008

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_WGS84_CONSTANTS_HPP
