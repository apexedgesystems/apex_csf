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
#include <memory>
#include <new>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- LockFreeQueue Methods ----------------------------- */

template <class T>
LockFreeQueue<T>::LockFreeQueue(std::size_t capacity) noexcept
    : mask_(0), buffer_(nullptr), head_(0), tail_(0) {
  // Ensure minimum capacity of 2 and round up to power of two
  const std::size_t CAP = nextPow2(capacity < 2 ? 2 : capacity);
  mask_ = CAP - 1;

  // Allocate raw storage; construct elements manually for sequence init.
  buffer_ = static_cast<Cell*>(::operator new[](CAP * sizeof(Cell)));

  for (std::size_t i = 0; i < CAP; ++i) {
    new (&buffer_[i].sequence) std::atomic<std::size_t>(i);
    new (&buffer_[i].data) T();
  }
}

template <class T> LockFreeQueue<T>::~LockFreeQueue() {
  // Destroy elements in reverse order (order doesn't matter, but is conventional).
  if (buffer_) {
    const std::size_t CAP = mask_ + 1;
    for (std::size_t i = CAP; i-- > 0;) {
      std::destroy_at(&buffer_[i].data);
      std::destroy_at(&buffer_[i].sequence);
    }
    ::operator delete[](buffer_);
    buffer_ = nullptr;
  }
}

template <class T> bool LockFreeQueue<T>::tryPush(const T& value) noexcept {
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
  buffer_[pos & mask_].data = value;
  buffer_[pos & mask_].sequence.store(pos + 1, std::memory_order_release);
  return true;
}

template <class T> bool LockFreeQueue<T>::tryPush(T&& value) noexcept {
  Cell* cell;
  std::size_t pos = head_.load(std::memory_order_relaxed);
  for (;;) {
    cell = &buffer_[pos & mask_];
    const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
    const std::intptr_t dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
    if (dif == 0) {
      if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
        break;
    } else if (dif < 0) {
      return false; // full
    } else {
      pos = head_.load(std::memory_order_relaxed);
    }
  }
  buffer_[pos & mask_].data = std::move(value);
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
