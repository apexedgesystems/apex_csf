#ifndef APEX_UTILITIES_COMPATIBILITY_ARRAY_HPP
#define APEX_UTILITIES_COMPATIBILITY_ARRAY_HPP
/**
 * @file compat_array.hpp
 * @brief Portable fixed-size array: std::array on hosted, minimal shim on freestanding.
 *
 * Exposes:
 *  - `apex::compat::array<T, N>` : fixed-size aggregate with operator[], data(),
 *                                   size(), begin()/end(). Maps to std::array when
 *                                   available, otherwise provides a constexpr-friendly
 *                                   struct wrapper around a C array.
 *
 * @note RT-safe (no allocations, no exceptions).
 */

#include <stddef.h>

#if defined(__has_include) && __has_include(<array>)
#include <array>
#ifndef APEX_COMPAT_HAS_ARRAY
#define APEX_COMPAT_HAS_ARRAY 1
#endif
#endif

namespace apex {
namespace compat {

#ifdef APEX_COMPAT_HAS_ARRAY

template <typename T, size_t N> using array = std::array<T, N>;

#else

/**
 * @brief Minimal constexpr-friendly fixed-size array for freestanding builds.
 *
 * Aggregate type (brace-initialization works). Provides the subset of
 * the std::array interface needed by CRC lookup tables and similar code.
 *
 * @note RT-safe (inline accessors, no allocations).
 */
template <typename T, size_t N> struct array {
  T data_[N]{};

  [[nodiscard]] constexpr T& operator[](size_t i) noexcept { return data_[i]; }
  [[nodiscard]] constexpr const T& operator[](size_t i) const noexcept { return data_[i]; }
  [[nodiscard]] constexpr T* data() noexcept { return data_; }
  [[nodiscard]] constexpr const T* data() const noexcept { return data_; }
  [[nodiscard]] constexpr size_t size() const noexcept { return N; }
  [[nodiscard]] constexpr T* begin() noexcept { return data_; }
  [[nodiscard]] constexpr T* end() noexcept { return data_ + N; }
  [[nodiscard]] constexpr const T* begin() const noexcept { return data_; }
  [[nodiscard]] constexpr const T* end() const noexcept { return data_ + N; }
};

#endif

} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_ARRAY_HPP
