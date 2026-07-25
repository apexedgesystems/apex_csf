#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_MOON_LUNAR_CONSTANTS_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_MOON_LUNAR_CONSTANTS_HPP

#include "src/utilities/math/celestial/inc/MoonConstants.hpp"
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

#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Lunar Reference Frame ----------------------------- */

namespace lunar {

/// Mean radius of the Moon [m].
/// Reference: IAU 2015 report.
constexpr double R_MEAN = apex::math::celestial::moon::R_MEAN;

/// Reference radius for GRAIL gravity models [m].
/// This is the normalization radius used in GRGM1200A.
constexpr double R_REF = apex::math::celestial::moon::R_REF;

/// Moon's gravitational constant GM [m^3/s^2].
/// Reference: GRGM1200A, DE430 ephemeris.
/// Note: 4902.80011526323 km^3/s^2 = 4.90280011526323e12 m^3/s^2
constexpr double GM = apex::math::celestial::moon::GM;

/// Moon's mean angular velocity [rad/s].
/// Synchronous rotation with orbital period ~27.3 days.
constexpr double OMEGA = apex::math::celestial::moon::OMEGA;

/// Moon's orbital period around Earth [s].
/// Sidereal month: ~27.32 days.
constexpr double T_ORBIT = apex::math::celestial::moon::T_SIDEREAL;

/// Surface gravity at mean radius [m/s^2]. Derived: GM / R_MEAN^2.
constexpr double G_SURFACE = apex::math::celestial::moon::G_SURFACE;

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

/// Fully-normalized C20 coefficient (J2 term), as distributed
/// (principal-axes frame).
constexpr double C20 = apex::math::celestial::moon::C20;

/// Fully-normalized C30 coefficient, as distributed.
constexpr double C30 = -3.1973308084610398e-6;

/// Fully-normalized C40 coefficient, as distributed.
constexpr double C40 = 3.2347808442570100e-6;

/// Fully-normalized C22 coefficient (equatorial ellipticity), as
/// distributed (principal-axes frame; frame choice moves degree-2
/// sectorials far more than any tide term).
constexpr double C22 = 3.4673096470696298e-5;

/// Fully-normalized S22 coefficient, as distributed (near zero by
/// construction in the principal-axes frame).
constexpr double S22 = 9.0791898343722900e-10;

/// Un-normalized J2 = -sqrt(5) * C20, computed from the distributed
/// C20. Much smaller than Earth's J2 (~1.08e-3).
constexpr double J2 = apex::math::celestial::moon::J2;

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
