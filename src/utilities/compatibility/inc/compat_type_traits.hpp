#ifndef APEX_UTILITIES_COMPATIBILITY_TYPE_TRAITS_HPP
#define APEX_UTILITIES_COMPATIBILITY_TYPE_TRAITS_HPP
/**
 * @file compat_type_traits.hpp
 * @brief Portable type traits: std delegates on hosted, manual shims on freestanding.
 *
 * Exposes:
 *  - `apex::compat::is_unsigned_v<T>` : true for unsigned integer types.
 *  - `apex::compat::is_same_v<T, U>`  : true when T and U are the same type.
 *
 * Maps to std::is_unsigned_v / std::is_same_v when <type_traits> is available,
 * otherwise provides constexpr variable templates via template specialization.
 *
 * @note RT-safe (compile-time only, no runtime cost).
 */

#if defined(__has_include) && __has_include(<type_traits>)
#include <type_traits>
#ifndef APEX_COMPAT_HAS_TYPE_TRAITS
#define APEX_COMPAT_HAS_TYPE_TRAITS 1
#endif
#endif

namespace apex {
namespace compat {

/* ----------------------------- is_same_v ----------------------------- */

#ifdef APEX_COMPAT_HAS_TYPE_TRAITS

template <typename T, typename U> constexpr bool is_same_v = std::is_same_v<T, U>;

#else

template <typename T, typename U> constexpr bool is_same_v = false;
template <typename T> constexpr bool is_same_v<T, T> = true;

#endif

/* ----------------------------- is_unsigned_v ----------------------------- */

#ifdef APEX_COMPAT_HAS_TYPE_TRAITS

template <typename T> constexpr bool is_unsigned_v = std::is_unsigned_v<T>;

#else

namespace detail {
template <typename T> struct is_unsigned_impl {
  static constexpr bool value = false;
};
template <> struct is_unsigned_impl<unsigned char> {
  static constexpr bool value = true;
};
template <> struct is_unsigned_impl<unsigned short> {
  static constexpr bool value = true;
};
template <> struct is_unsigned_impl<unsigned int> {
  static constexpr bool value = true;
};
template <> struct is_unsigned_impl<unsigned long> {
  static constexpr bool value = true;
};
template <> struct is_unsigned_impl<unsigned long long> {
  static constexpr bool value = true;
};
} // namespace detail

template <typename T> constexpr bool is_unsigned_v = detail::is_unsigned_impl<T>::value;

#endif

} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_TYPE_TRAITS_HPP
