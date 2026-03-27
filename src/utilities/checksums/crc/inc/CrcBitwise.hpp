#ifndef APEX_UTILITIES_CHECKSUMS_CRC_BITWISE_HPP
#define APEX_UTILITIES_CHECKSUMS_CRC_BITWISE_HPP

/**
 * @file CrcBitwise.hpp
 * @brief CRTP-based, bitwise (table-less) CRC implementation.
 *
 * Computes CRC by shifting and conditional XORs—no lookup table needed.
 * Zero dynamic allocations, no exceptions, C++17+ compatible, real-time ready.
 *
 * @tparam T           Unsigned accumulator type (`uint8_t`…`uint64_t`).
 * @tparam Poly        Generator polynomial.
 * @tparam Initial     Initial remainder seed.
 * @tparam XorOut      Final XOR mask.
 * @tparam ReflectIn   If true, process input LSB-first.
 * @tparam ReflectOut  If true, reflect bits of final CRC.
 * @tparam Width       CRC bit-width (default = `sizeof(T) * 8`).
 */

#include <stddef.h>
#include <stdint.h>

// Compatibility shims
#include "src/utilities/compatibility/inc/compat_type_traits.hpp"

// Base CRC interface
#include "src/utilities/checksums/crc/inc/CrcBase.hpp"

namespace apex {
namespace checksums {
namespace crc {

/* ----------------------------- CrcBitwise ----------------------------- */

/**
 * @brief CRTP-based, bitwise (table-less) CRC implementation.
 *
 * @note RT-safe: O(n) in input size, no allocations, no exceptions.
 *
 * See @ref CrcBase for the common interface. This variant computes
 * the CRC entirely in software without a lookup table.
 */
template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut,
          uint8_t Width = static_cast<uint8_t>(sizeof(T) * 8)>
class CrcBitwise
    : public CrcBase<T, CrcBitwise<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>> {
  static_assert(apex::compat::is_unsigned_v<T>, "CrcBitwise requires an unsigned integer type");
  static_assert(Width > 0 && Width <= sizeof(T) * 8, "Width must be between 1 and sizeof(T) * 8");

public:
  /// CRC register width in bits.
  static constexpr uint8_t fetchWidth() noexcept { return Width; }

  /// Initial remainder seed.
  static constexpr T fetchInitial() noexcept { return Initial; }

  /// Final XOR mask.
  static constexpr T fetchXorOut() noexcept { return XorOut; }

  /// Process input bytes LSB-first when true.
  static constexpr bool fetchReflectIn() noexcept { return ReflectIn; }

  /// Reflect final CRC bits when true.
  static constexpr bool fetchReflectOut() noexcept { return ReflectOut; }

  /**
   * @brief Core bitwise update loop (no table).
   *
   * Processes each input byte one bit at a time, shifting and
   * conditionally XORing with the polynomial.
   *
   * @param[in,out] rem  Current CRC remainder.
   * @param[in]     data Pointer to input buffer.
   * @param[in]     len  Number of bytes to process.
   */
  static void updateImpl(T& rem, const uint8_t* data, size_t len) noexcept;
};

} // namespace crc
} // namespace checksums
} // namespace apex

#include "src/utilities/checksums/crc/src/CrcBitwise.tpp"
#endif // APEX_UTILITIES_CHECKSUMS_CRC_BITWISE_HPP
