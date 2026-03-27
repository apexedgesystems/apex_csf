#ifndef APEX_UTILITIES_HELPERS_BITS_HPP
#define APEX_UTILITIES_HELPERS_BITS_HPP
/**
 * @file Bits.hpp
 * @brief Bit-manipulation helpers for byte-level flag operations.
 *
 * Provides hardened index handling (masks to [0,7]) to avoid UB on out-of-range bits.
 *
 * @note RT-SAFE: All functions are constexpr with no allocations or syscalls.
 */

#include <cstdint>

namespace apex {
namespace helpers {
namespace bits {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Set bit at position nbit in byte.
 * @param byte Target byte (modified in place).
 * @param nbit Bit position (masked to [0,7]).
 * @note RT-SAFE: Pure constexpr.
 */
constexpr void set(std::uint8_t& byte, std::uint8_t nbit) noexcept {
  const std::uint8_t MASK = static_cast<std::uint8_t>(1u << (static_cast<unsigned>(nbit) & 7u));
  byte = static_cast<std::uint8_t>(byte | MASK);
}

/**
 * @brief Clear bit at position nbit in byte.
 * @param byte Target byte (modified in place).
 * @param nbit Bit position (masked to [0,7]).
 * @note RT-SAFE: Pure constexpr.
 */
constexpr void clear(std::uint8_t& byte, std::uint8_t nbit) noexcept {
  const std::uint8_t MASK = static_cast<std::uint8_t>(1u << (static_cast<unsigned>(nbit) & 7u));
  byte = static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~MASK));
}

/**
 * @brief Flip bit at position nbit in byte.
 * @param byte Target byte (modified in place).
 * @param nbit Bit position (masked to [0,7]).
 * @note RT-SAFE: Pure constexpr.
 */
constexpr void flip(std::uint8_t& byte, std::uint8_t nbit) noexcept {
  const std::uint8_t MASK = static_cast<std::uint8_t>(1u << (static_cast<unsigned>(nbit) & 7u));
  byte = static_cast<std::uint8_t>(byte ^ MASK);
}

/**
 * @brief Test if bit at position nbit is set in byte.
 * @param byte Source byte.
 * @param nbit Bit position (masked to [0,7]).
 * @return True if bit is set.
 * @note RT-SAFE: Pure constexpr.
 */
constexpr bool test(std::uint8_t byte, std::uint8_t nbit) noexcept {
  const std::uint8_t MASK = static_cast<std::uint8_t>(1u << (static_cast<unsigned>(nbit) & 7u));
  return (static_cast<std::uint8_t>(byte & MASK) != std::uint8_t{0});
}

} // namespace bits
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_BITS_HPP
