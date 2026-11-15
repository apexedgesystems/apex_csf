#ifndef APEX_UTILITIES_CONCURRENCY_LATCH_HPP
#define APEX_UTILITIES_CONCURRENCY_LATCH_HPP
/**
 * @file Latch.hpp
 * @brief One-shot countdown synchronization primitive.
 *
 * Design goals:
 *  - std::latch's shape; C++20 like the rest of the library.
 *  - Multiple threads can count down.
 *  - Wait blocks until count reaches zero.
 *  - One-shot: once triggered, stays triggered.
 *
 * Use cases:
 *  - Wait for N workers to complete initialization.
 *  - Coordinate startup sequence.
 *  - Signal completion of parallel tasks.
 */

#include <atomic>
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

  /**
   * @brief Destroy the latch. No other thread may still be inside a call on
   *        it: a true wait result orders the destruction after the final
   *        countDown(), not after other waiters or later callers.
   */
  ~Latch() = default;

  /**
   * @brief Decrement the counter by n.
   * @param n Amount to decrement (default 1).
   * @note RT-safe when uncontended: Single mutex lock.
   */
  void countDown(std::size_t n = 1);

  /**
   * @brief Check if counter has reached zero.
   *
   * A false result is one atomic load. A true result takes and releases the
   * mutex once after reading zero, so it returns after the final countDown()
   * has left the latch; a caller that is the last to use the latch may then
   * destroy it. The mutex is shared with every other call, so that
   * acquisition may block for as long as any holder is scheduled out.
   *
   * @return True if count is zero.
   * @note RT-safe only while the count is nonzero (one atomic load); a true
   *       result may block on the mutex.
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
  // Under the mutex throughout, like wait(): the predicate returns at once
  // when the count is already zero, and a waiter cannot return and destroy
  // the latch while the final countDown() still holds the mutex to notify.
  std::unique_lock<std::mutex> lk(mutex_);
  return cv_.wait_for(lk, timeout,
                      [this]() { return count_.load(std::memory_order_relaxed) == 0; });
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_LATCH_HPP
