#ifndef APEX_SYSTEM_CORE_DATA_PROXY_BYTE_MASK_PROXY_HPP
#define APEX_SYSTEM_CORE_DATA_PROXY_BYTE_MASK_PROXY_HPP
/**
 * @file ByteMaskProxy.hpp
 * @brief General-purpose AND/XOR byte mask queue.
 *
 * Provides a fixed-capacity queue of mask operations for byte-level data
 * transformation. The mask rule is:
 *   byte = (byte & AND[i]) ^ XOR[i]
 *
 * This is a mechanism primitive, not tied to any specific use case. Consumers
 * compose it for fault injection, data overrides, safing, or any scenario
 * that requires deterministic byte-level mutation.
 *
 * RT-safe: All operations are O(1), no dynamic allocation, noexcept.
 *
 * Usage:
 * @code
 *   ByteMaskProxy proxy;
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
namespace data_proxy {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of queued mask operations.
constexpr std::size_t MASK_MAX_ENTRIES = 4;

/// Maximum length of a single mask in bytes.
constexpr std::size_t MASK_MAX_LEN = 32;

/* ----------------------------- ByteMaskStatus ----------------------------- */

/**
 * @enum ByteMaskStatus
 * @brief Status codes for mask operations.
 */
enum class ByteMaskStatus : std::uint8_t {
  SUCCESS = 0,        ///< Operation succeeded.
  ERROR_EMPTY = 1,    ///< No mask queued.
  ERROR_PARAM = 2,    ///< Null data pointer or zero size.
  ERROR_FULL = 3,     ///< Mask queue is full.
  ERROR_TOO_LONG = 4, ///< Mask length exceeds MASK_MAX_LEN.
  ERROR_BOUNDS = 5    ///< Mask extends beyond data buffer.
};

/**
 * @brief Human-readable string for ByteMaskStatus.
 * @param s Status value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(ByteMaskStatus s) noexcept {
  switch (s) {
  case ByteMaskStatus::SUCCESS:
    return "SUCCESS";
  case ByteMaskStatus::ERROR_EMPTY:
    return "ERROR_EMPTY";
  case ByteMaskStatus::ERROR_PARAM:
    return "ERROR_PARAM";
  case ByteMaskStatus::ERROR_FULL:
    return "ERROR_FULL";
  case ByteMaskStatus::ERROR_TOO_LONG:
    return "ERROR_TOO_LONG";
  case ByteMaskStatus::ERROR_BOUNDS:
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
  std::size_t index = 0;                            ///< Starting byte index.
  std::array<std::uint8_t, MASK_MAX_LEN> andMask{}; ///< AND mask bytes.
  std::array<std::uint8_t, MASK_MAX_LEN> xorMask{}; ///< XOR mask bytes.
  std::uint8_t len = 0;                             ///< Actual mask length (0 = empty).
};

/* ----------------------------- ByteMaskProxy ----------------------------- */

