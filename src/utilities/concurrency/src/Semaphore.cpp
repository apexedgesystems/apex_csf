/**
 * @file Semaphore.cpp
 * @brief Implementation of counting semaphore.
 */

#include "src/utilities/concurrency/inc/Semaphore.hpp"

namespace apex {
namespace concurrency {

/* ----------------------------- Semaphore Methods ----------------------------- */

Semaphore::Semaphore(std::size_t initial) noexcept : count_(static_cast<std::uint32_t>(initial)) {}

void Semaphore::acquire() {
  for (;;) {
    // Fast path: decrement if a permit is available.
    std::uint32_t current = count_.load(std::memory_order_relaxed);
    while (current > 0) {
      if (count_.compare_exchange_weak(current, current - 1, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        return;
      }
      // current updated by CAS failure, retry the fast path.
    }

    // Slow path: no permit. Announce as a waiter, then re-check count_ before
    // parking. The seq_cst waiters_ store + count_ load pair with release()'s
    // seq_cst count_ add + waiters_ load (a Dekker handshake): if release()
    // observes no waiter and skips its notify, this load observes the new
    // permit and we skip parking -- so the wakeup is never lost. atomic::wait
    // also re-checks the value, covering the narrow window after this load.
    waiters_.fetch_add(1, std::memory_order_seq_cst);
    if (count_.load(std::memory_order_seq_cst) == 0) {
      count_.wait(0U, std::memory_order_relaxed);
    }
    waiters_.fetch_sub(1, std::memory_order_relaxed);
  }
}

bool Semaphore::tryAcquire() {
  // Lock-free CAS loop.
  std::uint32_t current = count_.load(std::memory_order_relaxed);
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
  count_.fetch_add(static_cast<std::uint32_t>(count), std::memory_order_seq_cst);

  // Only touch the futex when a thread is actually parked. The common hot
  // path (busy pool, no waiters) is a single atomic add plus a relaxed-cost
  // load of waiters_ -- no notify syscall and no waiter-pool lookup, so it
  // stays lock-free and RT-safe. The seq_cst above pairs with acquire()'s
  // waiter registration so the wake is never lost.
  if (waiters_.load(std::memory_order_seq_cst) != 0) {
    if (count == 1) {
      count_.notify_one();
    } else {
      count_.notify_all();
    }
  }
}

std::size_t Semaphore::count() const { return count_.load(std::memory_order_relaxed); }

} // namespace concurrency
} // namespace apex
