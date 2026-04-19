#ifndef APEX_SYSTEM_CORE_DATA_PROXY_ENDIANNESS_PROXY_HPP
#define APEX_SYSTEM_CORE_DATA_PROXY_ENDIANNESS_PROXY_HPP
/**
 * @file EndiannessProxy.hpp
 * @brief Compile-time endianness conversion proxy.
 *
 * Provides byte-swap support for cross-platform data exchange.
 * Uses raw pointers (no shared_ptr) for RT-safety.
 *
 * For scalar types, uses apex::compat::byteswap directly.
 * For struct types, users must provide a free function:
 *   void endianSwap(const T& in, T& out) noexcept;
 *
 * RT-safe: All operations are O(sizeof(T)), no allocation, noexcept.
 *
 * Usage:
 * @code
 *   // Scalar type - automatic
 *   std::uint32_t in = 0x12345678;
 *   std::uint32_t out;
 *   EndiannessProxy<std::uint32_t, true> proxy(&in, &out);
 *   proxy.resolve();  // out = 0x78563412
 *
 *   // Struct type - requires user-defined endianSwap
 *   struct MyPacket { uint32_t a; uint16_t b; };
 *   void endianSwap(const MyPacket& in, MyPacket& out) noexcept {
 *     out.a = apex::compat::byteswap(in.a);
 *     out.b = apex::compat::byteswap(in.b);
 *   }
 *   MyPacket pktIn, pktOut;
 *   EndiannessProxy<MyPacket, true> proxy(&pktIn, &pktOut);
 *   proxy.resolve();
 * @endcode
 */

#include "src/utilities/compatibility/inc/compat_byteswap.hpp"

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace system_core {
namespace data_proxy {

/* ----------------------------- EndianStatus ----------------------------- */

/**
 * @enum EndianStatus
 * @brief Status codes for endianness operations.
 */
enum class EndianStatus : std::uint8_t {
  SUCCESS = 0 ///< Operation completed successfully.
};

/* ----------------------------- Traits ----------------------------- */

namespace detail {

/**
 * @brief Trait to detect if endianSwap(const T&, T&) is defined via ADL.
 */
template <typename T, typename = void> struct HasEndianSwap : std::false_type {};

template <typename T>
struct HasEndianSwap<
    T, std::void_t<decltype(endianSwap(std::declval<const T&>(), std::declval<T&>()))>>
    : std::true_type {};

/**
 * @brief Trait to detect if type is a swappable scalar.
 *
 * Scalars are: integral (except bool), or floating-point.
 */
template <typename T>
struct IsSwappableScalar
    : std::bool_constant<(std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>) ||
                         std::is_floating_point_v<T>> {};

} // namespace detail

/* ----------------------------- Scalar Swap Helpers ----------------------------- */

/**
 * @brief Swap bytes of an integral value.
 * @tparam T Integral type (not bool).
 * @param v Input value.
 * @return Byteswapped value.
 * @note RT-safe: O(1).
 */
template <typename T>
[[nodiscard]] inline std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>, T>
swapBytes(T v) noexcept {
  return apex::compat::byteswap(v);
}

/**
 * @brief Swap bytes of a floating-point value.
 * @tparam T Floating-point type (float or double).
 * @param v Input value.
 * @return Byteswapped value.
 * @note RT-safe: O(1).
 */
template <typename T>
[[nodiscard]] inline std::enable_if_t<std::is_floating_point_v<T>, T> swapBytes(T v) noexcept {
  return apex::compat::byteswapIeee(v);
}

/* ----------------------------- EndiannessProxy ----------------------------- */

/**
 * @class EndiannessProxy
 * @brief Compile-time endianness conversion between input and output buffers.
 *
 * When SwapRequired is false, resolve() simply copies input to output.
 * When SwapRequired is true, resolve() performs byte-swapping:
 *   - For scalar types: uses swapBytes()
 *   - For struct types: calls user-defined endianSwap(in, out) via ADL
 *
 * @tparam T Type of data to convert.
 * @tparam SwapRequired If true, perform byte-swapping; if false, just copy.
 *
 * @note RT-safe: All operations bounded O(sizeof(T)), noexcept, no allocation.
 */
template <typename T, bool SwapRequired> class EndiannessProxy {
  static_assert(std::is_trivially_copyable_v<T>,
                "EndiannessProxy requires trivially copyable types");

public:
  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct proxy with input/output pointers.
   * @param in Pointer to input data (read-only).
   * @param out Pointer to output data (written on resolve).
   * @note Both pointers must remain valid for lifetime of proxy.
   */
  EndiannessProxy(const T* in, T* out) noexcept : in_{in}, out_{out} {}

  /* ----------------------------- Operations ----------------------------- */

  /**
   * @brief Copy/swap from input to output.
   * @return SUCCESS.
   * @note RT-safe: O(sizeof(T)).
   *
   * If SwapRequired is false, performs a simple copy.
   * If SwapRequired is true:
   *   - For scalars: swaps bytes directly
   *   - For structs: calls endianSwap(in, out) via ADL
   */
  EndianStatus resolve() noexcept {
    if constexpr (!SwapRequired) {
      // No swap needed - just copy
      if (in_ != out_) {
        *out_ = *in_;
      }
    } else {
      // Swap required
      if constexpr (detail::IsSwappableScalar<T>::value) {
        // Direct scalar swap
        *out_ = swapBytes(*in_);
      } else if constexpr (detail::HasEndianSwap<T>::value) {
        // User-defined struct swap via ADL
        endianSwap(*in_, *out_);
      } else {
        // Fallback: just copy (user must handle swap elsewhere)
        static_assert(
            detail::HasEndianSwap<T>::value || detail::IsSwappableScalar<T>::value,
            "SwapRequired=true requires scalar type or endianSwap(const T&, T&) function");
      }
    }
    return EndianStatus::SUCCESS;
  }

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get input pointer.
   * @return Const pointer to input data.
   */
  [[nodiscard]] const T* input() const noexcept { return in_; }

  /**
   * @brief Get output pointer.
   * @return Pointer to output data.
   */
  [[nodiscard]] T* output() noexcept { return out_; }

  /**
   * @brief Check if byte-swapping is enabled.
   * @return SwapRequired template parameter.
   */
  [[nodiscard]] static constexpr bool swapRequired() noexcept { return SwapRequired; }

private:
  const T* in_; ///< Input data pointer.
  T* out_;      ///< Output data pointer.
};

/* ----------------------------- Specialization for no-swap ----------------------------- */

/**
 * @brief Optimized specialization when no swap is needed.
 *
 * When SwapRequired is false and in==out, resolve() is a no-op.
 */
template <typename T> class EndiannessProxy<T, false> {
  static_assert(std::is_trivially_copyable_v<T>,
                "EndiannessProxy requires trivially copyable types");

public:
  EndiannessProxy(const T* in, T* out) noexcept : in_{in}, out_{out} {}

  EndianStatus resolve() noexcept {
    if (in_ != out_) {
      *out_ = *in_;
    }
    return EndianStatus::SUCCESS;
  }

  [[nodiscard]] const T* input() const noexcept { return in_; }
  [[nodiscard]] T* output() noexcept { return out_; }
  [[nodiscard]] static constexpr bool swapRequired() noexcept { return false; }

private:
  const T* in_;
  T* out_;
};

} // namespace data_proxy
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_PROXY_ENDIANNESS_PROXY_HPP
