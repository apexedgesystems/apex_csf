#ifndef APEX_UTILITIES_COMPATIBILITY_MATH_HPP
#define APEX_UTILITIES_COMPATIBILITY_MATH_HPP
/**
 * @file compat_math.hpp
 * @brief Portable scalar math: std delegates on hosted, <math.h> shims on freestanding.
 *
 * Exposes float/double overloads in `apex::compat`:
 *  - `sqrt`, `sin`, `cos`, `asin`, `acos`, `atan2`, `fabs`, `copysign`
 *  - `epsilon<T>()` : machine epsilon (replaces std::numeric_limits<T>::epsilon()
 *    where <limits> is unavailable)
 *
 * Maps to the std:: functions when <cmath> is available. Freestanding
 * toolchains without a C++ standard library (avr-gcc) get the C <math.h>
 * functions instead -- avr-libc provides the full set; the float overloads
 * dispatch to the f-suffixed forms so single-precision FPU targets never
 * promote through double.
 *
 * @note RT-safe: pure functions, no allocation, all noexcept.
 */

#if defined(__has_include) && __has_include(<cmath>) && __has_include(<limits>)
#include <cmath>
#include <limits>
#ifndef APEX_COMPAT_HAS_CMATH
#define APEX_COMPAT_HAS_CMATH 1
#endif
#else
#include <float.h>
#include <math.h>
#endif

namespace apex {
namespace compat {

#ifdef APEX_COMPAT_HAS_CMATH

inline float sqrt(float v) noexcept { return std::sqrt(v); }
inline double sqrt(double v) noexcept { return std::sqrt(v); }
inline float sin(float v) noexcept { return std::sin(v); }
inline double sin(double v) noexcept { return std::sin(v); }
inline float cos(float v) noexcept { return std::cos(v); }
inline double cos(double v) noexcept { return std::cos(v); }
inline float asin(float v) noexcept { return std::asin(v); }
inline double asin(double v) noexcept { return std::asin(v); }
inline float acos(float v) noexcept { return std::acos(v); }
inline double acos(double v) noexcept { return std::acos(v); }
inline float atan2(float y, float x) noexcept { return std::atan2(y, x); }
inline double atan2(double y, double x) noexcept { return std::atan2(y, x); }
inline float fabs(float v) noexcept { return std::fabs(v); }
inline double fabs(double v) noexcept { return std::fabs(v); }
inline float copysign(float m, float s) noexcept { return std::copysign(m, s); }
inline double copysign(double m, double s) noexcept { return std::copysign(m, s); }

template <typename T> constexpr T epsilon() noexcept { return std::numeric_limits<T>::epsilon(); }

#else

inline float sqrt(float v) noexcept { return ::sqrtf(v); }
inline double sqrt(double v) noexcept { return ::sqrt(v); }
inline float sin(float v) noexcept { return ::sinf(v); }
inline double sin(double v) noexcept { return ::sin(v); }
inline float cos(float v) noexcept { return ::cosf(v); }
inline double cos(double v) noexcept { return ::cos(v); }
inline float asin(float v) noexcept { return ::asinf(v); }
inline double asin(double v) noexcept { return ::asin(v); }
inline float acos(float v) noexcept { return ::acosf(v); }
inline double acos(double v) noexcept { return ::acos(v); }
inline float atan2(float y, float x) noexcept { return ::atan2f(y, x); }
inline double atan2(double y, double x) noexcept { return ::atan2(y, x); }
inline float fabs(float v) noexcept { return ::fabsf(v); }
inline double fabs(double v) noexcept { return ::fabs(v); }
inline float copysign(float m, float s) noexcept { return ::copysignf(m, s); }
inline double copysign(double m, double s) noexcept { return ::copysign(m, s); }

template <typename T> constexpr T epsilon() noexcept;
template <> constexpr float epsilon<float>() noexcept { return FLT_EPSILON; }
template <> constexpr double epsilon<double>() noexcept { return DBL_EPSILON; }

#endif

} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_MATH_HPP
