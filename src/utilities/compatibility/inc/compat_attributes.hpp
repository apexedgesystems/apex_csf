#ifndef APEX_UTILITIES_COMPATIBILITY_ATTRIBUTES_HPP
#define APEX_UTILITIES_COMPATIBILITY_ATTRIBUTES_HPP

/**
 * @file compat_attributes.hpp
 * @brief Compiler portability macros for hot paths and branch prediction.
 *
 * Defines COMPAT_HOT, COMPAT_LIKELY(x), and COMPAT_UNLIKELY(x) with safe
 * fallbacks on compilers that do not support GCC/Clang attributes/builtins.
 */

/* ----------------------------- Macros ----------------------------- */

/// @def COMPAT_HOT
/// @brief Hint that a function is hot (frequently executed).
#if defined(__GNUC__) || defined(__clang__)
#define COMPAT_HOT __attribute__((hot))
#else
#define COMPAT_HOT
#endif

/// @def COMPAT_LIKELY(x)
/// @brief Branch prediction hint indicating the expression is likely true.
#if defined(__GNUC__) || defined(__clang__)
#define COMPAT_LIKELY(x) (__builtin_expect(!!(x), 1))
#else
#define COMPAT_LIKELY(x) (x)
#endif

/// @def COMPAT_UNLIKELY(x)
/// @brief Branch prediction hint indicating the expression is likely false.
#if defined(__GNUC__) || defined(__clang__)
#define COMPAT_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define COMPAT_UNLIKELY(x) (x)
#endif

#endif // APEX_UTILITIES_COMPATIBILITY_ATTRIBUTES_HPP
