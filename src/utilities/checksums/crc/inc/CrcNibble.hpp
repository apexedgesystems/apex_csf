#ifndef APEX_UTILITIES_CHECKSUMS_CRC_NIBBLE_HPP
#define APEX_UTILITIES_CHECKSUMS_CRC_NIBBLE_HPP

/**
 * @file CrcNibble.hpp
 * @brief CRTP-based, nibble-table CRC implementation (16 entries).
 *
 * Processes each byte in two 4-bit folds for a minimal lookup footprint.
 * Supports arbitrary width, MSB-first or LSB-first, and post-finalize reflection.
 *
 * @tparam T           Unsigned accumulator type.
 * @tparam Poly        Generator polynomial.
 * @tparam Initial     Initial remainder seed.
 * @tparam XorOut      Final XOR mask.
 * @tparam ReflectIn   If true, process LSB-first; otherwise MSB-first.
 * @tparam ReflectOut  If true, reflect final CRC in finalize().
 * @tparam Width       CRC register width in bits (1..sizeof(T)*8).
 */

#include <stddef.h>
#include <stdint.h>

// Compatibility shims
#include "src/utilities/compatibility/inc/compat_array.hpp"
#include "src/utilities/compatibility/inc/compat_type_traits.hpp"

// Base CRC interface
#include "src/utilities/checksums/crc/inc/CrcBase.hpp"

namespace apex {
namespace checksums {
namespace crc {

/* ----------------------------- Constants ----------------------------- */

/** Number of entries in a nibble lookup table. */
static constexpr size_t CRC_NIBBLE_TABLE_SIZE = 16;

/* ----------------------------- CrcNibble ----------------------------- */

/**
 * @brief CRTP-based, nibble-table CRC implementation.
 *
 * @note RT-safe: O(n) in input size, no allocations, no exceptions.
 *
 * See @ref CrcBase for the common interface. This variant uses a single
 * 16-entry lookup table, processing each byte in two 4-bit folds.
 */
template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut,
          uint8_t Width = static_cast<uint8_t>(sizeof(T) * 8)>
class CrcNibble
    : public CrcBase<T, CrcNibble<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>> {
  static_assert(apex::compat::is_unsigned_v<T>, "CrcNibble requires an unsigned integer type");
  static_assert(Width > 0 && Width <= sizeof(T) * 8, "Width must be in range 1..(8*sizeof(T))");

public:
  /// CRC register width, in bits.
  static constexpr uint8_t fetchWidth() noexcept { return Width; }

  /// Initial remainder seed.
  static constexpr T fetchInitial() noexcept { return Initial; }

  /// Final XOR mask.
  static constexpr T fetchXorOut() noexcept { return XorOut; }

  /// Reflect-in flag.
  static constexpr bool fetchReflectIn() noexcept { return ReflectIn; }

  /// Reflect-out flag.
  static constexpr bool fetchReflectOut() noexcept { return ReflectOut; }

  /**
   * @brief Nibble-table update loop.
   *
   * Splits each input byte into two 4-bit nibbles, xors into `rem`,
   * table-lookup, shift, mask, then repeats.
   *
   * @param[in,out] rem   Current remainder.
   * @param[in]     data  Input buffer pointer.
   * @param[in]     len   Number of bytes to process.
   */
  static void updateImpl(T& rem, const uint8_t* data, size_t len) noexcept;

private:
  /// Build the 16-entry lookup table at compile time.
  static constexpr apex::compat::array<T, CRC_NIBBLE_TABLE_SIZE> generateTable() noexcept;

  /// Precomputed nibble table.
  static constexpr apex::compat::array<T, CRC_NIBBLE_TABLE_SIZE> TABLE = generateTable();
};

} // namespace crc
} // namespace checksums
} // namespace apex

#include "src/utilities/checksums/crc/src/CrcNibble.tpp"
#endif // APEX_UTILITIES_CHECKSUMS_CRC_NIBBLE_HPP
