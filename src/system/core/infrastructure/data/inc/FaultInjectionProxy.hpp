#ifndef APEX_SYSTEM_CORE_DATA_FAULT_INJECTION_PROXY_HPP
#define APEX_SYSTEM_CORE_DATA_FAULT_INJECTION_PROXY_HPP
/**
 * @file FaultInjectionProxy.hpp
 * @brief Static-sized fault injection via AND/XOR byte masks.
 *
 * Provides a fixed-capacity queue of mask operations for fault injection.
 * Mask rule: byte = (byte & AND[i]) ^ XOR[i]
 *
 * RT-safe: All operations are O(1), no dynamic allocation, noexcept.
 *
 * Usage:
 * @code
 *   FaultInjectionProxy proxy;
 *
 *   // Queue a mask to force bytes 4-5 to zero
 *   std::array<std::uint8_t, 2> andMask = {0x00, 0x00};
 *   std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};
 *   proxy.push(4, andMask.data(), xorMask.data(), 2);
 *
 *   // Apply to data buffer
 *   std::array<std::uint8_t, 16> data = {...};
 *   proxy.apply(data.data(), data.size());
 * @endcode
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace system_core {
namespace data {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of queued mask operations.
constexpr std::size_t FAULT_MAX_MASKS = 4;

/// Maximum length of a single mask in bytes.
constexpr std::size_t FAULT_MAX_MASK_LEN = 32;

/* ----------------------------- FaultStatus ----------------------------- */

/**
 * @enum FaultStatus
 * @brief Status codes for fault mask operations.
 */
enum class FaultStatus : std::uint8_t {
  SUCCESS = 0,        ///< Operation succeeded.
  ERROR_EMPTY = 1,    ///< No mask queued.
  ERROR_PARAM = 2,    ///< Null data pointer or zero size.
  ERROR_FULL = 3,     ///< Mask queue is full.
  ERROR_TOO_LONG = 4, ///< Mask length exceeds MAX_MASK_LEN.
  ERROR_BOUNDS = 5    ///< Mask extends beyond data buffer.
};

/**
 * @brief Human-readable string for FaultStatus.
 * @param s Status value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(FaultStatus s) noexcept {
  switch (s) {
  case FaultStatus::SUCCESS:
    return "SUCCESS";
  case FaultStatus::ERROR_EMPTY:
    return "ERROR_EMPTY";
  case FaultStatus::ERROR_PARAM:
    return "ERROR_PARAM";
  case FaultStatus::ERROR_FULL:
    return "ERROR_FULL";
  case FaultStatus::ERROR_TOO_LONG:
    return "ERROR_TOO_LONG";
  case FaultStatus::ERROR_BOUNDS:
    return "ERROR_BOUNDS";
  }
  return "UNKNOWN";
}

/* ----------------------------- MaskEntry ----------------------------- */

/**
 * @struct MaskEntry
 * @brief Single mask operation with static storage.
 */
struct MaskEntry {
  std::size_t index = 0;                                  ///< Starting byte index.
  std::array<std::uint8_t, FAULT_MAX_MASK_LEN> andMask{}; ///< AND mask bytes.
  std::array<std::uint8_t, FAULT_MAX_MASK_LEN> xorMask{}; ///< XOR mask bytes.
  std::uint8_t len = 0;                                   ///< Actual mask length (0 = empty).
};

/* ----------------------------- FaultInjectionProxy ----------------------------- */

/**
 * @class FaultInjectionProxy
 * @brief Static-sized queue of AND/XOR masks for fault injection.
 *
 * Implements a circular buffer of mask operations. The front mask
 * is applied when apply() is called; masks are explicitly removed
 * via pop().
 *
 * Common mask patterns:
 *   - Zero: AND=0x00, XOR=0x00 -> forces byte to 0
 *   - High: AND=0x00, XOR=0xFF -> forces byte to 0xFF
 *   - Flip: AND=0xFF, XOR=0xFF -> inverts all bits
 *   - Set:  AND=0x00, XOR=val  -> forces byte to val
 *
 * @note RT-safe: All operations O(1), noexcept, no allocation.
 */
