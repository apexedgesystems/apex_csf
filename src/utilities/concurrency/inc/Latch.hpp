#ifndef APEX_UTILITIES_CONCURRENCY_LATCH_HPP
#define APEX_UTILITIES_CONCURRENCY_LATCH_HPP
/**
 * @file Latch.hpp
 * @brief One-shot countdown synchronization primitive.
 *
 * Design goals:
 *  - C++17 compatible (std::latch is C++20).
 *  - Multiple threads can count down.
 *  - Wait blocks until count reaches zero.
 *  - One-shot: once triggered, stays triggered.
 *
 * Use cases:
 *  - Wait for N workers to complete initialization.
 *  - Coordinate startup sequence.
 *  - Signal completion of parallel tasks.
 */

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace apex {
namespace concurrency {

/* ----------------------------- Latch ----------------------------- */

/**
 * @class Latch
 * @brief One-shot countdown latch.
 *
 * A latch maintains an internal counter. Threads can count down the counter,
 * and wait for it to reach zero. Once zero, the latch stays triggered.
 *
 * @note Thread-safe: All methods are safe to call from any thread.
 */
class Latch {
public:
  /**
   * @brief Construct a latch with initial count.
   * @param count Initial countdown value.
   * @note NOT RT-safe: May allocate internally (mutex, CV).
   */
  explicit Latch(std::size_t count) noexcept;

  // Non-copyable / non-movable.
  Latch(const Latch&) = delete;
  Latch& operator=(const Latch&) = delete;
  Latch(Latch&&) = delete;
  Latch& operator=(Latch&&) = delete;

  ~Latch() = default;

  /**
   * @brief Decrement the counter by n.
   * @param n Amount to decrement (default 1).
   * @note RT-safe when uncontended: Single mutex lock.
   */
  void countDown(std::size_t n = 1);

  /**
   * @brief Check if counter has reached zero.
   * @return True if count is zero.
   * @note RT-safe: Single atomic load.
   */
  [[nodiscard]] bool tryWait() const noexcept;

  /**
   * @brief Block until counter reaches zero.
   * @note NOT RT-safe: May block indefinitely.
   */
  void wait();

  /**
   * @brief Block until counter reaches zero or timeout.
   * @param timeout Maximum time to wait.
   * @return True if latch triggered, false if timeout.
   * @note NOT RT-safe: May block up to timeout.
   */
  template <class Rep, class Period>
  [[nodiscard]] bool waitFor(const std::chrono::duration<Rep, Period>& timeout);

  /**
   * @brief Decrement counter and wait for zero.
   * @param n Amount to decrement (default 1).
   * @note NOT RT-safe: May block indefinitely.
   */
  void arriveAndWait(std::size_t n = 1);

  /**
   * @brief Get current count (may be stale).
   * @return Current count value.
   * @note RT-safe: Single atomic load.
   */
  [[nodiscard]] std::size_t count() const noexcept;

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<std::size_t> count_;
};

/* ----------------------------- Template Implementations ----------------------------- */

template <class Rep, class Period>
bool Latch::waitFor(const std::chrono::duration<Rep, Period>& timeout) {
  // Fast path: already triggered.
  if (count_.load(std::memory_order_acquire) == 0) {
    return true;
  }

  // Slow path: wait with timeout.
  std::unique_lock<std::mutex> lk(mutex_);
  return cv_.wait_for(lk, timeout,
                      [this]() { return count_.load(std::memory_order_relaxed) == 0; });
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_LATCH_HPP
