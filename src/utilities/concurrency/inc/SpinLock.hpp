#ifndef APEX_UTILITIES_CONCURRENCY_SPIN_LOCK_HPP
#define APEX_UTILITIES_CONCURRENCY_SPIN_LOCK_HPP
/**
 * @file SpinLock.hpp
 * @brief Busy-wait spin lock for very short critical sections.
 *
 * Design goals:
 *  - Minimal latency for uncontended case.
 *  - Suitable for RT systems with short hold times.
 *  - Meets BasicLockable for use with std::lock_guard.
 *
 * Trade-offs:
 *  - Burns CPU while waiting (not suitable for long waits).
 *  - Use mutex/CV for longer critical sections.
 *
 * Use cases:
 *  - Protecting counters or small state updates.
 *  - Short RT-critical sections where CV sleep is too slow.
 */

#include <atomic>

#include "src/utilities/compatibility/inc/compat_rt_attrs.hpp"

namespace apex {
namespace concurrency {

/* ----------------------------- SpinLock ----------------------------- */

/**
 * @class SpinLock
 * @brief Simple test-and-set spin lock.
 *
 * Uses atomic flag with acquire/release semantics. Includes a PAUSE
 * hint in the spin loop to reduce power and improve performance on
 * hyper-threaded CPUs.
 *
 * @note Thread-safe: All methods are safe to call from any thread.
 * @note Meets BasicLockable requirements.
 */
class SpinLock {
public:
  /**
   * @brief Construct an unlocked spin lock.
   * @note RT-safe: No allocation.
   */
  constexpr SpinLock() noexcept = default;

  // Non-copyable / non-movable.
  SpinLock(const SpinLock&) = delete;
  SpinLock& operator=(const SpinLock&) = delete;
  SpinLock(SpinLock&&) = delete;
  SpinLock& operator=(SpinLock&&) = delete;

  ~SpinLock() = default;

  /**
   * @brief Acquire the lock (blocking spin).
   *
   * Spins until lock is acquired. Includes PAUSE hint for efficiency.
   *
   * @note RT-safe for short hold times: Bounded by hold duration.
   */
  void lock() noexcept APEX_NONBLOCKING;

  /**
   * @brief Try to acquire without spinning.
   * @return True if acquired, false if already locked.
   * @note RT-safe: Single atomic operation.
   */
  [[nodiscard]] bool tryLock() noexcept APEX_NONBLOCKING;

  /**
   * @brief Release the lock.
   * @note RT-safe: Single atomic operation.
   */
  void unlock() noexcept APEX_NONBLOCKING;

  /**
   * @brief Check if lock is currently held (approximate).
   * @return True if locked.
   * @note RT-safe: Single atomic load; it never touches the lock's state.
   */
  [[nodiscard]] bool isLocked() const noexcept APEX_NONBLOCKING;

private:
  std::atomic<bool> locked_{false};
};

/* ----------------------------- SpinLock Inline Methods ----------------------------- */

inline void SpinLock::lock() noexcept {
  // Acquire with an exchange; while held, spin on a plain read with the
  // PAUSE hint so the waiters do not hammer the line with writes.
  while (locked_.exchange(true, std::memory_order_acquire)) {
    while (locked_.load(std::memory_order_relaxed)) {
      // Spin-wait hint: reduces power and improves HT performance.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
      __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
      __asm__ volatile("yield" ::: "memory");
#else
      // Fallback: just spin
#endif
    }
  }
}

inline bool SpinLock::tryLock() noexcept {
  return !locked_.exchange(true, std::memory_order_acquire);
}

inline void SpinLock::unlock() noexcept { locked_.store(false, std::memory_order_release); }

inline bool SpinLock::isLocked() const noexcept { return locked_.load(std::memory_order_relaxed); }

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_SPIN_LOCK_HPP
