#ifndef APEX_UTILITIES_CONCURRENCY_RING_INDEX_HPP
#define APEX_UTILITIES_CONCURRENCY_RING_INDEX_HPP
/**
 * @file RingIndex.hpp
 * @brief Slot-count and index arithmetic the ring queues share.
 *
 * The queues size their rings to a power of two so an index is reduced with
 * a mask. They keep a head and a tail index that only ever increase and wrap
 * at the width of std::size_t; their difference is meaningful only in that
 * modular arithmetic, and comparing the two values numerically is wrong on
 * either side of a wrap.
 */

#include <cstddef>

namespace apex {
namespace concurrency {

/* ----------------------------- Slot Counts ----------------------------- */

/**
 * @brief Smallest power of two that is not below `value`.
 * @param value A count from 1 to 2^63.
 * @note RT-safe: bit operations.
 */
[[nodiscard]] constexpr std::size_t nextPowerOfTwo(std::size_t value) noexcept {
  --value;
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  value |= value >> 32;
  return value + 1;
}

/**
 * @brief Slots in a ring that must hold at least `requested` elements: the
 *        next power of two, and never fewer than two.
 * @note RT-safe: bit operations.
 */
[[nodiscard]] constexpr std::size_t ringSlots(std::size_t requested) noexcept {
  return nextPowerOfTwo(requested < 2 ? 2 : requested);
}

/* ----------------------------- ringOccupancy ----------------------------- */

/**
 * @brief Bounded distance from a read of the tail cursor to a read of the
 *        head cursor of a ring queue.
 * @param head A value read from the cursor that producers advance.
 * @param tail A value read from the cursor that consumers advance.
 * @param capacity The queue's capacity.
 * @return The distance from tail to head in the cursors' modular arithmetic
 *         when it is within capacity, zero otherwise. Never above capacity.
 *
 * The arithmetic holds on either side of an index wrap. What the distance
 * counts depends on when a queue advances its cursors. SPSCQueue advances
 * the head after the element is stored, so two cursors read at one instant
 * give its element count. LockFreeQueue advances a cursor to claim a cell
 * before the element is stored or taken, so its distance includes pushes
 * and pops in flight: it is the element count when the queue is quiescent,
 * and a distance above zero does not promise that a pop succeeds. A reader
 * that takes the two cursors at different instants can pair a head with a
 * later tail; that distance exceeds capacity and the result is zero.
 *
 * @note RT-safe: one subtraction and one comparison.
 */
[[nodiscard]] constexpr std::size_t ringOccupancy(std::size_t head, std::size_t tail,
                                                  std::size_t capacity) noexcept {
  const std::size_t DISTANCE = head - tail;
  return (DISTANCE <= capacity) ? DISTANCE : 0;
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_RING_INDEX_HPP
