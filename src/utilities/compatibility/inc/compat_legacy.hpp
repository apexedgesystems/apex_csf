#ifndef APEX_UTILITIES_COMPATIBILITY_LEGACY_HPP
#define APEX_UTILITIES_COMPATIBILITY_LEGACY_HPP
/**
 * @file compat_legacy.hpp
 * @brief Keyword fallback macros for pre-C++17 compilers (C++03, C++11, C++14).
 *
 * The Apex codebase targets C++17 as its baseline. This header provides
 * portable macros that expand to modern C++ keywords on conforming compilers
 * and degrade gracefully on ancient toolchains.
 *
 * Intended consumers: bare-metal HAL headers and the McuExecutive chain
 * that must compile on toolchains with limited C++ support (e.g., TI C2000
 * CGT which only supports C++03).
 *
 * Detection order:
 *   1. __TI_COMPILER_VERSION__ (TI CGT: C++03 only, no __cplusplus bump)
 *   2. __cplusplus value (standard detection)
 *   3. __has_cpp_attribute (attribute detection)
 *
 * Tier summary:
 *   C++03: APEX_NOEXCEPT, APEX_OVERRIDE, APEX_CONSTEXPR, APEX_NULLPTR,
 *          APEX_NODISCARD, APEX_STATIC_ASSERT all degrade to no-ops.
 *          Copy/move prevention via APEX_NONCOPYABLE macro.
 *
 *   C++11: Keywords available. APEX_NODISCARD still a no-op.
 *          APEX_IF_CONSTEXPR degrades to plain if.
 *
 *   C++14: Relaxed constexpr available. APEX_NODISCARD still a no-op.
 *
 *   C++17: Everything expands to the real keyword. This header is
 *          effectively transparent (zero overhead, same codegen).
 *
 * @note RT-safe (compile-time only, no runtime cost).
 */

/* ----------------------------- Compiler Detection ----------------------------- */

/**
 * @def APEX_LEGACY_CPP_STD
 * @brief Detected C++ standard level (3, 11, 14, 17, 20, 23).
 *
 * TI C2000 CGT defines __cplusplus but only supports C++03 regardless
 * of the value. Use __TI_COMPILER_VERSION__ to force C++03 detection.
 */
#if defined(__TI_COMPILER_VERSION__) && !defined(__TI_ARM_V7M__)
/* TI C28x (C2000) CGT: C++03 only, no C++11 support regardless of flags */
#define APEX_LEGACY_CPP_STD 3
#elif defined(__cplusplus)
#if __cplusplus >= 202302L
#define APEX_LEGACY_CPP_STD 23
#elif __cplusplus >= 202002L
#define APEX_LEGACY_CPP_STD 20
#elif __cplusplus >= 201703L
#define APEX_LEGACY_CPP_STD 17
#elif __cplusplus >= 201402L
#define APEX_LEGACY_CPP_STD 14
#elif __cplusplus >= 201103L
#define APEX_LEGACY_CPP_STD 11
#else
#define APEX_LEGACY_CPP_STD 3
#endif
#else
#define APEX_LEGACY_CPP_STD 3
#endif

/* ========================================================================== */
/*  C++11 Keywords                                                            */
/* ========================================================================== */

/* ----------------------------- APEX_NOEXCEPT ----------------------------- */

/// @def APEX_NOEXCEPT
/// @brief `noexcept` on C++11+, empty on C++03.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_NOEXCEPT noexcept
#else
#define APEX_NOEXCEPT
#endif

/* ----------------------------- APEX_OVERRIDE ----------------------------- */

/// @def APEX_OVERRIDE
/// @brief `override` on C++11+, empty on C++03.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_OVERRIDE override
#else
#define APEX_OVERRIDE
#endif

/* ----------------------------- APEX_CONSTEXPR ----------------------------- */

/// @def APEX_CONSTEXPR
/// @brief `constexpr` on C++11+, empty on C++03.
///
/// On C++03, constexpr functions become regular inline functions.
/// The compiler may still constant-fold, but it is not guaranteed.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_CONSTEXPR constexpr
#else
#define APEX_CONSTEXPR
#endif

/* ----------------------------- APEX_CONSTEXPR14 ----------------------------- */

/// @def APEX_CONSTEXPR14
/// @brief `constexpr` on C++14+ (relaxed constexpr), empty on C++11/C++03.
///
/// Use for constexpr functions with loops, local variables, or multiple
/// statements that require C++14 relaxed constexpr.
#if APEX_LEGACY_CPP_STD >= 14
#define APEX_CONSTEXPR14 constexpr
#else
#define APEX_CONSTEXPR14
#endif

/* ----------------------------- APEX_NULLPTR ----------------------------- */

