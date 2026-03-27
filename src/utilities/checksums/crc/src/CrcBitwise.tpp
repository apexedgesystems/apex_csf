/**
 * @file CrcBitwise.tpp
 * @brief Template implementation for CrcBitwise.
 */
#ifndef APEX_UTILITIES_CHECKSUMS_CRC_BITWISE_TPP
#define APEX_UTILITIES_CHECKSUMS_CRC_BITWISE_TPP

#include "src/utilities/compatibility/inc/compat_lang.hpp"
#include "src/utilities/checksums/crc/inc/CrcBitwise.hpp"

namespace apex {
namespace checksums {
namespace crc {

template <typename T, T Poly, T Initial, T XorOut, bool ReflectIn, bool ReflectOut, uint8_t Width>
inline void CrcBitwise<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>::updateImpl(
    T& rem, const uint8_t* data, size_t len) noexcept {
  using This = CrcBitwise<T, Poly, Initial, XorOut, ReflectIn, ReflectOut, Width>;

  constexpr uint8_t WIDTH = This::fetchWidth();
  constexpr T MASK = This::fetchMask();
  constexpr T TOPBIT = This::fetchTopBit();

  APEX_IF_CONSTEXPR(ReflectIn) {
    // LSB-first processing
    while (len--) {
      rem ^= static_cast<T>(*data++);
      for (uint8_t b = 0; b < 8; ++b) {
        if (rem & 1) {
          rem = static_cast<T>((rem >> 1) ^ Poly);
        } else {
          rem = static_cast<T>(rem >> 1);
        }
      }
    }
  }
  else {
    // MSB-first processing
    constexpr uint8_t SHIFT = WIDTH - 8;
    while (len--) {
      rem ^= static_cast<T>(static_cast<T>(*data++) << SHIFT);
      for (uint8_t b = 0; b < 8; ++b) {
        if (rem & TOPBIT) {
          rem = static_cast<T>((rem << 1) ^ Poly);
        } else {
          rem = static_cast<T>(rem << 1);
        }
        rem &= MASK;
      }
    }
  }
}

} // namespace crc
} // namespace checksums
} // namespace apex

#endif // APEX_UTILITIES_CHECKSUMS_CRC_BITWISE_TPP
