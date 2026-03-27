#ifndef APEX_UTILITIES_COMPATIBILITY_SPAN_HPP
#define APEX_UTILITIES_COMPATIBILITY_SPAN_HPP
/**
 * @file compat_span.hpp
 * @brief C++20 std::span alias with minimal C++17 shims.
 *
 * Exposes:
 *  - `apex::compat::span<T>`            : generic span for any element type T (mutable or const).
 *  - `apex::compat::bytes_span`         : read-only view of bytes (uint8_t).
 *  - `apex::compat::mutable_bytes_span` : writable view of bytes (uint8_t).
 *  - `apex::compat::rospan<T>`          : generic read-only view for any element type T.
 *
 * All map to std::span on C++20+, and to lightweight C++17 shims otherwise.
 *
 * On freestanding (bare-metal) builds where C++ standard library headers are
 * unavailable, the shim types use C headers and provide minimal type traits.
 * Container-based constructors are conditionally compiled.
 *
 * Usage for C++17 compatibility:
 *   Replace `std::span<T>` with `apex::compat::span<T>` throughout.
 */

// C headers (always available, including bare-metal freestanding)
#include <stddef.h>
#include <stdint.h>

// Hosted-only headers: conditionally included via __has_include
#if defined(__has_include)
#if __has_include(<type_traits>)
#include <type_traits>
#define APEX_COMPAT_HAS_TYPE_TRAITS 1
#endif
#if __has_include(<array>)
#include <array>
#define APEX_COMPAT_HAS_ARRAY 1
#endif
#if __has_include(<vector>)
#include <vector>
#define APEX_COMPAT_HAS_VECTOR 1
#endif
#if __has_include(<version>)
#include <version>
#endif
#if __has_include(<span>)
#include <span>
#endif
#endif

#ifndef __cpp_lib_span
#define __cpp_lib_span 0
#endif

#if !__cpp_lib_span
#if defined(__has_include) && __has_include(<string_view>)
#include <string_view>
#define APEX_COMPAT_HAS_STRING_VIEW 1
#endif
#endif