class FaultInjectionProxy {
public:
  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. Queue starts empty. */
  FaultInjectionProxy() noexcept = default;

  /* ----------------------------- Queue Operations ----------------------------- */

  /**
   * @brief Push a mask operation onto the queue.
   * @param index Starting byte index in target buffer.
   * @param andMask Pointer to AND mask bytes.
   * @param xorMask Pointer to XOR mask bytes.
   * @param len Length of mask in bytes (must be <= FAULT_MAX_MASK_LEN).
   * @return SUCCESS or error status.
   * @note RT-safe: O(len) copy, bounded by FAULT_MAX_MASK_LEN.
   */
  [[nodiscard]] FaultStatus push(std::size_t index, const std::uint8_t* andMask,
                                 const std::uint8_t* xorMask, std::uint8_t len) noexcept {
    if (count_ >= FAULT_MAX_MASKS) {
      return FaultStatus::ERROR_FULL;
    }
    if (len > FAULT_MAX_MASK_LEN) {
      return FaultStatus::ERROR_TOO_LONG;
    }
    if (len > 0 && (andMask == nullptr || xorMask == nullptr)) {
      return FaultStatus::ERROR_PARAM;
    }

    // Calculate insertion index (circular)
    const std::size_t insertIdx = (head_ + count_) % FAULT_MAX_MASKS;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    // Copy mask data
    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = andMask[i];
      entry.xorMask[i] = xorMask[i];
    }

