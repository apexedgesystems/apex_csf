#ifndef APEX_SYSTEM_CORE_INTERFACE_MESSAGE_BUFFER_HPP
#define APEX_SYSTEM_CORE_INTERFACE_MESSAGE_BUFFER_HPP
/**
 * @file MessageBuffer.hpp
 * @brief Zero-copy message buffer for internal bus messaging.
 *
 * Design:
 *   - Protocol-agnostic internal bus message format
 *   - Stores payload only; routing metadata in struct fields
 *   - APROTO encoding/decoding happens at the external boundary (Interface)
 *   - Allocated from BufferPool, passed as pointers (zero-copy queues)
 *   - Ownership transfer pattern: allocate -> queue -> process -> release
 *
 * RT-Safety:
 *   - Allocation from pool is RT-safe (lock-free)
 *   - Buffer access is RT-safe (no locks, single-owner)
 *   - Release to pool is RT-safe (lock-free)
 */

#include <cstddef>
#include <cstdint>

#include <atomic>

namespace system_core {
namespace interface {

/* ----------------------------- MessageBuffer ----------------------------- */

/**
 * @struct MessageBuffer
 * @brief Protocol-agnostic buffer for internal bus messages.
 *
 * Stores payload data only. Routing metadata (fullUid, opcode, sequence)
 * lives in struct fields, not serialized into data[]. APROTO or other
 * protocol encoding is applied at the external boundary by the Interface.
 *
 * Lifecycle:
 *   1. Allocate from BufferPool (acquire)
 *   2. Write payload into data[], set metadata fields
 *   3. Pass ownership via pointer (queue push)
 *   4. Receiver reads metadata + payload directly
 *   5. Receiver releases back to pool
 *
 * Ownership Rules:
 *   - At any time, exactly ONE owner (pool, queue, or component)
 *   - Owner is responsible for release (except during transfer)
 *   - Never access buffer after releasing
 *
 * Safety:
 *   - Always check nullptr before dereferencing
 *   - Never write past capacity
 *   - Always release when done (or leak!)
 *
 * @note NOT RT-safe: Allocation (pool creates this struct)
 * @note RT-safe: All operations after allocation
 */
struct MessageBuffer {
  /* ----------------------------- Data Members ----------------------------- */

  std::uint8_t* data{nullptr}; ///< Allocated buffer (actual message data).
  std::size_t capacity{0};     ///< Buffer size (bytes).
  std::size_t length{0};       ///< Actual message length (bytes).

  /* ----------------------------- Bus Metadata ----------------------------- */

  std::uint32_t fullUid{0};   ///< Component fullUid (source or target).
  std::uint16_t opcode{0};    ///< Operation code.
  std::uint16_t sequence{0};  ///< Sequence number (for ACK correlation).
  bool internalOrigin{false}; ///< true = internal message, false = external.

  /* ----------------------------- Reference Counting ----------------------------- */

  std::atomic<std::uint32_t> refCount{1}; ///< Reference count for multicast.

  /* ----------------------------- Debug/Safety ----------------------------- */

#ifndef NDEBUG
  std::uint32_t magic{0xDEADBEEF};   ///< Magic number (detect corruption).
  bool isAcquired{false};            ///< Debug: Track acquire/release state.
  std::uint64_t acquireTimestamp{0}; ///< Debug: When buffer was acquired.
#endif

  /* ----------------------------- Methods ----------------------------- */

  /**
   * @brief Check if buffer is valid.
   * @return True if buffer has allocated data.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] bool isValid() const noexcept {
#ifndef NDEBUG
    // In debug mode, also check magic number
    return data != nullptr && capacity > 0 && magic == 0xDEADBEEF;
#else
    return data != nullptr && capacity > 0;
#endif
  }

  /**
   * @brief Clear metadata (does not deallocate buffer).
   * @note RT-safe: No allocation.
   */
  void clear() noexcept {
    length = 0;
    fullUid = 0;
    opcode = 0;
    sequence = 0;
    internalOrigin = false;
    refCount.store(1, std::memory_order_relaxed); // Reset for next use.
  }

  /**
   * @brief Check if buffer has enough capacity for a message.
   * @param size Required size in bytes.
   * @return True if capacity >= size.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] bool canFit(std::size_t size) const noexcept { return capacity >= size; }

  /**
   * @brief Increment reference count.
   * @note RT-safe: Atomic increment.
   */
  void addRef() noexcept { refCount.fetch_add(1, std::memory_order_relaxed); }

  /**
   * @brief Decrement reference count.
   * @return true if this was the last reference (caller should return to pool).
   * @note RT-safe: Atomic decrement.
   */
  [[nodiscard]] bool decRef() noexcept {
    return refCount.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }

  /**
   * @brief Set reference count for multicast.
   * @param count Number of recipients.
   * @note RT-safe: Atomic store.
   */
  void setRefCount(std::uint32_t count) noexcept {
    refCount.store(count, std::memory_order_release);
  }

  /**
   * @brief Get current reference count.
   * @return Current refcount value.
   * @note RT-safe: Atomic load.
   */
  [[nodiscard]] std::uint32_t getRefCount() const noexcept {
    return refCount.load(std::memory_order_acquire);
  }
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_MESSAGE_BUFFER_HPP