namespace apex {
namespace compat {

/* ----------------------------- Internal Type Traits ----------------------------- */

#ifndef APEX_COMPAT_HAS_TYPE_TRAITS
namespace detail {
template <typename T> struct remove_cv {
  using type = T;
};
template <typename T> struct remove_cv<const T> {
  using type = T;
};
template <typename T> struct remove_cv<volatile T> {
  using type = T;
};
template <typename T> struct remove_cv<const volatile T> {
  using type = T;
};
template <typename T> using remove_cv_t = typename remove_cv<T>::type;
template <typename T> struct remove_const {
  using type = T;
};
template <typename T> struct remove_const<const T> {
  using type = T;
};
template <typename T> using remove_const_t = typename remove_const<T>::type;
} // namespace detail
#endif

/* ----------------------------- Types ----------------------------- */

#if __cpp_lib_span
/// Generic span alias (C++20+: direct std::span).
/// @note RT-safe (zero-cost wrapper over std::span).
template <typename T> using span = std::span<T>;

/// Read-only byte view (C++20+: std::span<const uint8_t>)
/// @note RT-safe (zero-cost wrapper over std::span).
using bytes_span = std::span<const uint8_t>;

/// Writable byte view (C++20+: std::span<uint8_t>)
/// @note RT-safe (zero-cost wrapper over std::span).
using mutable_bytes_span = std::span<uint8_t>;

/// Generic read-only span for any element type T (unique from bytes_span for clarity)
/// @note RT-safe (zero-cost wrapper over std::span).
template <typename T> using rospan = std::span<const T>;

#else // C++17 shims

/**
 * @brief Generic span for C++17.
 *
 * Non-owning view over contiguous elements of type T.
 * Works with both const and non-const types.
 * Constructible from pointer/length, C-arrays, and (on hosted builds)
 * std::array and std::vector.
 *
 * @note RT-safe (inline accessors, no allocations).
 */
template <typename T> class span {
public:
  using element_type = T;
#ifdef APEX_COMPAT_HAS_TYPE_TRAITS
  using value_type = std::remove_cv_t<T>;
#else
  using value_type = typename detail::remove_cv<T>::type;
#endif
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using iterator = T*;
  using const_iterator = const T*;

  constexpr span() noexcept : p_(nullptr), n_(0) {}
  constexpr span(T* p, size_type n) noexcept : p_(p), n_(n) {}

  template <size_t N> constexpr span(T (&arr)[N]) noexcept : p_(arr), n_(N) {}

#ifdef APEX_COMPAT_HAS_ARRAY
  template <size_t N> constexpr span(std::array<value_type, N>& a) noexcept : p_(a.data()), n_(N) {}

  template <size_t N>
  constexpr span(const std::array<value_type, N>& a) noexcept : p_(a.data()), n_(N) {}
#endif

#ifdef APEX_COMPAT_HAS_VECTOR
  template <class A> span(std::vector<value_type, A>& v) noexcept : p_(v.data()), n_(v.size()) {}

  template <class A>
  span(const std::vector<value_type, A>& v) noexcept : p_(v.data()), n_(v.size()) {}
#endif

  [[nodiscard]] constexpr T* data() const noexcept { return p_; }
  [[nodiscard]] constexpr size_type size() const noexcept { return n_; }
  [[nodiscard]] constexpr size_type size_bytes() const noexcept { return n_ * sizeof(T); }
  [[nodiscard]] constexpr bool empty() const noexcept { return n_ == 0; }

  [[nodiscard]] constexpr T& operator[](size_type i) const noexcept { return p_[i]; }
  [[nodiscard]] constexpr T& front() const noexcept { return p_[0]; }
  [[nodiscard]] constexpr T& back() const noexcept { return p_[n_ - 1]; }

  [[nodiscard]] constexpr iterator begin() const noexcept { return p_; }
  [[nodiscard]] constexpr iterator end() const noexcept { return p_ + n_; }

  [[nodiscard]] constexpr span<T> subspan(size_type offset, size_type count) const noexcept {
    return span<T>(p_ + offset, count);
  }

  [[nodiscard]] constexpr span<T> first(size_type count) const noexcept {
    return span<T>(p_, count);
  }

  [[nodiscard]] constexpr span<T> last(size_type count) const noexcept {
    return span<T>(p_ + (n_ - count), count);
  }

private:
  T* p_;
  size_type n_;
};

/**
 * @brief Minimal read-only byte view for C++17.
 *
 * Non-owning view over contiguous const bytes.
 * Constructible from pointer/length, and (on hosted builds) std::array<uint8_t,N>,
 * std::vector<uint8_t>, and std::string_view (reinterpreted as bytes).
 *
 * @note RT-safe (inline accessors, no allocations).
 */
class bytes_span {
public:
  using element_type = const uint8_t;
  using size_type = size_t;
  using iterator = const uint8_t*;
  using const_iterator = const uint8_t*;

  constexpr bytes_span() noexcept : p_(nullptr), n_(0) {}
  constexpr bytes_span(const uint8_t* p, size_type n) noexcept : p_(p), n_(n) {}

#ifdef APEX_COMPAT_HAS_ARRAY
  template <size_t N>
  constexpr bytes_span(const std::array<uint8_t, N>& a) noexcept : p_(a.data()), n_(N) {}
#endif

#ifdef APEX_COMPAT_HAS_VECTOR
  template <class A>
  bytes_span(const std::vector<uint8_t, A>& v) noexcept : p_(v.data()), n_(v.size()) {}
#endif

#ifdef APEX_COMPAT_HAS_STRING_VIEW
  bytes_span(std::string_view sv) noexcept
      : p_(reinterpret_cast<const uint8_t*>(sv.data())), n_(sv.size()) {}
#endif