    ++count_;
    return FaultStatus::SUCCESS;
  }

  /**
   * @brief Push a zero mask (forces bytes to 0x00).
   * @param index Starting byte index.
   * @param len Number of bytes to zero.
   * @return SUCCESS or error status.
   * @note RT-safe: O(len).
   */
  [[nodiscard]] FaultStatus pushZeroMask(std::size_t index, std::uint8_t len) noexcept {
    if (count_ >= FAULT_MAX_MASKS) {
      return FaultStatus::ERROR_FULL;
    }
    if (len > FAULT_MAX_MASK_LEN) {
      return FaultStatus::ERROR_TOO_LONG;
    }

    const std::size_t insertIdx = (head_ + count_) % FAULT_MAX_MASKS;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = 0x00;
      entry.xorMask[i] = 0x00;
    }

    ++count_;
    return FaultStatus::SUCCESS;
  }

  /**
   * @brief Push a high mask (forces bytes to 0xFF).
   * @param index Starting byte index.
   * @param len Number of bytes to set high.
   * @return SUCCESS or error status.
   * @note RT-safe: O(len).
   */
  [[nodiscard]] FaultStatus pushHighMask(std::size_t index, std::uint8_t len) noexcept {
    if (count_ >= FAULT_MAX_MASKS) {
      return FaultStatus::ERROR_FULL;
    }
    if (len > FAULT_MAX_MASK_LEN) {
      return FaultStatus::ERROR_TOO_LONG;
    }

    const std::size_t insertIdx = (head_ + count_) % FAULT_MAX_MASKS;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = 0x00;
      entry.xorMask[i] = 0xFF;
    }

    ++count_;
    return FaultStatus::SUCCESS;
  }

  /**
   * @brief Push a flip mask (inverts all bits).
   * @param index Starting byte index.
   * @param len Number of bytes to flip.
   * @return SUCCESS or error status.
   * @note RT-safe: O(len).
   */
  [[nodiscard]] FaultStatus pushFlipMask(std::size_t index, std::uint8_t len) noexcept {
    if (count_ >= FAULT_MAX_MASKS) {
      return FaultStatus::ERROR_FULL;
    }
    if (len > FAULT_MAX_MASK_LEN) {
      return FaultStatus::ERROR_TOO_LONG;
    }

    const std::size_t insertIdx = (head_ + count_) % FAULT_MAX_MASKS;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = 0xFF;
      entry.xorMask[i] = 0xFF;
    }

    ++count_;
    return FaultStatus::SUCCESS;
  }

  /**
   * @brief Remove the front mask from the queue.
   * @note RT-safe: O(1). No-op if queue is empty.
   */
  void pop() noexcept {
    if (count_ > 0) {
      // Clear the entry being removed
      masks_[head_].len = 0;
      head_ = (head_ + 1) % FAULT_MAX_MASKS;
      --count_;
    }
  }

  /**
   * @brief Clear all queued mask operations.
   * @note RT-safe: O(FAULT_MAX_MASKS).
   */
  void clear() noexcept {
    for (auto& entry : masks_) {
      entry.len = 0;
    }
    head_ = 0;
    count_ = 0;
  }

  /* ----------------------------- Query ----------------------------- */

  /**
   * @brief Check if queue is empty.
   * @return true if no masks queued.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

  /**
   * @brief Get number of queued masks.
   * @return Queue size.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  /**
   * @brief Get maximum queue capacity.
   * @return FAULT_MAX_MASKS.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return FAULT_MAX_MASKS; }

  /* ----------------------------- Application ----------------------------- */

  /**
   * @brief Apply the front mask to a data buffer.
   * @param data Pointer to mutable data buffer.
   * @param dataSize Size of data buffer in bytes.
   * @return SUCCESS or error status.
   * @note RT-safe: O(mask length). Does NOT pop the mask.
   *
   * The mask is applied as: byte[i] = (byte[i] & AND[i]) ^ XOR[i]
   */
  [[nodiscard]] FaultStatus apply(std::uint8_t* data, std::size_t dataSize) noexcept {
    if (count_ == 0) {
      return FaultStatus::ERROR_EMPTY;
    }
    if (data == nullptr || dataSize == 0) {
      return FaultStatus::ERROR_PARAM;
    }

    const MaskEntry& entry = masks_[head_];
    const std::size_t endIdx = entry.index + entry.len;

    // Bounds check
    if (endIdx > dataSize) {
      return FaultStatus::ERROR_BOUNDS;
    }

    // Apply mask: (byte & AND) ^ XOR
    // Optimize: process 8 bytes at a time when possible
    std::uint8_t* p = data + entry.index;
    const std::size_t len = entry.len;

    // Process 8-byte chunks for better throughput
    std::size_t i = 0;
    for (; i + 8 <= len; i += 8) {
      std::uint64_t chunk;
      std::memcpy(&chunk, p + i, 8);
      std::uint64_t andChunk;
      std::memcpy(&andChunk, &entry.andMask[i], 8);
      std::uint64_t xorChunk;
      std::memcpy(&xorChunk, &entry.xorMask[i], 8);
      chunk = (chunk & andChunk) ^ xorChunk;
      std::memcpy(p + i, &chunk, 8);
    }

    // Handle remaining bytes
    for (; i < len; ++i) {
      p[i] = static_cast<std::uint8_t>((p[i] & entry.andMask[i]) ^ entry.xorMask[i]);
    }

    return FaultStatus::SUCCESS;
  }

  /**
   * @brief Apply the front mask and pop it from the queue.
   * @param data Pointer to mutable data buffer.
   * @param dataSize Size of data buffer in bytes.
   * @return SUCCESS or error status.
   * @note RT-safe: O(mask length).
   */
  [[nodiscard]] FaultStatus applyAndPop(std::uint8_t* data, std::size_t dataSize) noexcept {
    const FaultStatus result = apply(data, dataSize);
    if (result == FaultStatus::SUCCESS) {
      pop();
    }
    return result;
  }

private:
  std::array<MaskEntry, FAULT_MAX_MASKS> masks_{}; ///< Circular buffer of masks.
  std::size_t head_ = 0;                           ///< Index of front mask.
  std::size_t count_ = 0;                          ///< Number of queued masks.
};

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_FAULT_INJECTION_PROXY_HPP
