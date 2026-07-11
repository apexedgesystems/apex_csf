#ifndef APEX_MATH_CELESTIAL_ANGLES_HPP
#define APEX_MATH_CELESTIAL_ANGLES_HPP
/**
 * @file Angles.hpp
 * @brief Angle constants and degree/radian conversion factors.
 *
 * The one definition of pi and the degree<->radian factors; consumers
 * reference these instead of restating M_PI/180 locally.
 *
 * @note RT-SAFE: constexpr constants only.
 */

namespace apex {
namespace math {
namespace celestial {

inline constexpr double PI = 3.14159265358979323846;
inline constexpr double TWO_PI = 6.28318530717958647692;
inline constexpr double HALF_PI = 1.57079632679489661923;

inline constexpr double DEG_TO_RAD = PI / 180.0;
inline constexpr double RAD_TO_DEG = 180.0 / PI;

/** @brief degrees -> radians. */
template <typename T> constexpr T degToRad(T deg) noexcept { return deg * T(DEG_TO_RAD); }

/** @brief radians -> degrees. */
template <typename T> constexpr T radToDeg(T rad) noexcept { return rad * T(RAD_TO_DEG); }

} // namespace celestial
} // namespace math
} // namespace apex

#endif // APEX_MATH_CELESTIAL_ANGLES_HPP
