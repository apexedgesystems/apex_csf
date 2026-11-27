#ifndef APEX_UTILITIES_CONCURRENCY_RING_BUFFER_HPP
#define APEX_UTILITIES_CONCURRENCY_RING_BUFFER_HPP
/**
 * @file RingBuffer.hpp
 * @brief High-performance single-threaded bounded queue.
 *
 * Design goals:
 *  - Maximum throughput for single-threaded use (zero atomics).
 *  - Fixed capacity, non-blocking tryPush/tryPop API.
 *  - No heap allocation after construction.
 *  - API compatible with SPSCQueue and LockFreeQueue.
 *
 * Requirements:
 *  - Capacity >= 2 (rounded up to power-of-two internally).
 *  - T must be move-constructible and move-assignable.
 *  - NOT thread-safe: Use SPSCQueue or LockFreeQueue for multi-threaded access.
 *
 * Performance:
 *  - ~100M+ ops/s (no synchronization overhead).
 *  - Use for processing pipelines, event buffers, batch operations.
 */

#include "src/utilities/concurrency/inc/RingIndex.hpp"
#include "src/utilities/concurrency/inc/RingStorage.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- RingBuffer ----------------------------- */

/**
 * @class RingBuffer
 * @tparam T Value type stored in the buffer.
 *
 * Classic ring buffer with head (write) and tail (read) pointers.
 * No atomic operations - maximum single-threaded performance.
 *
 * The two cursors lead the object, which is aligned to their combined size,
 * so one cache line holds both wherever the object is placed: a push and pop
 * pair that finds them on two lines is slower.
 *
 * @note NOT thread-safe: Single-threaded use only.
 */
template <class T> class alignas(2 * sizeof(std::size_t)) RingBuffer {
  static_assert(std::is_move_constructible_v<T>, "T must be move-constructible");
  static_assert(std::is_move_assignable_v<T>, "T must be move-assignable");

public:
  /**
   * @brief Construct a buffer with fixed capacity.
   * @param capacity Minimum buffer size (rounded up to power-of-two, minimum 2).
   * @note NOT RT-safe: Allocates buffer storage.
   */
  explicit RingBuffer(std::size_t capacity) noexcept;

  /**
   * @brief Destructor releases internal storage.
   * @note NOT RT-safe: Deallocates buffer.
   */
  ~RingBuffer() = default;

  // Non-copyable / non-movable (owns raw storage).
  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;
  RingBuffer(RingBuffer&&) = delete;
  RingBuffer& operator=(RingBuffer&&) = delete;

  /**
   * @brief Try to push a copy of value.
   * @param value Value to copy into the buffer.
   * @return True if pushed, false if buffer is full.
   * @note RT-safe: No allocations, O(1).
   */
  [[nodiscard]] bool tryPush(const T& value) noexcept { return push(value); }

  /**
   * @brief Try to push a moved value.
   * @param value Value to move into the buffer.
   * @return True if pushed, false if buffer is full.
   * @note RT-safe: No allocations, O(1).
   */
  [[nodiscard]] bool tryPush(T&& value) noexcept { return push(std::move(value)); }

  /**
   * @brief Try to pop a value from the buffer.
   * @param out Output parameter receiving the popped value.
   * @return True if popped, false if buffer is empty.
   * @note RT-safe: No allocations, O(1).
   */
  [[nodiscard]] bool tryPop(T& out) noexcept;

  /**
   * @brief Peek at front element without removing.
   * @param out Output parameter receiving a copy of front value.
   * @return True if peeked, false if buffer is empty.
   * @note RT-safe: No allocations, O(1).
   */
  [[nodiscard]] bool tryPeek(T& out) const noexcept;

  /**
   * @brief Check if buffer is empty.
   * @return True if buffer is empty.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool empty() const noexcept { return head_ == tail_; }

  /**
   * @brief Check if buffer is full.
   * @return True if buffer is full.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool full() const noexcept { return ((head_ + 1) & mask_) == tail_; }

  /**
   * @brief Get current number of elements.
   * @return Element count.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t size() const noexcept { return (head_ - tail_) & mask_; }

  /**
   * @brief Get the fixed capacity of the buffer.
   * @return Capacity in elements (power-of-two minus 1 for sentinel).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t capacity() const noexcept { return mask_; }

  /**
   * @brief Clear all elements from buffer.
   * @note RT-safe: O(1) (just resets pointers).
   */
  void clear() noexcept {
    head_ = 0;
    tail_ = 0;
  }

private:
  /// The one push: assigns `value` into the slot as the value category it arrived with.
  template <class U> [[nodiscard]] bool push(U&& value) noexcept;

  std::size_t head_{0};   ///< Write position (NOT atomic).
  std::size_t tail_{0};   ///< Read position (NOT atomic).
  std::size_t mask_;      ///< Slot count - 1 for fast modulo.
  RingStorage<T> buffer_; ///< Ring storage.
};

} // namespace concurrency
} // namespace apex

#include "src/utilities/concurrency/src/RingBuffer.tpp"

#endif // APEX_UTILITIES_CONCURRENCY_RING_BUFFER_HPP
