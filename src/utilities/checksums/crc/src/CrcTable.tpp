/**
 * @file CrcTable.tpp
 * @brief Template implementation for CrcTable.
 */
#ifndef APEX_UTILITIES_CHECKSUMS_CRC_TABLE_TPP
#define APEX_UTILITIES_CHECKSUMS_CRC_TABLE_TPP

#include "src/utilities/compatibility/inc/compat_lang.hpp"
#include "src/utilities/checksums/crc/inc/CrcTable.hpp"

namespace apex {
namespace checksums {
namespace crc {

template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut, uint8_t Width>
constexpr apex::compat::array<T, CRC_TABLE_SIZE>
CrcTable<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>::generateTable() noexcept {
  using This = CrcTable<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>;

  constexpr uint8_t BIT_WIDTH = This::fetchWidth();
  constexpr T MASK = This::fetchMask();
  constexpr T TOPBIT = This::fetchTopBit();

  apex::compat::array<T, CRC_TABLE_SIZE> tbl{};

  for (size_t i = 0; i < CRC_TABLE_SIZE; ++i) {
    T v = static_cast<T>(i);
    APEX_IF_CONSTEXPR(!ReflectIn) { v <<= (BIT_WIDTH - 8); }

    // Perform 8 shift/XOR iterations
    for (uint8_t b = 0; b < 8; ++b) {
      APEX_IF_CONSTEXPR(ReflectIn) {
        v = (v & T(1)) ? static_cast<T>((v >> 1) ^ Poly) : static_cast<T>(v >> 1);
      }
      else {
        v = (v & TOPBIT) ? static_cast<T>((v << 1) ^ Poly) : static_cast<T>(v << 1);
      }
    }
    tbl[i] = static_cast<T>(v & MASK);
  }

  return tbl;
}

template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut, uint8_t Width>
inline void CrcTable<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>::updateImpl(
    T& rem, const uint8_t* data, size_t len) noexcept {
  using This = CrcTable<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>;

  constexpr uint8_t SHIFT = This::fetchWidth() - 8;
  constexpr T MASK = This::fetchMask();

  APEX_IF_CONSTEXPR(ReflectIn) {
    // LSB-first: table index = (rem ^ BYTE)
    while (len--) {
      const uint8_t BYTE = *data++;
      const uint8_t IDX = static_cast<uint8_t>((rem ^ BYTE) & 0xFFu);
      rem = static_cast<T>((TABLE[IDX] ^ (rem >> 8)) & MASK);
    }
  }
  else {
    // MSB-first: table index = ((rem >> SHIFT) ^ BYTE)
    while (len--) {
      const uint8_t BYTE = *data++;
      const uint8_t IDX = static_cast<uint8_t>(((rem >> SHIFT) ^ BYTE) & 0xFFu);
      rem = static_cast<T>((TABLE[IDX] ^ (rem << 8)) & MASK);
    }
  }
}

} // namespace crc
} // namespace checksums
} // namespace apex

#endif // APEX_UTILITIES_CHECKSUMS_CRC_TABLE_TPP