/**
 * @class ByteMaskProxy
 * @brief Static-sized queue of AND/XOR masks for byte-level data mutation.
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
class ByteMaskProxy {
public:
  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. Queue starts empty. */
  ByteMaskProxy() noexcept = default;

  /* ----------------------------- Queue Operations ----------------------------- */

  /**
   * @brief Push a mask operation onto the queue.
   * @param index Starting byte index in target buffer.
   * @param andMask Pointer to AND mask bytes.
   * @param xorMask Pointer to XOR mask bytes.
   * @param len Length of mask in bytes (must be <= MASK_MAX_LEN).
   * @return SUCCESS or error status.
   * @note RT-safe: O(len) copy, bounded by MASK_MAX_LEN.
   */
  [[nodiscard]] ByteMaskStatus push(std::size_t index, const std::uint8_t* andMask,
                                    const std::uint8_t* xorMask, std::uint8_t len) noexcept {
    if (count_ >= MASK_MAX_ENTRIES) {
      return ByteMaskStatus::ERROR_FULL;
    }
    if (len > MASK_MAX_LEN) {
      return ByteMaskStatus::ERROR_TOO_LONG;
    }
    if (len > 0 && (andMask == nullptr || xorMask == nullptr)) {
      return ByteMaskStatus::ERROR_PARAM;
    }

    // Calculate insertion index (circular)
    const std::size_t insertIdx = (head_ + count_) % MASK_MAX_ENTRIES;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    // Copy mask data
    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = andMask[i];
      entry.xorMask[i] = xorMask[i];
    }

    ++count_;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Push a zero mask (forces bytes to 0x00).
   * @param index Starting byte index.
   * @param len Number of bytes to zero.
   * @return SUCCESS or error status.
   * @note RT-safe: O(len).
   */
  [[nodiscard]] ByteMaskStatus pushZeroMask(std::size_t index, std::uint8_t len) noexcept {
    if (count_ >= MASK_MAX_ENTRIES) {
      return ByteMaskStatus::ERROR_FULL;
    }
    if (len > MASK_MAX_LEN) {
      return ByteMaskStatus::ERROR_TOO_LONG;
    }

    const std::size_t insertIdx = (head_ + count_) % MASK_MAX_ENTRIES;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = 0x00;
      entry.xorMask[i] = 0x00;
    }

    ++count_;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Push a high mask (forces bytes to 0xFF).
   * @param index Starting byte index.
   * @param len Number of bytes to set high.
   * @return SUCCESS or error status.
   * @note RT-safe: O(len).
   */
  [[nodiscard]] ByteMaskStatus pushHighMask(std::size_t index, std::uint8_t len) noexcept {
    if (count_ >= MASK_MAX_ENTRIES) {
      return ByteMaskStatus::ERROR_FULL;
    }
    if (len > MASK_MAX_LEN) {
      return ByteMaskStatus::ERROR_TOO_LONG;
    }

    const std::size_t insertIdx = (head_ + count_) % MASK_MAX_ENTRIES;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = 0x00;
      entry.xorMask[i] = 0xFF;
    }

    ++count_;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Push a flip mask (inverts all bits).
   * @param index Starting byte index.
   * @param len Number of bytes to flip.
   * @return SUCCESS or error status.
   * @note RT-safe: O(len).
   */
  [[nodiscard]] ByteMaskStatus pushFlipMask(std::size_t index, std::uint8_t len) noexcept {
    if (count_ >= MASK_MAX_ENTRIES) {
      return ByteMaskStatus::ERROR_FULL;
    }
    if (len > MASK_MAX_LEN) {
      return ByteMaskStatus::ERROR_TOO_LONG;
    }

    const std::size_t insertIdx = (head_ + count_) % MASK_MAX_ENTRIES;
    MaskEntry& entry = masks_[insertIdx];

    entry.index = index;
    entry.len = len;

    for (std::uint8_t i = 0; i < len; ++i) {
      entry.andMask[i] = 0xFF;
      entry.xorMask[i] = 0xFF;
    }

    ++count_;
    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Remove the front mask from the queue.
   * @note RT-safe: O(1). No-op if queue is empty.
   */
  void pop() noexcept {
    if (count_ > 0) {
      // Clear the entry being removed
      masks_[head_].len = 0;
      head_ = (head_ + 1) % MASK_MAX_ENTRIES;
      --count_;
    }
  }

  /**
   * @brief Clear all queued mask operations.
   * @note RT-safe: O(MASK_MAX_ENTRIES).
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
   * @return MASK_MAX_ENTRIES.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return MASK_MAX_ENTRIES; }

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
  [[nodiscard]] ByteMaskStatus apply(std::uint8_t* data, std::size_t dataSize) noexcept {
    if (count_ == 0) {
      return ByteMaskStatus::ERROR_EMPTY;
    }
    if (data == nullptr || dataSize == 0) {
      return ByteMaskStatus::ERROR_PARAM;
    }

    const MaskEntry& entry = masks_[head_];
    const std::size_t endIdx = entry.index + entry.len;

    // Bounds check
    if (endIdx > dataSize) {
      return ByteMaskStatus::ERROR_BOUNDS;
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

    return ByteMaskStatus::SUCCESS;
  }

  /**
   * @brief Apply the front mask and pop it from the queue.
   * @param data Pointer to mutable data buffer.
   * @param dataSize Size of data buffer in bytes.
   * @return SUCCESS or error status.
   * @note RT-safe: O(mask length).
   */
  [[nodiscard]] ByteMaskStatus applyAndPop(std::uint8_t* data, std::size_t dataSize) noexcept {
    const ByteMaskStatus result = apply(data, dataSize);
    if (result == ByteMaskStatus::SUCCESS) {
      pop();
    }
    return result;
  }

private:
  std::array<MaskEntry, MASK_MAX_ENTRIES> masks_{}; ///< Circular buffer of masks.
  std::size_t head_ = 0;                            ///< Index of front mask.
  std::size_t count_ = 0;                           ///< Number of queued masks.
};

} // namespace data_proxy
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_PROXY_BYTE_MASK_PROXY_HPP
