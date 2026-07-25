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

/** @brief Mean radius, meters (IAU 2015). Also LOLA/SLDEM2015's
 *  reference sphere. Distinct from R_REF. */
inline constexpr double R_MEAN = 1737400.0;

/** @brief GRGM1200A normalization (reference) radius, meters. A gravity
 *  model convention, deliberately not the physical mean radius. */
inline constexpr double R_REF = 1738000.0;

/** @brief Sidereal rotation/orbital period, seconds (27.321661 days). */
inline constexpr double T_SIDEREAL = 2360591.5;

/** @brief Rotation rate, rad/s. Derived: 2*pi / T_SIDEREAL. */
inline constexpr double OMEGA = 2.6617e-6;

/** @brief Gravitational parameter GM, m^3/s^2. GRGM1200A as stated by
 *  the product label. */
inline constexpr double GM = 4.90280011526323e12;

/** @brief Fully normalized C20, GRGM1200A as distributed
 *  (principal-axes frame). */
inline constexpr double C20 = -9.0884339347424299e-5;

/** @brief Un-normalized J2 = -sqrt(5) * C20, computed to double
 *  precision from the distributed C20. */
inline constexpr double J2 = 0.00020322356087099962;

/** @brief Surface gravity, m/s^2. Derived: GM / R_MEAN^2, computed to
 *  double precision; not an independent measurement. */
inline constexpr double G_SURFACE = 1.624218875654165;

} // namespace moon
} // namespace celestial
} // namespace math
} // namespace apex

#endif // APEX_MATH_CELESTIAL_MOON_CONSTANTS_HPP
