#ifndef APEX_SYSTEM_CORE_DATA_PROXY_MASTER_DATA_PROXY_HPP
#define APEX_SYSTEM_CORE_DATA_PROXY_MASTER_DATA_PROXY_HPP
/**
 * @file MasterDataProxy.hpp
 * @brief Orchestrator for data transformations (endianness, byte masking).
 *
 * Composes EndiannessProxy and ByteMaskProxy into a single interface.
 * Uses compile-time template parameters to enable/disable features with
 * zero overhead when not used.
 *
 * RT-safe: All operations bounded O(sizeof(T)), noexcept, no allocation
 * after construction.
 *
 * Usage:
 * @code
 *   struct Packet { uint32_t a; uint16_t b; };
 *
 *   Packet input{0x12345678, 0xABCD};
 *
 *   // No transformations (passthrough)
 *   MasterDataProxy<Packet, false, false> proxy1(&input);
 *   proxy1.resolve();
 *   // proxy1.output() == input (same pointer)
 *
 *   // With endianness swap
 *   MasterDataProxy<Packet, true, false> proxy2(&input);
 *   proxy2.resolve();
 *   // proxy2.output() has swapped bytes
 *
 *   // With byte masking
 *   MasterDataProxy<Packet, false, true> proxy3(&input);
 *   proxy3.pushZeroMask(0, 4);  // Zero first 4 bytes
 *   proxy3.setMasksEnabled(true);
 *   proxy3.resolve();
 * @endcode
 */

#include "src/utilities/data_proxy/inc/EndiannessProxy.hpp"
#include "src/utilities/data_proxy/inc/ByteMaskProxy.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <variant>

