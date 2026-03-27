/**
 * @file CrcNibble.tpp
 * @brief Template implementation for CrcNibble.
 */
#ifndef APEX_UTILITIES_CHECKSUMS_CRC_NIBBLE_TPP
#define APEX_UTILITIES_CHECKSUMS_CRC_NIBBLE_TPP

#include "src/utilities/compatibility/inc/compat_lang.hpp"
#include "src/utilities/checksums/crc/inc/CrcNibble.hpp"

namespace apex {
namespace checksums {
namespace crc {

template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut, uint8_t Width>
constexpr apex::compat::array<T, CRC_NIBBLE_TABLE_SIZE>
CrcNibble<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>::generateTable() noexcept {
  using This = CrcNibble<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>;
  constexpr uint8_t WIDTH = This::fetchWidth();
  constexpr T MASK = This::fetchMask();
  [[maybe_unused]] constexpr T TOPBIT = This::fetchTopBit();

  apex::compat::array<T, CRC_NIBBLE_TABLE_SIZE> tbl{};

  auto foldNibble = [&](T v) {
    APEX_IF_CONSTEXPR(ReflectIn) {
      // LSB-first nibble fold
      for (int b = 0; b < 4; ++b) {
        if (v & 1) {
          v = static_cast<T>(((v >> 1) ^ Poly) & MASK);
        } else {
          v = static_cast<T>((v >> 1) & MASK);
        }
      }
    }
    else {
      // MSB-first nibble fold
      for (int b = 0; b < 4; ++b) {
        if (v & TOPBIT) {
          v = static_cast<T>(((v << 1) ^ Poly) & MASK);
        } else {
          v = static_cast<T>((v << 1) & MASK);
        }
      }
    }
    return v;
  };

  for (size_t i = 0; i < CRC_NIBBLE_TABLE_SIZE; ++i) {
    T seed = static_cast<T>(i);
    APEX_IF_CONSTEXPR(!ReflectIn) { seed = static_cast<T>(i << (WIDTH - 4)); }
    tbl[i] = foldNibble(seed);
  }

  return tbl;
}

template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut, uint8_t Width>
inline void CrcNibble<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>::updateImpl(
    T& rem, const uint8_t* data, size_t len) noexcept {
  using This = CrcNibble<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>;
  constexpr uint8_t SHIFT8 = static_cast<uint8_t>(This::fetchWidth() - 8);
  constexpr uint8_t SHIFT4 = static_cast<uint8_t>(This::fetchWidth() - 4);
  constexpr T MASK = This::fetchMask();

  APEX_IF_CONSTEXPR(ReflectIn) {
    while (len--) {
      rem ^= static_cast<T>(*data++);
      for (int pass = 0; pass < 2; ++pass) {
        uint8_t idx = static_cast<uint8_t>(rem & 0xFu);
        rem = static_cast<T>((TABLE[idx] ^ (rem >> 4)) & MASK);
      }
    }
  }
  else {
    while (len--) {
      rem ^= static_cast<T>(static_cast<T>(*data++) << SHIFT8);
      for (int pass = 0; pass < 2; ++pass) {
        uint8_t idx = static_cast<uint8_t>(rem >> SHIFT4);
        rem = static_cast<T>((TABLE[idx] ^ ((rem << 4) & MASK)) & MASK);
      }
    }
  }
}

} // namespace crc
} // namespace checksums
} // namespace apex

#endif // APEX_UTILITIES_CHECKSUMS_CRC_NIBBLE_TPP
