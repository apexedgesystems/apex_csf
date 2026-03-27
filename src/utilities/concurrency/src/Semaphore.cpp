/**
 * @file Semaphore.cpp
 * @brief Implementation of counting semaphore.
 */

#include "src/utilities/concurrency/inc/Semaphore.hpp"

namespace apex {
namespace concurrency {

/* ----------------------------- Semaphore Methods ----------------------------- */

Semaphore::Semaphore(std::size_t initial) noexcept : count_(initial) {}

void Semaphore::acquire() {
  // Fast path: try atomic decrement first.
  std::size_t current = count_.load(std::memory_order_relaxed);
  while (current > 0) {
    if (count_.compare_exchange_weak(current, current - 1, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return; // Acquired via fast path
    }
    // current is updated by CAS failure, retry
  }

  // Slow path: wait on CV.
  std::unique_lock<std::mutex> lk(mutex_);
  cv_.wait(lk, [this]() { return count_.load(std::memory_order_relaxed) > 0; });

  // Decrement under lock (another thread may have acquired between CV wake and here).
  current = count_.load(std::memory_order_relaxed);
  while (current > 0) {
    if (count_.compare_exchange_weak(current, current - 1, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return;
    }
  }
  // Spurious wakeup or race - re-wait (shouldn't happen often).
  lk.unlock();
  acquire(); // Recursive retry
}

bool Semaphore::tryAcquire() {
  // Lock-free CAS loop.
  std::size_t current = count_.load(std::memory_order_relaxed);
  while (current > 0) {
    if (count_.compare_exchange_weak(current, current - 1, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return true;
    }
    // current is updated by CAS failure
  }
  return false;
}

void Semaphore::release(std::size_t count) {
  count_.fetch_add(count, std::memory_order_release);

  // Wake waiters.
  if (count == 1) {
    cv_.notify_one();
  } else {
    cv_.notify_all();
  }
}

std::size_t Semaphore::count() const { return count_.load(std::memory_order_relaxed); }

} // namespace concurrency
} // namespace apex
