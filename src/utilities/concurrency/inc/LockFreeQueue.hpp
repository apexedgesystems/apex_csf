#ifndef APEX_UTILITIES_CONCURRENCY_LOCK_FREE_QUEUE_HPP
#define APEX_UTILITIES_CONCURRENCY_LOCK_FREE_QUEUE_HPP
/**
 * @file LockFreeQueue.hpp
 * @brief Bounded lock-free MPMC queue using Vyukov's ring buffer algorithm.
 *
 * Design goals:
 *  - Multiple producers / multiple consumers (MPMC).
 *  - Fixed capacity, non-blocking tryPush/tryPop API.
 *  - No heap allocation after construction.
 *  - Wait-free progress for individual operations.
 *
 * Requirements:
 *  - Capacity >= 2 (rounded up to power-of-two internally for performance).
 *  - T must be move-constructible and move-assignable.
 */

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace apex {
namespace concurrency {

/* ----------------------------- LockFreeQueue ----------------------------- */

/**
 * @class LockFreeQueue
 * @tparam T Value type stored in the queue.
 *
 * Memory ordering follows Dmitry Vyukov's MPMC algorithm with per-cell
 * sequence counters gating producer/consumer progress.
 */
template <class T> class LockFreeQueue {
  static_assert(std::is_move_constructible_v<T>, "T must be move-constructible");
  static_assert(std::is_move_assignable_v<T>, "T must be move-assignable");

public:
  /**
   * @brief Construct a queue with fixed capacity.
   * @param capacity Minimum queue size (rounded up to power-of-two, minimum 2).
   * @note NOT RT-safe: Allocates buffer storage.
   */
  explicit LockFreeQueue(std::size_t capacity) noexcept;

  /**
   * @brief Destructor releases internal storage.
   * @note NOT RT-safe: Deallocates buffer.
   */
  ~LockFreeQueue();

  // Non-copyable / non-movable (owns raw storage).
  LockFreeQueue(const LockFreeQueue&) = delete;
  LockFreeQueue& operator=(const LockFreeQueue&) = delete;
  LockFreeQueue(LockFreeQueue&&) = delete;
  LockFreeQueue& operator=(LockFreeQueue&&) = delete;

  /**
   * @brief Try to push a copy of value.
   * @param value Value to copy into the queue.
   * @return True if pushed, false if queue is full.
   * @note RT-safe: Lock-free, bounded CAS loop.
   */
  [[nodiscard]] bool tryPush(const T& value) noexcept;

  /**
   * @brief Try to push a moved value.
   * @param value Value to move into the queue.
   * @return True if pushed, false if queue is full.
   * @note RT-safe: Lock-free, bounded CAS loop.
   */
  [[nodiscard]] bool tryPush(T&& value) noexcept;

  /**
   * @brief Try to pop a value from the queue.
   * @param out Output parameter receiving the popped value.
   * @return True if popped, false if queue is empty.
   * @note RT-safe: Lock-free, bounded CAS loop.
   */
  [[nodiscard]] bool tryPop(T& out) noexcept;

  /**
   * @brief Get the fixed capacity of the queue.
   * @return Capacity in elements (power-of-two).
   * @note RT-safe: Returns stored value.
   */
  [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }

  /**
   * @brief Get approximate number of elements in queue.
   * @return Approximate element count (may be stale due to concurrent access).
   * @note RT-safe: Two atomic loads.
   */
  [[nodiscard]] std::size_t sizeApprox() const noexcept {
    const std::size_t h = head_.load(std::memory_order_relaxed);
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    return (h >= t) ? (h - t) : 0;
  }

private:
  static constexpr std::size_t CACHE_LINE = 64;

  struct alignas(CACHE_LINE) Cell {
    std::atomic<std::size_t> sequence; ///< Per-slot sequence counter.
    T data;                            ///< Stored value.
    // Padding implicit via alignas - each Cell occupies full cache line.
  };

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

  std::size_t mask_{0};                          ///< capacity - 1 for fast modulo.
  Cell* buffer_{nullptr};                        ///< Ring storage (manual lifetime).
  alignas(64) std::atomic<std::size_t> head_{0}; ///< Producer cursor.
  alignas(64) std::atomic<std::size_t> tail_{0}; ///< Consumer cursor.
};

} // namespace concurrency
} // namespace apex

#include "src/utilities/concurrency/src/LockFreeQueue.tpp"

#endif // APEX_UTILITIES_CONCURRENCY_LOCK_FREE_QUEUE_HPP
