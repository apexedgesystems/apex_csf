#ifndef APEX_UTILITIES_CHECKSUMS_CRC_BASE_HPP
#define APEX_UTILITIES_CHECKSUMS_CRC_BASE_HPP

/**
 * @file CrcBase.hpp
 * @brief CRTP base class for streaming and one-shot CRC computations.
 *
 * Provides a portable, container-friendly CRC interface using compatibility
 * shims for spans. Supports C++17 and later.
 */

#include <stddef.h>
#include <stdint.h>

// Compatibility shims
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/compatibility/inc/compat_type_traits.hpp"

namespace apex {
namespace checksums {
namespace crc {

/* ----------------------------- Constants ----------------------------- */

/**
 * @enum Status
 * @brief Status codes returned by CRC operations.
 */
enum class Status : uint8_t {
  SUCCESS = 0,   /**< Computation succeeded. */
  ERROR_CALC = 1 /**< Generic error during calculation. */
};

/* ----------------------------- CrcBase ----------------------------- */

/**
 * @brief CRTP base class for streaming and one-shot CRC computations.
 *
 * @note RT-safe: All operations are O(n) in input size with no dynamic
 *       allocations, no exceptions, and no blocking operations.
 *
 * @tparam T       Unsigned accumulator type (`uint8_t`…`uint64_t`).
 * @tparam Derived CRTP policy class implementing fetch./updateImpl().
 *
 * Derived must implement:
 * - static constexpr uint8_t fetchWidth()      noexcept
 * - static constexpr T       fetchInitial()    noexcept
 * - static constexpr T       fetchXorOut()     noexcept
 * - static constexpr bool    fetchReflectIn()  noexcept
 * - static constexpr bool    fetchReflectOut() noexcept
 * - static void              updateImpl(T &rem,
 *                                        const uint8_t *data,
 *                                        size_t len) noexcept
 *
 * Provides:
 * - Lifecycle: reset/update/finalize
 * - One-shot calculate() overloads
 * - Masking and optional bit-reflection
 * - Container-friendly API: pointers, std::vector, apex::compat::bytes_span
 */
template <typename T, typename Derived> class CrcBase {
  static_assert(apex::compat::is_unsigned_v<T>, "CrcBase requires an unsigned integer type");

public:
  /// Construct and seed the remainder.
  constexpr CrcBase() noexcept : rem_(fetchInitial()) {}

  /// Reset streaming remainder to the initial seed.
  constexpr void reset() noexcept { rem_ = fetchInitial(); }

  /**
   * @brief Feed raw bytes into the CRC engine.
   * @param data Pointer to input buffer.
   * @param len  Number of bytes to process.
   * @return Status::SUCCESS
   */
  Status update(const uint8_t* data, size_t len) noexcept;

  /**
   * @brief Feed a span of bytes into the CRC engine.
   * @param data Span of input bytes.
   * @return Status::SUCCESS
   */
  Status update(apex::compat::bytes_span data) noexcept;

  /**
   * @brief Finalize computation: reflect-out, xor-out, mask, and write result.
   * @param out Computed CRC value.
   * @return Status::SUCCESS
   */
  Status finalize(T& out) const noexcept;

  /**
   * @brief One-shot CRC on raw buffer (reset → update → finalize).
   * @param data Pointer to input buffer.
   * @param len  Number of bytes to process.
   * @param out  Computed CRC value.
   * @return Status::SUCCESS
   */
  Status calculate(const uint8_t* data, size_t len, T& out) noexcept;

  /**
   * @brief One-shot CRC on a span of input bytes.
   * @param data Span of input bytes.
   * @param out  Computed CRC value.
   * @return Status::SUCCESS
   */
  Status calculate(apex::compat::bytes_span data, T& out) noexcept;

#ifdef APEX_COMPAT_HAS_VECTOR
  /**
   * @brief One-shot CRC on a std::vector of bytes.
   * @param data Vector of input bytes.
   * @param out  Computed CRC value.
   * @return Status::SUCCESS
   */
  Status calculate(const std::vector<uint8_t>& data, T& out) noexcept;
#endif

protected:
  T rem_; /**< Current remainder (internal state). */

public:
  /** @name Policy forwarders (delegated to Derived) */
  ///@{
  static constexpr uint8_t fetchWidth() noexcept { return Derived::fetchWidth(); }
  static constexpr T fetchInitial() noexcept { return Derived::fetchInitial(); }
  static constexpr T fetchXorOut() noexcept { return Derived::fetchXorOut(); }
  static constexpr bool fetchReflectIn() noexcept { return Derived::fetchReflectIn(); }
  static constexpr bool fetchReflectOut() noexcept { return Derived::fetchReflectOut(); }
  static constexpr T fetchTopBit() noexcept { return T(1) << (fetchWidth() - 1); }
  static constexpr T fetchMask() noexcept {
    return fetchWidth() == sizeof(T) * 8 ? ~T(0) : (T(1) << fetchWidth()) - 1;
  }
  ///@}

  /**
   * @brief Reflect the low `width` bits of `v`.
   * @param v     Value to reflect.
   * @param width Number of bits to reflect (1…width).
   * @return Reflected bit pattern.
   */
  static constexpr T reflectBits(T v, uint8_t width) noexcept;
};

} // namespace crc
} // namespace checksums
} // namespace apex

#include "src/utilities/checksums/crc/src/CrcBase.tpp"
#endif // APEX_UTILITIES_CHECKSUMS_CRC_BASE_HPP
