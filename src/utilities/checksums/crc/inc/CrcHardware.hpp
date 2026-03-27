#ifndef APEX_UTILITIES_CHECKSUMS_CRC_HARDWARE_HPP
#define APEX_UTILITIES_CHECKSUMS_CRC_HARDWARE_HPP
/**
 * @file CrcHardware.hpp
 * @brief Hardware-accelerated CRC implementations using CPU intrinsics.
 *
 * Provides platform-specific CRC acceleration:
 * - x86/x64: SSE4.2 CRC32C instruction (_mm_crc32_u*)
 * - ARM/ARM64: CRC32 extension (__crc32*)
 *
 * Hardware CRC is ONLY available for the CRC-32C polynomial (iSCSI/SCTP/Btrfs).
 * Other polynomials must use software implementations (CrcTable, etc.).
 *
 * @note RT-safe: All operations are O(n) with no allocations or exceptions.
 */

#include "src/utilities/checksums/crc/inc/CrcBase.hpp"
#include "src/utilities/compatibility/inc/compat_type_traits.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ----------------------------- Platform Detection ----------------------------- */

// Detect SSE4.2 support (x86/x64)
#if defined(__SSE4_2__)
#define APEX_CRC_HAS_SSE42 1
#include <nmmintrin.h>
#else
#define APEX_CRC_HAS_SSE42 0
#endif

// Detect ARM CRC32 extension (ARM/ARM64)
#if defined(__ARM_FEATURE_CRC32)
#define APEX_CRC_HAS_ARM_CRC32 1
#include <arm_acle.h>
#else
#define APEX_CRC_HAS_ARM_CRC32 0
#endif

// Convenience macro: any hardware CRC available?
#define APEX_CRC_HAS_HARDWARE (APEX_CRC_HAS_SSE42 || APEX_CRC_HAS_ARM_CRC32)

namespace apex {
namespace checksums {
namespace crc {

/* ----------------------------- Constants ----------------------------- */

/**
 * @brief CRC-32C polynomial (iSCSI, SCTP, Btrfs, ext4).
 *
 * This is the ONLY polynomial supported by hardware CRC instructions.
 * - Catalog poly: 0x1EDC6F41
 * - Reflected poly: 0x82F63B78 (used internally by hardware)
 */
constexpr uint32_t CRC32C_POLY_REFLECTED = 0x82F63B78u;

/* ----------------------------- SSE4.2 Implementation ----------------------------- */

#if APEX_CRC_HAS_SSE42

/**
 * @brief SSE4.2 hardware-accelerated CRC-32C implementation.
 *
 * Uses the CRC32 instruction available on Intel Nehalem+ and AMD Bulldozer+ CPUs.
 * Achieves ~20-30 GB/s throughput (vs ~300 MB/s for software table lookup).
 *
 * @note Only valid for CRC-32C polynomial (0x82F63B78 reflected).
 *
 * @tparam T       Must be uint32_t
 * @tparam Poly    Must be CRC32C_POLY_REFLECTED (0x82F63B78)
 * @tparam Init    Initial CRC value (typically 0xFFFFFFFF)
 * @tparam XorOut  Final XOR value (typically 0xFFFFFFFF)
 */
template <typename T, T Poly, T Init, T XorOut, bool ReflectIn, bool ReflectOut,
          uint8_t Width = sizeof(T) * 8>
class CrcSse42 : public CrcBase<T, CrcSse42<T, Poly, Init, XorOut, ReflectIn, ReflectOut, Width>> {
  static_assert(apex::compat::is_same_v<T, uint32_t>, "SSE4.2 CRC32 only supports uint32_t");
  static_assert(Poly == CRC32C_POLY_REFLECTED,
                "SSE4.2 CRC32 only supports CRC-32C polynomial (0x82F63B78)");
  static_assert(Width == 32, "SSE4.2 CRC32 only supports 32-bit width");
  static_assert(ReflectIn == true, "SSE4.2 CRC32 requires reflected input");

