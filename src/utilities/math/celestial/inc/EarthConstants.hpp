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

/** @brief Gravitational parameter GM, m^3/s^2. WGS-84 defining. */
inline constexpr double GM = 3.986004418e14;

/* Zonal gravity family. The normalized C coefficients are the EGM2008
 * values exactly as distributed (tide-free system); each un-normalized
 * Jn below is -sqrt(2n+1) * Cn0 computed from its C to double
 * precision, so the pair is one dataset, not two. Zero-tide and other
 * model vintages (EGM96's J2 = 1.0826266835e-3) differ from these in
 * the fifth to sixth significant digit and are deliberately not used. */

/** @brief Fully normalized C20, EGM2008 as distributed (tide-free). */
inline constexpr double C20 = -0.484165143790815e-3;

/** @brief Fully normalized C30, EGM2008 as distributed. */
inline constexpr double C30 = 0.957161207093473e-6;

/** @brief Fully normalized C40, EGM2008 as distributed. */
inline constexpr double C40 = 0.539965866638991e-6;

/** @brief Fully normalized C50, EGM2008 as distributed. */
inline constexpr double C50 = 0.686702913736681e-7;

/** @brief Fully normalized C60, EGM2008 as distributed. */
inline constexpr double C60 = -0.149953927978527e-6;

/** @brief Un-normalized J2 = -sqrt(5) * C20. */
inline constexpr double J2 = 0.0010826261738522227;

/** @brief Un-normalized J3 = -sqrt(7) * C30. */
inline constexpr double J3 = -2.5324105185677225e-6;

/** @brief Un-normalized J4 = -3 * C40. */
inline constexpr double J4 = -1.6198975999169731e-6;

/** @brief Un-normalized J5 = -sqrt(11) * C50. */
inline constexpr double J5 = -2.2775359073083618e-7;

/** @brief Un-normalized J6 = -sqrt(13) * C60. */
inline constexpr double J6 = 5.406665762838132e-7;

/** @brief Standard gravity, m/s^2 (defined constant, BIPM). */
inline constexpr double G0 = 9.80665;

} // namespace earth
} // namespace celestial
} // namespace math
} // namespace apex

#endif // APEX_MATH_CELESTIAL_EARTH_CONSTANTS_HPP
