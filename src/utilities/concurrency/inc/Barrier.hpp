#ifndef APEX_UTILITIES_CONCURRENCY_BARRIER_HPP
#define APEX_UTILITIES_CONCURRENCY_BARRIER_HPP
/**
 * @file Barrier.hpp
 * @brief Reusable thread synchronization barrier.
 *
 * Design goals:
 *  - C++17 compatible (std::barrier is C++20).
 *  - Reusable after each phase completes.
 *  - Threads can drop out with arriveAndDrop().
 *  - Simple generation-based implementation.
 *
 * Use cases:
 *  - Parallel algorithm phase synchronization.
 *  - Iterative computation barriers.
 *  - Worker thread coordination.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace apex {
namespace concurrency {

/* ----------------------------- Barrier ----------------------------- */

/**
 * @class Barrier
 * @brief Reusable synchronization barrier.
 *
 * A barrier synchronizes a fixed number of threads at a rendezvous point.
 * Once all expected threads arrive, they are all released and the barrier
 * resets for the next phase.
 *
 * @note Thread-safe: All methods are safe to call from any thread.
 */
class Barrier {
public:
  /**
   * @brief Construct a barrier for the specified number of threads.
   * @param expected Number of threads that must arrive.
   * @note NOT RT-safe: May allocate internally (mutex, CV).
   */
  explicit Barrier(std::size_t expected) noexcept;

  // Non-copyable / non-movable.
  Barrier(const Barrier&) = delete;
  Barrier& operator=(const Barrier&) = delete;
  Barrier(Barrier&&) = delete;
  Barrier& operator=(Barrier&&) = delete;

  ~Barrier() = default;

  /**
   * @brief Arrive at the barrier and wait for others.
   *
   * Blocks until all expected threads have arrived, then releases all.
   * The barrier automatically resets for the next phase.
   *
   * @note NOT RT-safe: May block until all threads arrive.
   */
  void arriveAndWait();

  /**
   * @brief Arrive at the barrier and wait with timeout.
   * @param timeout Maximum time to wait.
   * @return True if barrier was reached, false if timeout.
   * @note NOT RT-safe: May block up to timeout.
   */
  template <class Rep, class Period>
  [[nodiscard]] bool arriveAndWaitFor(const std::chrono::duration<Rep, Period>& timeout);

  /**
   * @brief Arrive and drop out of future phases.
   *
   * The thread signals arrival but will not participate in future phases.
   * Reduces the expected count for subsequent phases.
   *
   * @note RT-safe when uncontended: Single mutex lock.
   */
  void arriveAndDrop();

  /**
   * @brief Get the expected count for current phase.
   * @return Number of threads expected.
   * @note RT-safe: Atomic load.
   */
  [[nodiscard]] std::size_t expected() const;

  /**
   * @brief Get the current generation number.
   * @return Generation count (increments after each phase).
   * @note RT-safe: Atomic load.
   */
  [[nodiscard]] std::size_t generation() const;

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<std::size_t> expected_;   ///< Threads expected in each phase.
  std::size_t arrived_;                 ///< Threads arrived in current phase.
  std::atomic<std::size_t> generation_; ///< Phase counter.
};

/* ----------------------------- Template Implementations ----------------------------- */

template <class Rep, class Period>
bool Barrier::arriveAndWaitFor(const std::chrono::duration<Rep, Period>& timeout) {
  std::unique_lock<std::mutex> lk(mutex_);
  const std::size_t MY_GEN = generation_.load(std::memory_order_relaxed);
  ++arrived_;

  if (arrived_ >= expected_.load(std::memory_order_relaxed)) {
    // Last thread to arrive: release all and reset
    arrived_ = 0;
    generation_.fetch_add(1, std::memory_order_release);
    lk.unlock();
    cv_.notify_all();
    return true;
  }

  // Wait for this generation to complete
  return cv_.wait_for(lk, timeout, [this, MY_GEN]() {
    return generation_.load(std::memory_order_relaxed) != MY_GEN;
  });
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_BARRIER_HPP