  using This = CrcSse42<T, Poly, Init, XorOut, ReflectIn, ReflectOut, Width>;
  friend class CrcBase<T, This>;

public:
  static constexpr uint8_t fetchWidth() noexcept { return Width; }
  static constexpr T fetchInitial() noexcept { return Init; }
  static constexpr T fetchXorOut() noexcept { return XorOut; }
  static constexpr bool fetchReflectIn() noexcept { return ReflectIn; }
  static constexpr bool fetchReflectOut() noexcept { return ReflectOut; }

protected:
  /**
   * @brief Process data using SSE4.2 CRC32 instructions.
   *
   * Processes 8 bytes at a time on 64-bit, 4 bytes on 32-bit,
   * with byte-by-byte handling for the tail.
   */
  static void updateImpl(T& rem, const uint8_t* data, size_t len) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    // 64-bit: process 8 bytes at a time
    while (len >= 8) {
      uint64_t val;
      memcpy(&val, data, 8);
      rem = static_cast<uint32_t>(_mm_crc32_u64(rem, val));
      data += 8;
      len -= 8;
    }
    // Process remaining 4 bytes if present
    if (len >= 4) {
      uint32_t val;
      memcpy(&val, data, 4);
      rem = _mm_crc32_u32(rem, val);
      data += 4;
      len -= 4;
    }
#else
    // 32-bit: process 4 bytes at a time
    while (len >= 4) {
      uint32_t val;
      memcpy(&val, data, 4);
      rem = _mm_crc32_u32(rem, val);
      data += 4;
      len -= 4;
    }
#endif
    // Process remaining bytes
    while (len > 0) {
      rem = _mm_crc32_u8(rem, *data);
      ++data;
      --len;
    }
  }
};

#endif // APEX_CRC_HAS_SSE42

/* ----------------------------- ARM CRC32 Implementation ----------------------------- */

#if APEX_CRC_HAS_ARM_CRC32

/**
 * @brief ARM CRC32 extension hardware-accelerated CRC-32C implementation.
 *
 * Uses the CRC32 instructions available on ARMv8-A with CRC extension.
 * Achieves similar throughput to SSE4.2 (~20+ GB/s).
 *
 * @note Only valid for CRC-32C polynomial (0x82F63B78 reflected).
 */
template <typename T, T Poly, T Init, T XorOut, bool ReflectIn, bool ReflectOut,
          uint8_t Width = sizeof(T) * 8>
class CrcArm : public CrcBase<T, CrcArm<T, Poly, Init, XorOut, ReflectIn, ReflectOut, Width>> {
  static_assert(apex::compat::is_same_v<T, uint32_t>, "ARM CRC32 only supports uint32_t");
  static_assert(Poly == CRC32C_POLY_REFLECTED,
                "ARM CRC32C only supports CRC-32C polynomial (0x82F63B78)");
  static_assert(Width == 32, "ARM CRC32 only supports 32-bit width");
  static_assert(ReflectIn == true, "ARM CRC32 requires reflected input");

  using This = CrcArm<T, Poly, Init, XorOut, ReflectIn, ReflectOut, Width>;
  friend class CrcBase<T, This>;

public:
  static constexpr uint8_t fetchWidth() noexcept { return Width; }
  static constexpr T fetchInitial() noexcept { return Init; }
  static constexpr T fetchXorOut() noexcept { return XorOut; }
  static constexpr bool fetchReflectIn() noexcept { return ReflectIn; }
  static constexpr bool fetchReflectOut() noexcept { return ReflectOut; }

protected:
  /**
   * @brief Process data using ARM CRC32C instructions.
   */
  static void updateImpl(T& rem, const uint8_t* data, size_t len) noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    // 64-bit ARM: process 8 bytes at a time
    while (len >= 8) {
      uint64_t val;
      memcpy(&val, data, 8);
      rem = __crc32cd(rem, val);
      data += 8;
      len -= 8;
    }
    if (len >= 4) {
      uint32_t val;
      memcpy(&val, data, 4);
      rem = __crc32cw(rem, val);
      data += 4;
      len -= 4;
    }
#else
    // 32-bit ARM: process 4 bytes at a time
    while (len >= 4) {
      uint32_t val;
      memcpy(&val, data, 4);
      rem = __crc32cw(rem, val);
      data += 4;
      len -= 4;
    }
#endif
    // Process remaining bytes
    while (len > 0) {
      rem = __crc32cb(rem, *data);
      ++data;
      --len;
    }
  }
};

#endif // APEX_CRC_HAS_ARM_CRC32

/* ----------------------------- Platform-Agnostic Alias ----------------------------- */

/**
 * @brief Hardware-accelerated CRC-32C, with automatic platform selection.
 *
 * Selects the best available implementation:
 * - SSE4.2 on x86/x64 with SSE4.2 support
 * - ARM CRC32 on ARM with CRC extension
 * - Falls back to CrcTable (software) if no hardware support
 *
 * Usage:
 *   apex::checksums::crc::Crc32CHardware calc;
 *   uint32_t crc{};
 *   calc.calculate(data, crc);
 */
#if APEX_CRC_HAS_SSE42
template <uint32_t Init = 0xFFFFFFFFu, uint32_t XorOut = 0xFFFFFFFFu>
using Crc32CHardware = CrcSse42<uint32_t, CRC32C_POLY_REFLECTED, Init, XorOut, true, false>;
#elif APEX_CRC_HAS_ARM_CRC32
template <uint32_t Init = 0xFFFFFFFFu, uint32_t XorOut = 0xFFFFFFFFu>
using Crc32CHardware = CrcArm<uint32_t, CRC32C_POLY_REFLECTED, Init, XorOut, true, false>;
#else
// Fallback to software - will be defined after including CrcTable.hpp
// Forward declaration; actual definition in Crc.hpp
#endif

} // namespace crc
} // namespace checksums
} // namespace apex

#endif // APEX_UTILITIES_CHECKSUMS_CRC_HARDWARE_HPP
