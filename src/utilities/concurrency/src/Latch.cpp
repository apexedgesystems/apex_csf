/**
 * @file Latch.cpp
 * @brief Implementation of one-shot countdown latch.
 */

#include "src/utilities/concurrency/inc/Latch.hpp"

namespace apex {
namespace concurrency {

/* ----------------------------- Latch Methods ----------------------------- */

Latch::Latch(std::size_t count) noexcept : count_(count) {}

void Latch::countDown(std::size_t n) {
  bool shouldNotify = false;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    std::size_t current = count_.load(std::memory_order_relaxed);
    if (current == 0) {
      return; // Already triggered
    }
    std::size_t newVal = (n >= current) ? 0 : current - n;
    count_.store(newVal, std::memory_order_relaxed);
    shouldNotify = (newVal == 0);
  }
  if (shouldNotify) {
    cv_.notify_all();
  }
}

bool Latch::tryWait() const noexcept {
  // Fast path: pure atomic load - no mutex needed.
  return count_.load(std::memory_order_acquire) == 0;
}

void Latch::wait() {
  // Fast path: already triggered.
  if (count_.load(std::memory_order_acquire) == 0) {
    return;
  }

  // Slow path: wait on CV.
  std::unique_lock<std::mutex> lk(mutex_);
  cv_.wait(lk, [this]() { return count_.load(std::memory_order_relaxed) == 0; });
}

void Latch::arriveAndWait(std::size_t n) {
  std::unique_lock<std::mutex> lk(mutex_);
  std::size_t current = count_.load(std::memory_order_relaxed);
  if (current == 0) {
    return; // Already triggered
  }
  std::size_t newVal = (n >= current) ? 0 : current - n;
  count_.store(newVal, std::memory_order_relaxed);

  if (newVal == 0) {
    lk.unlock();
    cv_.notify_all();
    return;
  }

  cv_.wait(lk, [this]() { return count_.load(std::memory_order_relaxed) == 0; });
}

std::size_t Latch::count() const noexcept { return count_.load(std::memory_order_relaxed); }

} // namespace concurrency
} // namespace apex