namespace system_core {
namespace data_proxy {

/* ----------------------------- Constants ----------------------------- */

/// Number of proxy slots in status table.
constexpr std::size_t PROXY_STATUS_SLOTS = 8;

/* ----------------------------- ProxySlot ----------------------------- */

/**
 * @enum ProxySlot
 * @brief Logical indices for proxy status table.
 */
enum class ProxySlot : std::uint8_t {
  MASTER = 0,    ///< Master orchestration status.
  ENDIAN = 1,    ///< Endianness proxy status.
  BYTE_MASK = 2, ///< Byte mask proxy status.
  // Slots 3-7 reserved for future proxies (encryption, etc.)
};

/* ----------------------------- MasterStatus ----------------------------- */

/**
 * @enum MasterStatus
 * @brief Status codes for master orchestration.
 */
enum class MasterStatus : std::uint8_t {
  SUCCESS = 0,      ///< All proxies succeeded.
  ERROR_PARAM = 1,  ///< Invalid parameter (null pointer).
  ERROR_PROXIES = 2 ///< One or more proxies reported error.
};

/**
 * @brief Human-readable string for MasterStatus.
 * @param s Status value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(MasterStatus s) noexcept {
  switch (s) {
  case MasterStatus::SUCCESS:
    return "SUCCESS";
  case MasterStatus::ERROR_PARAM:
    return "ERROR_PARAM";
  case MasterStatus::ERROR_PROXIES:
    return "ERROR_PROXIES";
  }
  return "UNKNOWN";
}

/* ----------------------------- MasterDataProxy ----------------------------- */

/**
 * @class MasterDataProxy
 * @brief Orchestrates data transformations for a trivially-copyable type.
 *
 * Composes:
 *   - EndiannessProxy (when SwapRequired=true)
 *   - ByteMaskProxy (when MaskEnabled=true)
 *
 * When neither transformation is needed, output() returns the input pointer
 * directly (zero-copy passthrough).
 *
 * When transformations are enabled, an internal overlay buffer stores the
 * transformed data.
 *
 * @tparam T Trivially-copyable data type.
 * @tparam SwapRequired If true, perform endianness conversion.
 * @tparam MaskEnabled If true, support byte mask operations.
 *
 * @note RT-safe: All operations bounded, noexcept, no dynamic allocation.
 */
template <typename T, bool SwapRequired, bool MaskEnabled> class MasterDataProxy {
  static_assert(std::is_trivially_copyable_v<T>,
                "MasterDataProxy requires trivially copyable types");

  /// True if we need an overlay buffer for transformations.
  static constexpr bool NEEDS_OVERLAY = SwapRequired || MaskEnabled;

public:
  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct proxy from input data pointer.
   * @param input Pointer to source data (must remain valid).
   */
  explicit MasterDataProxy(const T* input) noexcept
      : in_{input}, out_{NEEDS_OVERLAY ? &overlay_ : const_cast<T*>(input)}, endian_{in_, out_} {
    status_.fill(0);
  }

  /* ----------------------------- Byte Masking ----------------------------- */

  /**
   * @brief Enable or disable byte mask application.
   * @param enabled True to apply queued masks during resolve().
   * @note Only effective when MaskEnabled=true.
   */
  void setMasksEnabled(bool enabled) noexcept {
    if constexpr (MaskEnabled) {
      masksEnabled_ = enabled;
    }
    (void)enabled; // Suppress warning when MaskEnabled=false
  }

  /**
   * @brief Check if byte masking is enabled.
   * @return True if masks will be applied during resolve().
   */
  [[nodiscard]] bool masksEnabled() const noexcept {
    if constexpr (MaskEnabled) {
      return masksEnabled_;
    }
    return false;
  }

  /**
   * @brief Push a custom mask onto the queue.
   * @param index Starting byte index.
   * @param andMask AND mask bytes.
   * @param xorMask XOR mask bytes.
   * @param len Mask length.
   * @return ByteMaskStatus.
   */
  [[nodiscard]] ByteMaskStatus pushMask(std::size_t index, const std::uint8_t* andMask,
                                        const std::uint8_t* xorMask, std::uint8_t len) noexcept {
    if constexpr (MaskEnabled) {
      return mask_.push(index, andMask, xorMask, len);
    }
    (void)index;
    (void)andMask;
    (void)xorMask;
    (void)len;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Push a zero mask (forces bytes to 0x00).
   * @param index Starting byte index.
   * @param len Number of bytes.
   * @return ByteMaskStatus.
   */
  [[nodiscard]] ByteMaskStatus pushZeroMask(std::size_t index, std::uint8_t len) noexcept {
    if constexpr (MaskEnabled) {
      return mask_.pushZeroMask(index, len);
    }
    (void)index;
    (void)len;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Push a high mask (forces bytes to 0xFF).
   * @param index Starting byte index.
   * @param len Number of bytes.
   * @return ByteMaskStatus.
   */
  [[nodiscard]] ByteMaskStatus pushHighMask(std::size_t index, std::uint8_t len) noexcept {
    if constexpr (MaskEnabled) {
      return mask_.pushHighMask(index, len);
    }
    (void)index;
    (void)len;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Push a flip mask (inverts all bits).
   * @param index Starting byte index.
   * @param len Number of bytes.
   * @return ByteMaskStatus.
   */
  [[nodiscard]] ByteMaskStatus pushFlipMask(std::size_t index, std::uint8_t len) noexcept {
    if constexpr (MaskEnabled) {
      return mask_.pushFlipMask(index, len);
    }
    (void)index;
    (void)len;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Pop the front mask from the queue.
   */
  void popMask() noexcept {
    if constexpr (MaskEnabled) {
      mask_.pop();
    }
  }

  /**
   * @brief Clear all queued masks.
   */
  void clearMasks() noexcept {
    if constexpr (MaskEnabled) {
      mask_.clear();
    }
  }

  /**
   * @brief Get number of queued masks.
   * @return Queue size.
   */
  [[nodiscard]] std::size_t maskCount() const noexcept {
    if constexpr (MaskEnabled) {
      return mask_.size();
    }
    return 0;
  }

  /* ----------------------------- Resolution ----------------------------- */

  /**
   * @brief Compute output from input, applying all enabled transformations.
   * @return MasterStatus indicating overall result.
   * @note RT-safe: O(sizeof(T)).
   *
   * Order of operations:
   *   1. Validate input pointer
   *   2. Apply endianness conversion (copy/swap to overlay)
   *   3. Apply byte masks (if enabled and masksEnabled_)
   *   4. Aggregate status from all proxies
   */
  MasterStatus resolve() noexcept {
    // Reset status for this cycle (single 64-bit store)
    std::uint64_t zero = 0;
    std::memcpy(status_.data(), &zero, sizeof(zero));

    // Validate input
    if (in_ == nullptr) {
      status_[static_cast<std::size_t>(ProxySlot::MASTER)] =
          static_cast<std::uint8_t>(MasterStatus::ERROR_PARAM);
      return MasterStatus::ERROR_PARAM;
    }

    // Step 1: Endianness (copy or swap)
    auto endianStatus = endian_.resolve();
    status_[static_cast<std::size_t>(ProxySlot::ENDIAN)] = static_cast<std::uint8_t>(endianStatus);

    // Step 2: Byte masking (if enabled)
    if constexpr (MaskEnabled) {
      if (masksEnabled_ && !mask_.empty()) {
        auto maskStatus = mask_.apply(reinterpret_cast<std::uint8_t*>(out_), sizeof(T));
        status_[static_cast<std::size_t>(ProxySlot::BYTE_MASK)] =
            static_cast<std::uint8_t>(maskStatus);
      }
    }

    // Aggregate: check if any proxy failed (single 64-bit check)
    // All status bytes except MASTER (byte 0) must be zero
    std::uint64_t packed;
    std::memcpy(&packed, status_.data(), sizeof(packed));
    const bool anyFailed = (packed >> 8) != 0;

    MasterStatus result = anyFailed ? MasterStatus::ERROR_PROXIES : MasterStatus::SUCCESS;
    status_[static_cast<std::size_t>(ProxySlot::MASTER)] = static_cast<std::uint8_t>(result);

    return result;
  }

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get input data pointer.
   * @return Const pointer to source data.
   */
  [[nodiscard]] const T* input() const noexcept { return in_; }

  /**
   * @brief Get output data pointer.
   * @return Pointer to transformed data (or input if no transformations).
   * @note Call resolve() before reading output.
   */
  [[nodiscard]] T* output() noexcept { return out_; }

  /**
   * @brief Get output as const pointer.
   * @return Const pointer to transformed data.
   */
  [[nodiscard]] const T* output() const noexcept { return out_; }

  /**
   * @brief Get output as byte pointer.
   * @return Pointer to raw bytes of output.
   */
  [[nodiscard]] std::uint8_t* outputBytes() noexcept {
    return reinterpret_cast<std::uint8_t*>(out_);
  }

  /**
   * @brief Get output as const byte pointer.
   * @return Const pointer to raw bytes of output.
   */
  [[nodiscard]] const std::uint8_t* outputBytes() const noexcept {
    return reinterpret_cast<const std::uint8_t*>(out_);
  }

  /**
   * @brief Get size of data type.
   * @return sizeof(T).
   */
  [[nodiscard]] static constexpr std::size_t size() noexcept { return sizeof(T); }

  /* ----------------------------- Status ----------------------------- */

  /**
   * @brief Get master status from last resolve().
   * @return MasterStatus.
   */
  [[nodiscard]] MasterStatus masterStatus() const noexcept {
    return static_cast<MasterStatus>(status_[static_cast<std::size_t>(ProxySlot::MASTER)]);
  }

  /**
   * @brief Get status code for a specific proxy slot.
   * @param slot Proxy slot index.
   * @return Raw status byte.
   */
  [[nodiscard]] std::uint8_t proxyStatus(ProxySlot slot) const noexcept {
    return status_[static_cast<std::size_t>(slot)];
  }

  /**
   * @brief Check if any proxy failed in last resolve().
   * @return True if any non-master slot is non-zero.
   */
  [[nodiscard]] bool anyProxyFailed() const noexcept {
    std::uint8_t anyFailed = 0;
    for (std::size_t i = 1; i < PROXY_STATUS_SLOTS; ++i) {
      anyFailed |= status_[i];
    }
    return anyFailed != 0;
  }

  /**
   * @brief Get copy of full status table.
   * @return Array of status bytes.
   */
  [[nodiscard]] std::array<std::uint8_t, PROXY_STATUS_SLOTS> statusCopy() const noexcept {
    return status_;
  }

  /* ----------------------------- Compile-Time Queries ----------------------------- */

  /**
   * @brief Check if endianness swapping is enabled.
   * @return SwapRequired template parameter.
   */
  [[nodiscard]] static constexpr bool swapRequired() noexcept { return SwapRequired; }

  /**
   * @brief Check if byte masking is supported.
   * @return MaskEnabled template parameter.
   */
  [[nodiscard]] static constexpr bool maskSupported() noexcept { return MaskEnabled; }

  /**
   * @brief Check if overlay buffer is used.
   * @return True if transformations require separate output buffer.
   */
  [[nodiscard]] static constexpr bool needsOverlay() noexcept { return NEEDS_OVERLAY; }

private:
  const T* in_;                             ///< Input data pointer.
  T* out_;                                  ///< Output data pointer.
  T overlay_{};                             ///< Overlay buffer (used when NEEDS_OVERLAY).
  EndiannessProxy<T, SwapRequired> endian_; ///< Endianness converter.
  [[no_unique_address]] std::conditional_t<MaskEnabled, ByteMaskProxy, std::monostate> mask_{};
  std::array<std::uint8_t, PROXY_STATUS_SLOTS> status_{}; ///< Proxy status table.
  bool masksEnabled_ = false;                             ///< Runtime mask enable flag.
};

} // namespace data_proxy
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_PROXY_MASTER_DATA_PROXY_HPP
