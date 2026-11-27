/**
 * @file LockFreeQueue.tpp
 * @brief Template implementation for LockFreeQueue<T>.
 *
 * Uses placement new for cells to initialize sequence counters as required
 * by Vyukov's algorithm. Destruction mirrors construction order.
 *
 * Optimization: Uses bitwise AND instead of modulo for index wrapping
 * (requires power-of-two capacity).
 */

#include <cstdint>
#include <new>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- LockFreeQueue Methods ----------------------------- */

// Each cell starts with the sequence number of the first cursor position that lands on it: the
// one value in [firstIndex, firstIndex + slots) whose low bits are the cell's index.
template <class T>
LockFreeQueue<T>::LockFreeQueue(std::size_t capacity, std::size_t firstIndex) noexcept
    : mask_(ringSlots(capacity) - 1),
      buffer_(mask_ + 1,
              [this, firstIndex](void* where, std::size_t i) {
                ::new (where) Cell(firstIndex + ((i - firstIndex) & mask_));
              }),
      head_(firstIndex), tail_(firstIndex) {}

template <class T> template <class U> bool LockFreeQueue<T>::push(U&& value) noexcept {
  Cell* cell;
  std::size_t pos = head_.load(std::memory_order_relaxed);
  for (;;) {
    cell = &buffer_[pos & mask_];
    const std::size_t SEQ = cell->sequence.load(std::memory_order_acquire);
    // The distance from this cursor position to the cell's sequence number, in the indices'
    // modular arithmetic: zero when the cell is free for this position, negative when the ring
    // is full, positive when another producer has taken the position.
    const auto AHEAD = static_cast<std::intptr_t>(SEQ - pos);
    if (AHEAD == 0) {
      if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
        break; // This producer owns the cell.
      }
    } else if (AHEAD < 0) {
      return false; // Full.
    } else {
      pos = head_.load(std::memory_order_relaxed);
    }
  }
  // Publish the data, then advance the sequence so consumers can observe it.
  cell->data = std::forward<U>(value);
  cell->sequence.store(pos + 1, std::memory_order_release);
  return true;
}

template <class T> bool LockFreeQueue<T>::tryPop(T& out) noexcept {
  Cell* cell;
  std::size_t pos = tail_.load(std::memory_order_relaxed);
  for (;;) {
    cell = &buffer_[pos & mask_];
    const std::size_t SEQ = cell->sequence.load(std::memory_order_acquire);
    // Zero when the cell holds the element for this position, negative when the ring is empty,
    // positive when another consumer has taken the position.
    const auto AHEAD = static_cast<std::intptr_t>(SEQ - (pos + 1));
    if (AHEAD == 0) {
      if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
        break; // This consumer owns the cell.
      }
    } else if (AHEAD < 0) {
      return false; // Empty.
    } else {
      pos = tail_.load(std::memory_order_relaxed);
    }
  }
  out = std::move(cell->data);
  cell->sequence.store(pos + (mask_ + 1), std::memory_order_release);
  return true;
}

} // namespace concurrency
} // namespace apex
