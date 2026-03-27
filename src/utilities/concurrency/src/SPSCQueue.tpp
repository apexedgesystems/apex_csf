/**
 * @file SPSCQueue.tpp
 * @brief Template implementation for SPSCQueue<T>.
 *
 * Classic ring buffer with power-of-two capacity for fast index wrapping.
 * Head is written by producer, tail by consumer. Each observes the other
 * with acquire semantics to determine available space/data.
 */

#include <memory>
#include <new>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- SPSCQueue Methods ----------------------------- */

template <class T>
SPSCQueue<T>::SPSCQueue(std::size_t capacity) noexcept
    : mask_(0), buffer_(nullptr), head_(0), tail_(0) {
  // Ensure minimum capacity of 2 and round up to power of two.
  const std::size_t CAP = nextPow2(capacity < 2 ? 2 : capacity);
  mask_ = CAP - 1;

  // Allocate and default-construct elements.
  buffer_ = static_cast<T*>(::operator new[](CAP * sizeof(T)));
  for (std::size_t i = 0; i < CAP; ++i) {
    new (&buffer_[i]) T();
  }
}

template <class T> SPSCQueue<T>::~SPSCQueue() {
  if (buffer_) {
    const std::size_t CAP = mask_ + 1;
    for (std::size_t i = CAP; i-- > 0;) {
      std::destroy_at(&buffer_[i]);
    }
    ::operator delete[](buffer_);
    buffer_ = nullptr;
  }
}

template <class T> bool SPSCQueue<T>::tryPush(const T& value) noexcept {
  const std::size_t HEAD = head_.load(std::memory_order_relaxed);
  const std::size_t TAIL = tail_.load(std::memory_order_acquire);

  // Full when head - tail == capacity (all slots used).
  if (HEAD - TAIL > mask_) {
    return false;
  }

  buffer_[HEAD & mask_] = value;
  head_.store(HEAD + 1, std::memory_order_release);
  return true;
}

template <class T> bool SPSCQueue<T>::tryPush(T&& value) noexcept {
  const std::size_t HEAD = head_.load(std::memory_order_relaxed);
  const std::size_t TAIL = tail_.load(std::memory_order_acquire);

  // Full when head - tail == capacity (all slots used).
  if (HEAD - TAIL > mask_) {
    return false;
  }

  buffer_[HEAD & mask_] = std::move(value);
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
  return HEAD - TAIL;
}

} // namespace concurrency
} // namespace apex
