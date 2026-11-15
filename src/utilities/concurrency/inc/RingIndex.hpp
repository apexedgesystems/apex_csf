#ifndef APEX_UTILITIES_CONCURRENCY_RING_INDEX_HPP
#define APEX_UTILITIES_CONCURRENCY_RING_INDEX_HPP
/**
 * @file RingIndex.hpp
 * @brief Arithmetic on the free-running indices of the ring queues.
 *
 * The queues keep a head and a tail index that only ever increase and wrap at
 * the width of std::size_t. Their difference is meaningful only in that
 * modular arithmetic; comparing the two values numerically is wrong on either
 * side of a wrap.
 */

#include <cstddef>

namespace apex {
namespace concurrency {

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
