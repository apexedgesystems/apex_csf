/**
 * @file RingBuffer.tpp
 * @brief Template implementation for RingBuffer<T>.
 *
 * Classic ring buffer with power-of-two capacity for fast index wrapping.
 * No atomics - maximum single-threaded performance.
 *
 * Note: Uses one slot as sentinel, so actual capacity is (power-of-two - 1).
 */

#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- RingBuffer Methods ----------------------------- */

// One slot is the sentinel that tells full from empty, so the ring has one more slot than the
// elements it must hold.
template <class T>
RingBuffer<T>::RingBuffer(std::size_t capacity) noexcept
    : mask_(nextPowerOfTwo((capacity < 2 ? 2 : capacity) + 1) - 1), buffer_(mask_ + 1) {}

template <class T> template <class U> bool RingBuffer<T>::push(U&& value) noexcept {
  const std::size_t NEXT = (head_ + 1) & mask_;

  // Full when next write position equals tail.
  if (NEXT == tail_) {
    return false;
  }

  buffer_[head_] = std::forward<U>(value);
  head_ = NEXT;
  return true;
}

template <class T> bool RingBuffer<T>::tryPop(T& out) noexcept {
  // Empty when head equals tail.
  if (head_ == tail_) {
    return false;
  }

  out = std::move(buffer_[tail_]);
  tail_ = (tail_ + 1) & mask_;
  return true;
}

template <class T> bool RingBuffer<T>::tryPeek(T& out) const noexcept {
  // Empty when head equals tail.
  if (head_ == tail_) {
    return false;
  }

  out = buffer_[tail_];
  return true;
}

} // namespace concurrency
} // namespace apex
