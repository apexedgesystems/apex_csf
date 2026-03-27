#ifndef APEX_UTILITIES_CONCURRENCY_SPSC_QUEUE_HPP
#define APEX_UTILITIES_CONCURRENCY_SPSC_QUEUE_HPP
/**
 * @file SPSCQueue.hpp
 * @brief Lock-free single-producer single-consumer bounded queue.
 *
 * Design goals:
 *  - Single producer, single consumer (no contention).
 *  - Fixed capacity, non-blocking tryPush/tryPop API.
 *  - No heap allocation after construction.
 *  - Minimal cache-line bouncing via separated head/tail.
 *
 * Requirements:
 *  - Capacity >= 2 (rounded up to power-of-two internally).
 *  - T must be move-constructible and move-assignable.
 *  - Only ONE thread may call tryPush, only ONE thread may call tryPop.
 */

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace apex {
namespace concurrency {

/* ----------------------------- SPSCQueue ----------------------------- */

/**
 * @class SPSCQueue
 * @tparam T Value type stored in the queue.
 *
 * Uses a classic ring buffer with separated head (producer) and tail (consumer)
 * pointers. Each pointer is cache-line aligned to prevent false sharing.
 *
 * @note Thread safety: Exactly one producer thread and one consumer thread.
 */
template <class T> class SPSCQueue {
  static_assert(std::is_move_constructible_v<T>, "T must be move-constructible");
  static_assert(std::is_move_assignable_v<T>, "T must be move-assignable");

public:
  /**
   * @brief Construct a queue with fixed capacity.
   * @param capacity Minimum queue size (rounded up to power-of-two, minimum 2).
   * @note NOT RT-safe: Allocates buffer storage.
   */
  explicit SPSCQueue(std::size_t capacity) noexcept;

  /**
   * @brief Destructor releases internal storage.
   * @note NOT RT-safe: Deallocates buffer.
   */
  ~SPSCQueue();

  // Non-copyable / non-movable (owns raw storage).
  SPSCQueue(const SPSCQueue&) = delete;
  SPSCQueue& operator=(const SPSCQueue&) = delete;
  SPSCQueue(SPSCQueue&&) = delete;
  SPSCQueue& operator=(SPSCQueue&&) = delete;

  /**
   * @brief Try to push a copy of value (producer only).
   * @param value Value to copy into the queue.
   * @return True if pushed, false if queue is full.
   * @note RT-safe: No locks, single atomic load/store.
   */
  [[nodiscard]] bool tryPush(const T& value) noexcept;

  /**
   * @brief Try to push a moved value (producer only).
   * @param value Value to move into the queue.
   * @return True if pushed, false if queue is full.
   * @note RT-safe: No locks, single atomic load/store.
   */
  [[nodiscard]] bool tryPush(T&& value) noexcept;

  /**
   * @brief Try to pop a value from the queue (consumer only).
   * @param out Output parameter receiving the popped value.
   * @return True if popped, false if queue is empty.
   * @note RT-safe: No locks, single atomic load/store.
   */
  [[nodiscard]] bool tryPop(T& out) noexcept;

  /**
   * @brief Check if queue is empty (consumer only, approximate).
   * @return True if queue appears empty.
   * @note RT-safe: Single atomic load.
   */
  [[nodiscard]] bool empty() const noexcept;

  /**
   * @brief Get the fixed capacity of the queue.
   * @return Capacity in elements (power-of-two).
   * @note RT-safe: Returns stored value.
   */
  [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }

  /**
   * @brief Get approximate number of elements in queue.
   * @return Approximate element count (may be stale).
   * @note RT-safe: Two atomic loads, result may be approximate.
   */
  [[nodiscard]] std::size_t sizeApprox() const noexcept;

private:
  static constexpr std::size_t CACHE_LINE = 64;

  /**
   * @brief Round up to next power of two.
   */
  static constexpr std::size_t nextPow2(std::size_t v) noexcept {
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
  }

  std::size_t mask_{0}; ///< capacity - 1 for fast modulo.
  T* buffer_{nullptr};  ///< Ring storage.

  // Separated to different cache lines to prevent false sharing.
  alignas(CACHE_LINE) std::atomic<std::size_t> head_{0}; ///< Producer write position.
  alignas(CACHE_LINE) std::atomic<std::size_t> tail_{0}; ///< Consumer read position.
};

} // namespace concurrency
} // namespace apex

#include "src/utilities/concurrency/src/SPSCQueue.tpp"

#endif // APEX_UTILITIES_CONCURRENCY_SPSC_QUEUE_HPP