  [[nodiscard]] constexpr const uint8_t* data() const noexcept { return p_; }
  [[nodiscard]] constexpr size_type size() const noexcept { return n_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return n_ == 0; }
  [[nodiscard]] constexpr const uint8_t& operator[](size_type i) const noexcept { return p_[i]; }
  [[nodiscard]] constexpr iterator begin() const noexcept { return p_; }
  [[nodiscard]] constexpr iterator end() const noexcept { return p_ + n_; }

private:
  const uint8_t* p_;
  size_type n_;
};

/**
 * @brief Minimal writable byte view for C++17.
 *
 * Non-owning view over contiguous mutable bytes.
 * Constructible from pointer/length, and (on hosted builds) std::array<uint8_t,N>
 * and std::vector<uint8_t>.
 *
 * @note RT-safe (inline accessors, no allocations).
 */
class mutable_bytes_span {
public:
  using element_type = uint8_t;
  using size_type = size_t;
  using iterator = uint8_t*;
  using const_iterator = uint8_t*;

  constexpr mutable_bytes_span() noexcept : p_(nullptr), n_(0) {}
  constexpr mutable_bytes_span(uint8_t* p, size_type n) noexcept : p_(p), n_(n) {}

#ifdef APEX_COMPAT_HAS_ARRAY
  template <size_t N>
  constexpr mutable_bytes_span(std::array<uint8_t, N>& a) noexcept : p_(a.data()), n_(N) {}
#endif

#ifdef APEX_COMPAT_HAS_VECTOR
  template <class A>
  mutable_bytes_span(std::vector<uint8_t, A>& v) noexcept : p_(v.data()), n_(v.size()) {}
#endif

  [[nodiscard]] constexpr uint8_t* data() const noexcept { return p_; }
  [[nodiscard]] constexpr size_type size() const noexcept { return n_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return n_ == 0; }
  [[nodiscard]] constexpr uint8_t& operator[](size_type i) const noexcept { return p_[i]; }
  [[nodiscard]] constexpr iterator begin() const noexcept { return p_; }
  [[nodiscard]] constexpr iterator end() const noexcept { return p_ + n_; }

private:
  uint8_t* p_;
  size_type n_;
};

/**
 * @brief Minimal generic read-only span for C++17.
 *
 * Unique from bytes_span in that it supports *any* element type T,
 * not just uint8_t. Useful for read-only math arrays, lookup tables, etc.
 *
 * @note RT-safe (inline accessors, no allocations).
 */
template <typename T> class rospan {
public:
  using element_type = T;
  using size_type = size_t;

  constexpr rospan() noexcept : p_(nullptr), n_(0) {}
  constexpr rospan(const T* p, size_type n) noexcept : p_(p), n_(n) {}

#ifdef APEX_COMPAT_HAS_ARRAY
  template <size_t N>
#ifdef APEX_COMPAT_HAS_TYPE_TRAITS
  constexpr rospan(const std::array<std::remove_const_t<T>, N>& a) noexcept : p_(a.data()), n_(N) {
  }
#else
  constexpr rospan(const std::array<typename detail::remove_const<T>::type, N>& a) noexcept
      : p_(a.data()), n_(N) {
  }
#endif
#endif

#ifdef APEX_COMPAT_HAS_VECTOR
#ifdef APEX_COMPAT_HAS_TYPE_TRAITS
  template <class A>
  rospan(const std::vector<std::remove_const_t<T>, A>& v) noexcept : p_(v.data()), n_(v.size()) {}
#else
  template <class A>
  rospan(const std::vector<typename detail::remove_const<T>::type, A>& v) noexcept
      : p_(v.data()), n_(v.size()) {}
#endif
#endif

  [[nodiscard]] constexpr const T* data() const noexcept { return p_; }
  [[nodiscard]] constexpr size_type size() const noexcept { return n_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return n_ == 0; }

private:
  const T* p_;
  size_type n_;
};

#endif // __cpp_lib_span

} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_SPAN_HPP
