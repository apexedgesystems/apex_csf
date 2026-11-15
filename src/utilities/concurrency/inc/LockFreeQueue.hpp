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

#include "src/utilities/concurrency/inc/Cache.hpp"
#include "src/utilities/concurrency/inc/RingIndex.hpp"
#include "src/utilities/concurrency/inc/RingStorage.hpp"

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

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
  explicit LockFreeQueue(std::size_t capacity) noexcept : LockFreeQueue(capacity, 0) {}

  /**
   * @brief Destructor releases internal storage.
   * @note NOT RT-safe: Deallocates buffer.
   */
  ~LockFreeQueue() = default;

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
  [[nodiscard]] bool tryPush(const T& value) noexcept { return push(value); }

  /**
   * @brief Try to push a moved value.
   * @param value Value to move into the queue.
   * @return True if pushed, false if queue is full.
   * @note RT-safe: Lock-free, bounded CAS loop.
   */
  [[nodiscard]] bool tryPush(T&& value) noexcept { return push(std::move(value)); }

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
   * @return Approximate element count (may be stale due to concurrent access), never more than
   *         capacity().
   * @note RT-safe: Two atomic loads.
   * @note The two index reads are not one instant: a consumer can pass the head already read,
   *       and the result is zero for a pair that no state of the queue could have produced.
   */
  [[nodiscard]] std::size_t sizeApprox() const noexcept {
    const std::size_t HEAD = head_.load(std::memory_order_relaxed);
    const std::size_t TAIL = tail_.load(std::memory_order_relaxed);
    return ringOccupancy(HEAD, TAIL, mask_ + 1);
  }

protected:
  /**
   * @brief Construct a queue whose two cursors both start at firstIndex.
   * @param capacity Minimum queue size (rounded up to power-of-two, minimum 2).
   * @param firstIndex Starting value of the producer and consumer cursors.
   *
   * The cursors and the per-cell sequence numbers run freely and wrap at the width of
   * std::size_t. A queue started just below that width reaches the wrap within a few operations,
   * which is how the index arithmetic is tested.
   * @note NOT RT-safe: Allocates buffer storage.
   */
  LockFreeQueue(std::size_t capacity, std::size_t firstIndex) noexcept;

private:
  /// One slot of the ring, a whole number of cache lines.
  struct alignas(cache::CACHE_LINE_SIZE) Cell {
    explicit Cell(std::size_t firstSequence) : sequence(firstSequence), data() {}

    std::atomic<std::size_t> sequence; ///< Per-slot sequence counter.
    T data;                            ///< Stored value.
  };

  /// The one push: assigns `value` into the claimed cell as the value category it arrived with.
  template <class U> [[nodiscard]] bool push(U&& value) noexcept;

  std::size_t mask_;                                              ///< capacity - 1 for fast modulo.
  RingStorage<Cell> buffer_;                                      ///< Ring storage.
  alignas(cache::CACHE_LINE_SIZE) std::atomic<std::size_t> head_; ///< Producer cursor.
  alignas(cache::CACHE_LINE_SIZE) std::atomic<std::size_t> tail_; ///< Consumer cursor.
};

} // namespace concurrency
} // namespace apex

#include "src/utilities/concurrency/src/LockFreeQueue.tpp"

#endif // APEX_UTILITIES_CONCURRENCY_LOCK_FREE_QUEUE_HPP
