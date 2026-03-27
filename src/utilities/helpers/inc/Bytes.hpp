#ifndef APEX_UTILITIES_HELPERS_BYTES_HPP
#define APEX_UTILITIES_HELPERS_BYTES_HPP
/**
 * @file Bytes.hpp
 * @brief Byte/word extraction and load/store helpers for explicit LE/BE handling.
 *
 * Provides portable, optimized byte-order operations using memcpy and conditional
 * byteswap for safe type-punning and predictable codegen.
 *
 * @note RT-SAFE: All functions are inline/constexpr with no allocations or syscalls.
 */

#include "src/utilities/compatibility/inc/compat_endian.hpp"
#include "src/utilities/compatibility/inc/compat_byteswap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace apex {
namespace helpers {
namespace bytes {

/* ----------------------------- Extraction ----------------------------- */

/**
 * @brief Extract a byte at index from value, interpreting value as little-endian.
 * @tparam T Integral (non-bool) type.
 * @param value Source value.
 * @param index Byte index (0 = least-significant).
 * @return Extracted byte.
 * @note RT-SAFE: Pure constexpr.
 */
template <typename T> constexpr std::uint8_t extractLe(T value, std::size_t index) noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>,
                "T must be an integral (non-bool) type");
  using U = std::make_unsigned_t<T>;
  return static_cast<std::uint8_t>((static_cast<U>(value) >> (static_cast<U>(index) * 8U)) &
                                   U(0xFF));
}

/**
 * @brief Extract a byte at index from value, interpreting value as big-endian.
 * @tparam T Integral (non-bool) type.
 * @param value Source value.
 * @param index Byte index (0 = most-significant).
 * @return Extracted byte.
 * @note RT-SAFE: Pure constexpr.
 */
template <typename T> constexpr std::uint8_t extractBe(T value, std::size_t index) noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>,
                "T must be an integral (non-bool) type");
  using U = std::make_unsigned_t<T>;
  constexpr std::size_t TOTAL_BYTES = sizeof(T);
  return static_cast<std::uint8_t>((static_cast<U>(value) >> ((TOTAL_BYTES - 1U - index) * 8U)) &
                                   U(0xFF));
}

/* ----------------------------- Load ----------------------------- */

/**
 * @brief Load an integral T from data interpreted as little-endian.
 * @tparam T Integral (non-bool) type.
 * @param data Pointer to at least sizeof(T) bytes.
 * @return Loaded value.
 * @note RT-SAFE: Uses memcpy, no allocation.
 */
template <typename T> inline T loadLe(const std::uint8_t* data) noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>,
                "T must be an integral (non-bool) type");
  using U = std::make_unsigned_t<T>;
  U x{};
  std::memcpy(&x, data, sizeof(T));
  if (apex::compat::NATIVE_ENDIAN == apex::compat::endian::big) {
    x = apex::compat::byteswap(x);
  }
  return static_cast<T>(x);
}

/**
 * @brief Load an integral T from data interpreted as big-endian.
 * @tparam T Integral (non-bool) type.
 * @param data Pointer to at least sizeof(T) bytes.
 * @return Loaded value.
 * @note RT-SAFE: Uses memcpy, no allocation.
 */
template <typename T> inline T loadBe(const std::uint8_t* data) noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>,
                "T must be an integral (non-bool) type");
  using U = std::make_unsigned_t<T>;
  U x{};
  std::memcpy(&x, data, sizeof(T));
  if (apex::compat::NATIVE_ENDIAN == apex::compat::endian::little) {
    x = apex::compat::byteswap(x);
  }
  return static_cast<T>(x);
}

/* ----------------------------- Store ----------------------------- */

/**
 * @brief Store an integral T into data in little-endian order.
 * @tparam T Integral (non-bool) type.
 * @param value Value to store.
 * @param data Pointer to at least sizeof(T) bytes.
 * @note RT-SAFE: Uses memcpy, no allocation.
 */
template <typename T> inline void storeLe(T value, std::uint8_t* data) noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>,
                "T must be an integral (non-bool) type");
  using U = std::make_unsigned_t<T>;
  U x = static_cast<U>(value);
  if (apex::compat::NATIVE_ENDIAN == apex::compat::endian::big) {
    x = apex::compat::byteswap(x);
  }
  std::memcpy(data, &x, sizeof(T));
}

/**
 * @brief Store an integral T into data in big-endian order.
 * @tparam T Integral (non-bool) type.
 * @param value Value to store.
 * @param data Pointer to at least sizeof(T) bytes.
 * @note RT-SAFE: Uses memcpy, no allocation.
 */
template <typename T> inline void storeBe(T value, std::uint8_t* data) noexcept {
  static_assert(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>,
                "T must be an integral (non-bool) type");
  using U = std::make_unsigned_t<T>;
  U x = static_cast<U>(value);
  if (apex::compat::NATIVE_ENDIAN == apex::compat::endian::little) {
    x = apex::compat::byteswap(x);
  }
  std::memcpy(data, &x, sizeof(T));
}

/* ----------------------------- Endian Names ----------------------------- */

/**
 * @brief Human-readable name for an endianness value.
 * @param e Endianness value.
 * @return Static string literal.
 * @note RT-SAFE: Pure constexpr.
 */
constexpr const char* endianName(apex::compat::endian e) noexcept {
  return (e == apex::compat::endian::big)      ? "big-endian"
         : (e == apex::compat::endian::little) ? "little-endian"
                                               : "native-endian";
}

/**
 * @brief Endianness name for the host CPU.
 * @return Static string literal.
 * @note RT-SAFE: Pure constexpr.
 */
constexpr const char* nativeEndianName() noexcept {
  return endianName(apex::compat::NATIVE_ENDIAN);
}

/* ----------------------------- Serialization ----------------------------- */

/**
 * @brief Convert a trivially copyable object into its raw byte representation.
 *
 * No endianness conversion is performed. The output reflects the host machine's
 * native layout.
 *
 * @tparam T Trivially copyable type.
 * @param value Input value to convert.
 * @return Array of bytes containing the object representation.
 * @note RT-SAFE: No allocation, fixed-size array.
 */
template <typename T>
[[nodiscard]] inline std::array<std::uint8_t, sizeof(T)> toBytes(const T& value) noexcept {
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
  std::array<std::uint8_t, sizeof(T)> out{};
  std::memcpy(out.data(), &value, sizeof(T));
  return out;
}

} // namespace bytes
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_BYTES_HPP
