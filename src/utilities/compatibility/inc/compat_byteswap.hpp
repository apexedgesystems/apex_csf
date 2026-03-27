#ifndef APEX_UTILITIES_COMPATIBILITY_BYTESWAP_HPP
#define APEX_UTILITIES_COMPATIBILITY_BYTESWAP_HPP
/**
 * @file compat_byteswap.hpp
 * @brief Portable byte-swap helpers with C++23 fallback to std::byteswap.
 *
 * Provides:
 *  - apex::compat::byteswap(T) for integral types (excluding bool).
 *  - apex::compat::byteswapIeee(T) for IEEE-754 floats.
 *
 * Notes:
 *  - On C++23+: forwards to std::byteswap for integrals.
 *  - On C++17/20: uses constexpr bit-manipulation for 2/4/8-byte types.
 */

#include <cstdint>
#include <type_traits>

#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#if __has_include(<bit>)
#include <bit>
#endif
#endif

#ifndef __cpp_lib_byteswap
#define __cpp_lib_byteswap 0
#endif

namespace apex {
namespace compat {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Byte-swap for integral types (excluding bool).
 *
 * @tparam T Integral type to swap (1, 2, 4, or 8 bytes).
 * @param v Value to swap.
 * @return Byteswapped value.
 * @note RT-safe (constexpr, no allocations).
 */
template <class T>
[[nodiscard]] constexpr std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>, T>
byteswap(T v) noexcept {
#if __cpp_lib_byteswap >= 202110L
  return std::byteswap(v); // C++23+
#else
  using U = std::make_unsigned_t<T>;
  if constexpr (sizeof(T) == 1) {
    return v;
  } else if constexpr (sizeof(T) == 2) {
    U x = static_cast<U>(v);
    U r = static_cast<U>(((x & U(0x00FF)) << 8) | ((x & U(0xFF00)) >> 8));
    return static_cast<T>(r);
  } else if constexpr (sizeof(T) == 4) {
    U x = static_cast<U>(v);
    U r = ((x & U(0x000000FF)) << 24) | ((x & U(0x0000FF00)) << 8) | ((x & U(0x00FF0000)) >> 8) |
          ((x & U(0xFF000000)) >> 24);
    return static_cast<T>(r);
  } else if constexpr (sizeof(T) == 8) {
    U x = static_cast<U>(v);
    U r = ((x & U(0x00000000000000FFull)) << 56) | ((x & U(0x000000000000FF00ull)) << 40) |
          ((x & U(0x0000000000FF0000ull)) << 24) | ((x & U(0x00000000FF000000ull)) << 8) |
          ((x & U(0x000000FF00000000ull)) >> 8) | ((x & U(0x0000FF0000000000ull)) >> 24) |
          ((x & U(0x00FF000000000000ull)) >> 40) | ((x & U(0xFF00000000000000ull)) >> 56);
    return static_cast<T>(r);
  } else {
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                  "apex::compat::byteswap only supports 1,2,4,8-byte integral types");
    return v;
  }
#endif
}

/**
 * @brief Byte-swap for IEEE-754 floats (float/double).
 *
 * @tparam T Floating-point type (float or double).
 * @param v Value to swap.
 * @return Byteswapped value.
 * @note RT-safe (inline, no allocations).
 */
template <class T>
[[nodiscard]] inline std::enable_if_t<std::is_floating_point_v<T>, T> byteswapIeee(T v) noexcept {
  static_assert(sizeof(T) == 4 || sizeof(T) == 8,
                "apex::compat::byteswapIeee supports float(4B) and double(8B) only");

  if constexpr (sizeof(T) == 4) {
    union {
      T f;
      std::uint32_t u;
    } u{};
    u.f = v;
    u.u = apex::compat::byteswap(u.u);
    return u.f;
  } else {
    union {
      T f;
      std::uint64_t u;
    } u{};
    u.f = v;
    u.u = apex::compat::byteswap(u.u);
    return u.f;
  }
}

} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_BYTESWAP_HPP