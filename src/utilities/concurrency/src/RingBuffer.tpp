/**
 * @file RingBuffer.tpp
 * @brief Template implementation for RingBuffer<T>.
 *
 * Classic ring buffer with power-of-two capacity for fast index wrapping.
 * No atomics - maximum single-threaded performance.
 *
 * Note: Uses one slot as sentinel, so actual capacity is (power-of-two - 1).
 */

#include <memory>
#include <new>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- RingBuffer Methods ----------------------------- */

template <class T>
RingBuffer<T>::RingBuffer(std::size_t capacity) noexcept
    : mask_(0), buffer_(nullptr), head_(0), tail_(0) {
  // Ensure minimum capacity of 2 and round up to power of two.
  // Add 1 because we use one slot as sentinel.
  const std::size_t CAP = nextPow2((capacity < 2 ? 2 : capacity) + 1);
  mask_ = CAP - 1;

  // Allocate and default-construct elements.
  buffer_ = static_cast<T*>(::operator new[](CAP * sizeof(T)));
  for (std::size_t i = 0; i < CAP; ++i) {
    new (&buffer_[i]) T();
  }
}

template <class T> RingBuffer<T>::~RingBuffer() {
  if (buffer_) {
    const std::size_t CAP = mask_ + 1;
    for (std::size_t i = CAP; i-- > 0;) {
      std::destroy_at(&buffer_[i]);
    }
    ::operator delete[](buffer_);
    buffer_ = nullptr;
  }
}

template <class T> bool RingBuffer<T>::tryPush(const T& value) noexcept {
  const std::size_t NEXT = (head_ + 1) & mask_;

  // Full when next write position equals tail.
  if (NEXT == tail_) {
    return false;
  }

  buffer_[head_] = value;
  head_ = NEXT;
  return true;
}

template <class T> bool RingBuffer<T>::tryPush(T&& value) noexcept {
  const std::size_t NEXT = (head_ + 1) & mask_;

  // Full when next write position equals tail.
  if (NEXT == tail_) {
    return false;
  }

  buffer_[head_] = std::move(value);
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
