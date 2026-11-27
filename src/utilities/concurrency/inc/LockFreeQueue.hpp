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
 *  - Calls never wait: tryPush and tryPop return without blocking on another
 *    thread. One call has no retry bound while others compete for the same
 *    cursor.
 *
 * Held cells:
 *  A call claims its cell by advancing a cursor and releases it by storing the
 *  cell's sequence number, with the element's assignment in between. A cell
 *  claimed and not yet released is held. While a consumer holds a cell,
 *  tryPush reports full once the producer cursor has come round to that cell,
 *  however few elements are unread. While a producer holds a cell, tryPop
 *  reports empty at that cell, whatever was published behind it. Both last
 *  until the holding thread runs again, so a thread descheduled inside a call
 *  can keep every other thread from passing its cell. The calls never wait,
 *  but the ring is not lock-free in the strict sense: no push or pop is
 *  certain to succeed while one thread is stopped inside a call. A caller
 *  that must not lose an element on a false return tries again; even a ring
 *  whose users never offer more than capacity() elements can refuse a push
 *  this way.
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
   * @return True if pushed. False if the queue is full, or if a consumer still holds the cell at
   *         the producer cursor (see "Held cells" above); value is then untouched.
   * @note RT-DEPENDS: lock-free, with no allocation, lock or syscall. The
   *       compare-and-swap loop has no per-caller retry bound: a competing
   *       producer, or a spurious failure of the weak exchange, sends the
   *       call round again. Also depends on T's copy assignment, which runs
   *       inside the call, and on a lock-free std::atomic<std::size_t>.
   */
  [[nodiscard]] bool tryPush(const T& value) noexcept { return push(value); }

  /**
   * @brief Try to push a moved value.
   * @param value Value to move into the queue.
   * @return True if pushed. False as for tryPush(const T&); value is then not moved from.
   * @note RT-DEPENDS: as tryPush(const T&), with T's move assignment.
   */
  [[nodiscard]] bool tryPush(T&& value) noexcept { return push(std::move(value)); }

  /**
   * @brief Try to pop a value from the queue.
   * @param out Output parameter receiving the popped value.
   * @return True if popped. False if the queue is empty, or if a producer still holds the cell at
   *         the consumer cursor (see "Held cells" above); out is then untouched.
   * @note RT-DEPENDS: lock-free, with no allocation, lock or syscall. The
   *       compare-and-swap loop has no per-caller retry bound: a competing
   *       consumer, or a spurious failure of the weak exchange, sends the
   *       call round again. Also depends on T's move assignment, which runs
   *       inside the call, and on a lock-free std::atomic<std::size_t>.
   */
  [[nodiscard]] bool tryPop(T& out) noexcept;

  /**
   * @brief Get the fixed capacity of the queue.
   * @return Capacity in elements (power-of-two).
   * @note RT-safe: Returns stored value.
   */
  [[nodiscard]] std::size_t capacity() const noexcept { return mask_ + 1; }

  /**
   * @brief Get an estimate of the queue's occupancy.
   * @return The distance between the consumer and producer cursors, never more than capacity().
   *         It is the element count when no push or pop is in progress.
   * @note RT-safe: Two atomic loads.
   * @note A cursor moves when an operation claims its cell, before the element is stored or
   *       taken, so the estimate counts operations in flight: a result above zero does not
   *       promise that tryPop() succeeds. The two cursor reads are also not one instant: a
   *       consumer can pass the head already read, and that pair gives zero.
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
