#ifndef APEX_UTILITIES_COMPATIBILITY_ENDIAN_HPP
#define APEX_UTILITIES_COMPATIBILITY_ENDIAN_HPP
/**
 * @file compat_endian.hpp
 * @brief C++20 std::endian alias with minimal C++17 shim.
 *
 * Exposes:
 *  - apex::compat::endian              : enum with { little, big, native }
 *  - apex::compat::NATIVE_ENDIAN       : constexpr value of the host byte order
 *  - apex::compat::endianName(endian)  : const char* helper for logging/debug
 */

#include <cstdint>

#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#if __has_include(<bit>)
#include <bit>
#endif
#endif

#ifndef __cpp_lib_endian
#define __cpp_lib_endian 0
#endif

namespace apex {
namespace compat {

/* ----------------------------- Types ----------------------------- */

#if __cpp_lib_endian

using endian = std::endian;
constexpr endian NATIVE_ENDIAN = std::endian::native;

#else // ---------------------------- C++17 shim -------------------------------

enum class endian : std::uint8_t { little, big, native };

// --- Compile-time detection of native endianness ---
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr endian NATIVE_ENDIAN = endian::little;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr endian NATIVE_ENDIAN = endian::big;
#else
constexpr endian NATIVE_ENDIAN = endian::little; // conservative default
#endif
#elif defined(_WIN32) || defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) ||                       \
    defined(__MIPSEL__) || defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) ||         \
    defined(_M_X64)
constexpr endian NATIVE_ENDIAN = endian::little;
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__MIPSEB__) ||                      \
    defined(__sparc__) || defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)
constexpr endian NATIVE_ENDIAN = endian::big;
#else
constexpr endian NATIVE_ENDIAN = endian::little;
#endif

#endif // __cpp_lib_endian

/* ----------------------------- API ----------------------------- */

// Tiny helper for logging/debugging
[[nodiscard]] inline constexpr const char* endianName(endian e) noexcept {
  return (e == endian::big)      ? "big-endian"
         : (e == endian::little) ? "little-endian"
                                 : "native-endian";
}

} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_ENDIAN_HPP