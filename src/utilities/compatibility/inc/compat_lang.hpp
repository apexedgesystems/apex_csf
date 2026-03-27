#ifndef APEX_UTILITIES_COMPATIBILITY_LANG_HPP
#define APEX_UTILITIES_COMPATIBILITY_LANG_HPP
/**
 * @file compat_lang.hpp
 * @brief Portable macros for C++17 language features.
 *
 * Provides safe fallbacks for C++17 language constructs that cannot
 * be polyfilled with library-only code:
 *
 *  - APEX_IF_CONSTEXPR : Expands to `if constexpr` on C++17, `if` otherwise.
 *  - APEX_INLINE_VAR   : Expands to `inline` on C++17, nothing otherwise.
 *
 * @note RT-safe (compile-time only, no runtime cost).
 */

/* ----------------------------- APEX_IF_CONSTEXPR ----------------------------- */

/// @def APEX_IF_CONSTEXPR
/// @brief `if constexpr` on compilers that support it, plain `if` otherwise.
///
/// Both branches must be valid code when using APEX_IF_CONSTEXPR. The
/// optimizer eliminates the dead branch in both cases, but unlike true
/// `if constexpr`, the dead branch is still type-checked.
#if __cpp_if_constexpr >= 201606L
#define APEX_IF_CONSTEXPR if constexpr
#else
#define APEX_IF_CONSTEXPR if
#endif

/* ----------------------------- APEX_INLINE_VAR ----------------------------- */

/// @def APEX_INLINE_VAR
/// @brief `inline` for variable declarations on C++17, empty otherwise.
///
/// Use for non-template `constexpr` variables in headers. Variable
/// templates do not need this (they are implicitly inline).
#if __cpp_inline_variables >= 201606L
#define APEX_INLINE_VAR inline
#else
#define APEX_INLINE_VAR
#endif

#endif // APEX_UTILITIES_COMPATIBILITY_LANG_HPP
