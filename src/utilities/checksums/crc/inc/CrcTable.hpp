#ifndef APEX_UTILITIES_CHECKSUMS_CRC_TABLE_HPP
#define APEX_UTILITIES_CHECKSUMS_CRC_TABLE_HPP

/**
 * @file CrcTable.hpp
 * @brief CRTP-based, table-driven CRC implementation.
 *
 * Generates a 256-entry lookup table at compile time and applies
 * an 8-bit slicing-by-1 update loop.
 *
 * @tparam T           Unsigned accumulator type.
 * @tparam Poly        Generator polynomial.
 * @tparam Initial     Initial remainder seed.
 * @tparam XorOut      Final XOR mask.
 * @tparam ReflectIn   If true, reflect each input byte (LSB-first).
 * @tparam ReflectOut  If true, reflect final CRC on finalize().
 * @tparam Width       CRC bit-width (default = sizeof(T)*8).
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

/** Number of entries in an 8-bit lookup table. */
static constexpr size_t CRC_TABLE_SIZE = 256;

/* ----------------------------- CrcTable ----------------------------- */

/**
 * @brief Table-driven CRC implementation (slicing-by-1).
 *
 * @note RT-safe: O(n) in input size, no allocations, no exceptions.
 *
 * Uses a precomputed 256-entry lookup table for byte-wise processing.
 */
template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut,
          uint8_t Width = static_cast<uint8_t>(sizeof(T) * 8)>
class CrcTable
    : public CrcBase<T, CrcTable<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>> {
  static_assert(apex::compat::is_unsigned_v<T>, "CrcTable requires an unsigned integer type");
  static_assert(Width > 0 && Width <= sizeof(T) * 8, "Width must be 1…8*sizeof(T)");

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
   * @brief Table-driven update (slicing-by-1).
   *
   * Applies the precomputed TABLE to each input byte.
   *
   * @param[in,out] rem Current remainder (accumulator).
   * @param[in]     data Pointer to input buffer.
   * @param[in]     len  Number of bytes to process.
   */
  static void updateImpl(T& rem, const uint8_t* data, size_t len) noexcept;

private:
  /**
   * @brief Compile-time generation of the 256-entry CRC table.
   *
   * Uses ReflectIn, Poly, and Width to compute each entry.
   *
   * @return Lookup table array.
   */
  static constexpr apex::compat::array<T, CRC_TABLE_SIZE> generateTable() noexcept;

  /// Compile-time CRC lookup table.
  static constexpr apex::compat::array<T, CRC_TABLE_SIZE> TABLE = generateTable();
};

} // namespace crc
} // namespace checksums
} // namespace apex

#include "src/utilities/checksums/crc/src/CrcTable.tpp"
#endif // APEX_UTILITIES_CHECKSUMS_CRC_TABLE_HPP
