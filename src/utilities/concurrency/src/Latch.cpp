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
  std::lock_guard<std::mutex> lk(mutex_);
  std::size_t current = count_.load(std::memory_order_relaxed);
  if (current == 0) {
    return; // Already triggered
  }
  std::size_t newVal = (n >= current) ? 0 : current - n;
  // Release so a worker's writes before countDown happen-before a waiter's
  // reads after wait() returns.
  count_.store(newVal, std::memory_order_release);
  if (newVal == 0) {
    // Notify while holding the mutex. A waiter cannot re-acquire it and return
    // -- then destroy a stack-allocated latch -- until this call releases it,
    // so the final countDown never touches a latch the waiter has freed.
    cv_.notify_all();
  }
}

bool Latch::tryWait() const noexcept {
  // Fast path: pure atomic load - no mutex needed.
  return count_.load(std::memory_order_acquire) == 0;
}

void Latch::wait() {
  // Always take the mutex. A lock-free fast path could let a waiter observe
  // zero, return, and destroy the latch while the last countDown is still
  // notifying through cv_. Acquiring the mutex serializes the waiter's return
  // until countDown completes. wait() is a blocking, non-hot-path call.
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
  count_.store(newVal, std::memory_order_release);

  if (newVal == 0) {
    // Notify under the lock (see countDown) so a participant cannot return and
    // destroy the latch while another is still notifying.
    cv_.notify_all();
    return;
  }

  cv_.wait(lk, [this]() { return count_.load(std::memory_order_relaxed) == 0; });
}

std::size_t Latch::count() const noexcept { return count_.load(std::memory_order_relaxed); }

} // namespace concurrency
} // namespace apex
