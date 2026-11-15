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
    cell = &buffer_[pos & mask_]; // Bitwise AND instead of modulo
    const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
    const std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
    if (dif == 0) {
      if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
        break; // this producer owns the cell
    } else if (dif < 0) {
      return false; // full
    } else {
      pos = head_.load(std::memory_order_relaxed); // retry with updated head
    }
  }
  // Publish data then advance sequence so consumers can observe it.
  buffer_[pos & mask_].data = std::forward<U>(value);
  buffer_[pos & mask_].sequence.store(pos + 1, std::memory_order_release);
  return true;
}

template <class T> bool LockFreeQueue<T>::tryPop(T& out) noexcept {
  Cell* cell;
  std::size_t pos = tail_.load(std::memory_order_relaxed);
  for (;;) {
    cell = &buffer_[pos & mask_];
    const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
    const std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);
    if (dif == 0) {
      if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
        break; // this consumer owns the cell
    } else if (dif < 0) {
      return false; // empty
    } else {
      pos = tail_.load(std::memory_order_relaxed); // retry with updated tail
    }
  }
  out = std::move(buffer_[pos & mask_].data);
  buffer_[pos & mask_].sequence.store(pos + (mask_ + 1), std::memory_order_release);
  return true;
}

} // namespace concurrency
} // namespace apex
