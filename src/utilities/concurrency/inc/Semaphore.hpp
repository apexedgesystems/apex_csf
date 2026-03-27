#ifndef APEX_UTILITIES_CONCURRENCY_SEMAPHORE_HPP
#define APEX_UTILITIES_CONCURRENCY_SEMAPHORE_HPP
/**
 * @file Semaphore.hpp
 * @brief Counting semaphore for resource limiting and synchronization.
 *
 * Design goals:
 *  - C++17 compatible (std::counting_semaphore is C++20).
 *  - Blocking acquire with optional timeout.
 *  - Non-blocking tryAcquire for RT-safe paths.
 *  - Release can be called from any thread.
 *
 * Use cases:
 *  - Resource pool limiting (e.g., connection pool).
 *  - Producer-consumer throttling.
 *  - Task gating and synchronization.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace apex {
namespace concurrency {

/* ----------------------------- Semaphore ----------------------------- */

/**
 * @class Semaphore
 * @brief Counting semaphore for resource limiting.
 *
 * A semaphore maintains an internal count. acquire() blocks until count > 0,
 * then decrements. release() increments the count and wakes waiters.
 *
 * @note Thread-safe: All methods are safe to call from any thread.
 */
class Semaphore {
public:
  /**
   * @brief Construct a semaphore with initial count.
   * @param initial Initial count (default 0).
   * @note NOT RT-safe: May allocate internally (mutex, CV).
   */
  explicit Semaphore(std::size_t initial = 0) noexcept;

  // Non-copyable / non-movable (owns synchronization primitives).
  Semaphore(const Semaphore&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;
  Semaphore(Semaphore&&) = delete;
  Semaphore& operator=(Semaphore&&) = delete;

  ~Semaphore() = default;

  /**
   * @brief Acquire the semaphore (blocking).
   *
   * Blocks until count > 0, then decrements count.
   *
   * @note NOT RT-safe: May block indefinitely.
   */
  void acquire();

  /**
   * @brief Try to acquire without blocking.
   * @return True if acquired, false if count was zero.
   * @note RT-safe: Lock-free CAS loop.
   */
  [[nodiscard]] bool tryAcquire();

  /**
   * @brief Try to acquire with timeout.
   * @param timeout Maximum time to wait.
   * @return True if acquired, false if timeout elapsed.
   * @note NOT RT-safe: May block up to timeout.
   */
  template <class Rep, class Period>
  [[nodiscard]] bool tryAcquireFor(const std::chrono::duration<Rep, Period>& timeout);

  /**
   * @brief Release the semaphore (increment count).
   * @param count Number to add (default 1).
   * @note RT-safe: Atomic fetch_add + notify.
   */
  void release(std::size_t count = 1);

  /**
   * @brief Get current count (approximate, may be stale).
   * @return Current count value.
   * @note RT-safe: Atomic load.
   */
  [[nodiscard]] std::size_t count() const;

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<std::size_t> count_;
};

/* ----------------------------- Template Implementations ----------------------------- */

template <class Rep, class Period>
bool Semaphore::tryAcquireFor(const std::chrono::duration<Rep, Period>& timeout) {
  // Fast path: try atomic first.
  std::size_t current = count_.load(std::memory_order_relaxed);
  while (current > 0) {
    if (count_.compare_exchange_weak(current, current - 1, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return true;
    }
  }

  // Slow path: wait with timeout.
  std::unique_lock<std::mutex> lk(mutex_);
  if (!cv_.wait_for(lk, timeout, [this]() { return count_.load(std::memory_order_relaxed) > 0; })) {
    return false; // Timeout
  }

  // Try to acquire after wakeup.
  current = count_.load(std::memory_order_relaxed);
  while (current > 0) {
    if (count_.compare_exchange_weak(current, current - 1, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return true;
    }
  }
  return false; // Lost race after wakeup
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_SEMAPHORE_HPP
