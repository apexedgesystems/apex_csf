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
#include <cstddef>
#include <cstdint>
#include <thread>

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
   * @note Trivial: holds a single atomic counter, no internal allocation.
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
  // 32-bit so wait()/notify() use the native futex directly (a 64-bit counter
  // would route through libstdc++'s slower proxy waiter-pool). Permit counts
  // are bounded by the (small) queue capacity, so 32 bits is ample.
  std::atomic<std::uint32_t> count_;

  // Number of threads parked in acquire(). release() only issues a futex
  // wake when this is non-zero, so the hot path (busy pool, no waiters) pays
  // just an atomic add + load -- no notify call. seq_cst on count_/waiters_
  // forms a Dekker handshake that keeps the wakeup from being lost.
  std::atomic<std::uint32_t> waiters_{0};
};

/* ----------------------------- Template Implementations ----------------------------- */

template <class Rep, class Period>
bool Semaphore::tryAcquireFor(const std::chrono::duration<Rep, Period>& timeout) {
  // Fast path: try atomic decrement first.
  if (tryAcquire()) {
    return true;
  }

  // Slow path: poll with bounded exponential backoff until the deadline.
  // atomic::wait has no timed overload, so a timed acquire polls rather than
  // parks. This path is never on a hot path (the thread pool only ever calls
  // the untimed acquire()), so the small poll overhead is acceptable.
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::chrono::nanoseconds backoff{500};
  const std::chrono::nanoseconds maxBackoff{std::chrono::microseconds(100)};
  while (std::chrono::steady_clock::now() < deadline) {
    if (tryAcquire()) {
      return true;
    }
    std::this_thread::sleep_for(backoff);
    if (backoff < maxBackoff) {
      backoff *= 2;
    }
  }
  return tryAcquire(); // final attempt at the deadline
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_SEMAPHORE_HPP
