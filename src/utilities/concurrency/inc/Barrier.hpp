#ifndef APEX_UTILITIES_CONCURRENCY_BARRIER_HPP
#define APEX_UTILITIES_CONCURRENCY_BARRIER_HPP
/**
 * @file Barrier.hpp
 * @brief Reusable thread synchronization barrier.
 *
 * Design goals:
 *  - std::barrier's phase semantics (a drop is one arrival of the current
 *    phase and leaves the next phases); C++20 like the rest of the library.
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
   *
   * A timed-out arrival is withdrawn: the phase no longer counts the caller,
   * so a later retry is one new arrival and the barrier stays usable.
   *
   * @param timeout Maximum time to wait.
   * @return True if barrier was reached, false if timeout.
   * @note NOT RT-safe: May block up to timeout.
   */
  template <class Rep, class Period>
  [[nodiscard]] bool arriveAndWaitFor(const std::chrono::duration<Rep, Period>& timeout);

  /**
   * @brief Arrive and drop out of future phases.
   *
   * One arrival of the current phase, and one participant fewer from the
   * next phase on: a phase of E participants completes after E arrivals
   * whether they wait or drop, as with std::barrier. With no participants
   * left, every arrival, a drop included, completes a phase by itself and
   * advances the generation.
   *
   * @note RT-safe when uncontended: Single mutex lock.
   */
  void arriveAndDrop();

  /**
   * @brief Get the participant count of the next phase.
   * @return Number of threads expected to arrive in each later phase (the
   *         construction count less the drops so far).
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
  /// Count one arrival of the current phase under the lock; true when it
  /// completes the phase, which then resets for the participants left.
  bool arriveLocked() noexcept;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<std::size_t> expected_;   ///< Participants of the next phases.
  std::size_t remaining_;               ///< Arrivals still needed in the current phase.
  std::atomic<std::size_t> generation_; ///< Phase counter.
};

/* ----------------------------- Template Implementations ----------------------------- */

template <class Rep, class Period>
bool Barrier::arriveAndWaitFor(const std::chrono::duration<Rep, Period>& timeout) {
  std::unique_lock<std::mutex> lk(mutex_);
  const std::size_t MY_GEN = generation_.load(std::memory_order_relaxed);

  if (arriveLocked()) {
    lk.unlock();
    cv_.notify_all();
    return true;
  }

  // Wait for this generation to complete
  const bool COMPLETED = cv_.wait_for(lk, timeout, [this, MY_GEN]() {
    return generation_.load(std::memory_order_relaxed) != MY_GEN;
  });
  if (!COMPLETED) {
    // The lock is held and the phase is still MY_GEN: the arrival is withdrawn.
    ++remaining_;
  }
  return COMPLETED;
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_BARRIER_HPP
