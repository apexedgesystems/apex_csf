#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_MOON_LUNAR_CONSTANTS_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_MOON_LUNAR_CONSTANTS_HPP
/**
 * @file LunarConstants.hpp
 * @brief Lunar reference frame and gravitational constants.
 *
 * Constants for lunar gravity modeling from the GRAIL mission.
 * Reference: GRGM1200A gravity field model (Lemoine et al., 2014)
 *
 * Data source: NASA GSFC Planetary Geodynamics Data Archive
 * https://pgda.gsfc.nasa.gov/products/50
 */

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Lunar Reference Frame ----------------------------- */

namespace lunar {

/// Mean radius of the Moon [m].
/// Reference: IAU 2015 report.
constexpr double R_MEAN = 1737400.0;

/// Reference radius for GRAIL gravity models [m].
/// This is the normalization radius used in GRGM1200A.
constexpr double R_REF = 1738000.0;

/// Moon's gravitational constant GM [m^3/s^2].
/// Reference: GRGM1200A, DE430 ephemeris.
/// Note: 4902.80011526323 km^3/s^2 = 4.90280011526323e12 m^3/s^2
constexpr double GM = 4.90280011526323e12;

/// Moon's mean angular velocity [rad/s].
/// Synchronous rotation with orbital period ~27.3 days.
constexpr double OMEGA = 2.6617e-6;

/// Moon's orbital period around Earth [s].
/// Sidereal month: ~27.32 days.
constexpr double T_ORBIT = 2360591.5;

/// Surface gravity at mean radius [m/s^2].
/// g = GM / R_MEAN^2 ~ 1.62 m/s^2.
constexpr double G_SURFACE = 1.624;

/// Moon/Earth mass ratio.
/// M_moon / M_earth ~ 0.0123.
constexpr double MASS_RATIO = 0.0123;

/// Semi-major axis of Moon's orbit around Earth [m].
/// ~384,400 km.
constexpr double A_ORBIT = 384400000.0;

} // namespace lunar

/* ----------------------------- GRAIL/GRGM1200A Zonal Harmonics ----------------------------- */

namespace grgm1200a {

/// Maximum degree/order of GRGM1200A model.
constexpr int16_t MAX_DEGREE = 1200;

/// Fully-normalized C20 coefficient (J2 term).
/// The Moon has a much smaller J2 than Earth due to slower rotation.
constexpr double C20 = 9.088124854276e-5;

/// Fully-normalized C30 coefficient.
constexpr double C30 = 3.195858098608e-6;

/// Fully-normalized C40 coefficient.
constexpr double C40 = -3.262044813618e-6;

/// Fully-normalized C22 coefficient (equatorial ellipticity).
/// Significant for the Moon due to tidal locking with Earth.
constexpr double C22 = 3.470972057788e-5;

/// Fully-normalized S22 coefficient.
constexpr double S22 = 2.441389904839e-6;

/// Un-normalized J2 = -sqrt(5) * C20.
/// Much smaller than Earth's J2 (~1.08e-3).
constexpr double J2 = 2.032e-4;

/// Kaula constraint coefficient for high degrees.
/// Power law: sigma_n = K / n^2, with K = 3.6e-4 for n > 600.
constexpr double KAULA_K = 3.6e-4;

/// Degree at which Kaula constraint is applied.
constexpr int16_t KAULA_START_DEGREE = 600;

} // namespace grgm1200a

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_MOON_LUNAR_CONSTANTS_HPP
