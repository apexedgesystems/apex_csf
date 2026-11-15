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
 * @brief Element count implied by one read of each index of a ring queue.
 * @param head A value read from the index that producers advance.
 * @param tail A value read from the index that consumers advance.
 * @param capacity The queue's capacity.
 * @return The distance from tail to head in the indices' modular arithmetic
 *         when it is within capacity, zero otherwise.
 *
 * For two values read at one instant the distance is the element count, on
 * either side of an index wrap. A reader that takes the two values at
 * different instants can pair a head with a later tail; the distance then
 * exceeds capacity, which no state of the queue produces, and the result is
 * zero. The result is never above capacity.
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
