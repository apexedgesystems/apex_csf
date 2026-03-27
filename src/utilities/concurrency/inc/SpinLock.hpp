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
  void lock() noexcept;

  /**
   * @brief Try to acquire without spinning.
   * @return True if acquired, false if already locked.
   * @note RT-safe: Single atomic operation.
   */
  [[nodiscard]] bool tryLock() noexcept;

  /**
   * @brief Release the lock.
   * @note RT-safe: Single atomic operation.
   */
  void unlock() noexcept;

  /**
   * @brief Check if lock is currently held (approximate).
   * @return True if locked.
   * @note RT-safe: Single atomic load.
   */
  [[nodiscard]] bool isLocked() const noexcept;

private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

/* ----------------------------- SpinLock Inline Methods ----------------------------- */

inline void SpinLock::lock() noexcept {
  // Spin with test-and-set, using PAUSE hint between attempts.
  while (flag_.test_and_set(std::memory_order_acquire)) {
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

inline bool SpinLock::tryLock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }

inline void SpinLock::unlock() noexcept { flag_.clear(std::memory_order_release); }

inline bool SpinLock::isLocked() const noexcept {
  // Note: test() is C++20, so we use a const_cast workaround for C++17.
  // This is safe because we're only reading the state.
  auto& mutableFlag = const_cast<std::atomic_flag&>(flag_);
  const bool WAS_SET = mutableFlag.test_and_set(std::memory_order_relaxed);
  if (!WAS_SET) {
    mutableFlag.clear(std::memory_order_relaxed);
  }
  return WAS_SET;
}

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_SPIN_LOCK_HPP
