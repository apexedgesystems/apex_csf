#ifndef APEX_UTILITIES_COMPATIBILITY_RT_ATTRS_HPP
#define APEX_UTILITIES_COMPATIBILITY_RT_ATTRS_HPP

/**
 * @file compat_rt_attrs.hpp
 * @brief Real-time function-effect attribute shims (RealtimeSanitizer).
 *
 * These let real-time-safe primitives declare their contract without leaking a
 * raw clang attribute into code built by other toolchains. When the compiler
 * does not understand the attribute (GCC, the TI compiler, older clang), the
 * macros collapse to no-ops, so the annotation is documentation there and an
 * enforced check only under a clang `-fsanitize=realtime` build.
 *
 * - APEX_NONBLOCKING  : the function must not call blocking runtime functions
 *                       (malloc/free, mutex lock, blocking syscalls). RTSan
 *                       flags any such call reached from it at runtime.
 * - APEX_NONALLOCATING: the weaker contract -- no heap allocation, but other
 *                       blocking calls are permitted.
 *
 * Place after the parameter list and any noexcept, like a trailing specifier:
 *   void unlock() noexcept APEX_NONBLOCKING;
 */

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define APEX_NONBLOCKING [[clang::nonblocking]]
#define APEX_NONALLOCATING [[clang::nonallocating]]
#else
#define APEX_NONBLOCKING
#define APEX_NONALLOCATING
#endif
#else
#define APEX_NONBLOCKING
#define APEX_NONALLOCATING
#endif

#endif // APEX_UTILITIES_COMPATIBILITY_RT_ATTRS_HPP
