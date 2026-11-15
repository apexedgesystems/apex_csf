/**
 * @file SPSCQueue.tpp
 * @brief Template implementation for SPSCQueue<T>.
 *
 * Classic ring buffer with power-of-two capacity for fast index wrapping.
 * Head is written by producer, tail by consumer. Each observes the other
 * with acquire semantics to determine available space/data.
 */

#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- SPSCQueue Methods ----------------------------- */

template <class T>
SPSCQueue<T>::SPSCQueue(std::size_t capacity, std::size_t firstIndex) noexcept
    : mask_(ringSlots(capacity) - 1), buffer_(mask_ + 1), head_(firstIndex), tail_(firstIndex) {}

template <class T> template <class U> bool SPSCQueue<T>::push(U&& value) noexcept {
  const std::size_t HEAD = head_.load(std::memory_order_relaxed);
  const std::size_t TAIL = tail_.load(std::memory_order_acquire);

  // Full when head - tail == capacity (all slots used).
  if (HEAD - TAIL > mask_) {
    return false;
  }

  buffer_[HEAD & mask_] = std::forward<U>(value);
  head_.store(HEAD + 1, std::memory_order_release);
  return true;
}

template <class T> bool SPSCQueue<T>::tryPop(T& out) noexcept {
  const std::size_t TAIL = tail_.load(std::memory_order_relaxed);

  // Check if queue is empty: tail equals head.
  if (TAIL == head_.load(std::memory_order_acquire)) {
    return false; // Empty
  }

  out = std::move(buffer_[TAIL & mask_]);
  tail_.store(TAIL + 1, std::memory_order_release);
  return true;
}

template <class T> bool SPSCQueue<T>::empty() const noexcept {
  return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
}

template <class T> std::size_t SPSCQueue<T>::sizeApprox() const noexcept {
  const std::size_t HEAD = head_.load(std::memory_order_acquire);
  const std::size_t TAIL = tail_.load(std::memory_order_acquire);

  // The two loads are not atomic together: the consumer can move the tail past the head read
  // above. ringOccupancy() reports zero for such a pair.
  return ringOccupancy(HEAD, TAIL, mask_ + 1);
}

} // namespace concurrency
} // namespace apex