/// @def APEX_NULLPTR
/// @brief `nullptr` on C++11+, `0` on C++03.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_NULLPTR nullptr
#else
#define APEX_NULLPTR 0
#endif

/* ----------------------------- APEX_STATIC_ASSERT ----------------------------- */

/// @def APEX_STATIC_ASSERT(cond, msg)
/// @brief `static_assert` on C++11+, no-op on C++03.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define APEX_STATIC_ASSERT(cond, msg)
#endif

/* ----------------------------- APEX_NONCOPYABLE ----------------------------- */

/// @def APEX_NONCOPYABLE(ClassName)
/// @brief Delete copy/move constructors and assignment operators.
///
/// C++11+: Uses `= delete`.
/// C++03: Declares private unimplemented copy constructor and assignment.
///
/// Usage: Place in the public section of the class.
/// @code
/// class Foo {
/// public:
///   APEX_NONCOPYABLE(Foo)
///   Foo();
/// };
/// @endcode
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_NONCOPYABLE(ClassName)                                                                \
  ClassName(const ClassName&) = delete;                                                            \
  ClassName& operator=(const ClassName&) = delete;                                                 \
  ClassName(ClassName&&) = delete;                                                                 \
  ClassName& operator=(ClassName&&) = delete;
#else
#define APEX_NONCOPYABLE(ClassName)                                                                \
private:                                                                                           \
  ClassName(const ClassName&);                                                                     \
  ClassName& operator=(const ClassName&);                                                          \
                                                                                                   \
public:
#endif

/* ----------------------------- APEX_DEFAULT_CTOR ----------------------------- */

/// @def APEX_DEFAULT_CTOR(ClassName)
/// @brief Defaulted constructor. `= default` on C++11+, empty body on C++03.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_DEFAULT_CTOR(ClassName) ClassName() = default;
#else
#define APEX_DEFAULT_CTOR(ClassName)                                                               \
  ClassName() {}
#endif

/* ----------------------------- APEX_DEFAULT_DTOR ----------------------------- */

/// @def APEX_DEFAULT_DTOR(ClassName)
/// @brief Defaulted destructor. `= default` on C++11+, empty body on C++03.
///
/// For virtual destructors, use APEX_DEFAULT_VIRTUAL_DTOR instead.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_DEFAULT_DTOR(ClassName) ~ClassName() = default;
#else
#define APEX_DEFAULT_DTOR(ClassName)                                                               \
  ~ClassName() {}
#endif

/* ----------------------------- APEX_DEFAULT_VIRTUAL_DTOR ----------------------------- */

/// @def APEX_DEFAULT_VIRTUAL_DTOR(ClassName)
/// @brief Virtual defaulted destructor.
/// `virtual ~Foo() = default` on C++11+, `virtual ~Foo() {}` on C++03.
#if APEX_LEGACY_CPP_STD >= 11
#define APEX_DEFAULT_VIRTUAL_DTOR(ClassName) virtual ~ClassName() = default;
#else
#define APEX_DEFAULT_VIRTUAL_DTOR(ClassName)                                                       \
  virtual ~ClassName() {}
#endif

/* ========================================================================== */
/*  C++17 Keywords                                                            */
/* ========================================================================== */

/* ----------------------------- APEX_NODISCARD ----------------------------- */

/// @def APEX_NODISCARD
/// @brief `[[nodiscard]]` on C++17+, empty on older standards.
#if APEX_LEGACY_CPP_STD >= 17
#define APEX_NODISCARD [[nodiscard]]
#elif defined(__has_cpp_attribute)
#if __has_cpp_attribute(nodiscard)
#define APEX_NODISCARD [[nodiscard]]
#else
#define APEX_NODISCARD
#endif
#else
#define APEX_NODISCARD
#endif

/* ----------------------------- APEX_MAYBE_UNUSED ----------------------------- */

/// @def APEX_MAYBE_UNUSED
/// @brief `[[maybe_unused]]` on C++17+, empty on older standards.
#if APEX_LEGACY_CPP_STD >= 17
#define APEX_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__has_cpp_attribute)
#if __has_cpp_attribute(maybe_unused)
#define APEX_MAYBE_UNUSED [[maybe_unused]]
#else
#define APEX_MAYBE_UNUSED
#endif
#else
#define APEX_MAYBE_UNUSED
#endif

/* ----------------------------- APEX_FALLTHROUGH ----------------------------- */

/// @def APEX_FALLTHROUGH
/// @brief `[[fallthrough]]` on C++17+, empty on older standards.
#if APEX_LEGACY_CPP_STD >= 17
#define APEX_FALLTHROUGH [[fallthrough]]
#else
#define APEX_FALLTHROUGH
#endif

#endif /* APEX_UTILITIES_COMPATIBILITY_LEGACY_HPP */
