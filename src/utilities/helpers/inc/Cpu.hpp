#ifndef APEX_UTILITIES_HELPERS_CPU_HPP
#define APEX_UTILITIES_HELPERS_CPU_HPP
/**
 * @file Cpu.hpp
 * @brief CPU relax/backoff helpers for spin-waits on common architectures.
 *
 * Provides architecture-specific pause/yield hints for efficient spin-waiting
 * and an exponential backoff helper for contended scenarios.
 *
 * @note RT-SAFE: Single CPU instruction (~1-2 cycles), no allocation or syscalls.
 */

#include <cstdint>
#include <ctime> // clock_gettime, CLOCK_MONOTONIC

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#endif

namespace apex {
namespace helpers {
namespace cpu {

/* ----------------------------- Timing ----------------------------- */

/**
 * @brief Get monotonic timestamp in nanoseconds.
 *
 * Uses CLOCK_MONOTONIC for consistent, non-decreasing time measurements
 * unaffected by system clock adjustments.
 *
 * @return Current monotonic time in nanoseconds.
 * @note RT-CAUTION: Syscall (clock_gettime), but typically vDSO-accelerated.
 */
[[nodiscard]] inline std::uint64_t getMonotonicNs() noexcept {
  struct timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

/* ----------------------------- Spin Hints ----------------------------- */

/**
 * @brief Hint the CPU to relax/yield in a spin loop.
 *
 * Uses architecture-specific instructions:
 *  - x86/x64: PAUSE instruction
 *  - ARM: YIELD instruction
 *  - Others: No-op (safe fallback)
 *
 * @note RT-SAFE: Single CPU instruction.
 */
inline void relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(__GNUC__) || defined(__clang__)
  __builtin_ia32_pause();
#elif defined(_MSC_VER)
  _mm_pause();
#else
  (void)0;
#endif
#elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM) || defined(_M_ARM64)
#if defined(__GNUC__) || defined(__clang__)
  __asm__ __volatile__("yield" ::: "memory");
#else
  (void)0;
#endif
#else
  (void)0;
#endif
}

/* ----------------------------- Types ----------------------------- */

/**
 * @brief Exponential backoff helper for contested locks.
 *
 * Increases spin count exponentially up to a maximum (4096 iterations),
 * reducing cache-line thrashing in contended scenarios.
 *
 * @note RT-SAFE: No allocation, bounded iteration.
 */
struct ExponentialBackoff {
  unsigned n{1}; ///< Current spin count.

  /**
   * @brief Execute one backoff cycle.
   *
   * Spins for n iterations calling relax(), then doubles n (up to 4096).
   *
   * @note RT-SAFE: Bounded iteration.
   */
  inline void spinOnce() noexcept {
    for (unsigned i = 0; i < n; ++i) {
      relax();
    }
    if (n < (1u << 12)) {
      n <<= 1;
    }
  }

  /**
   * @brief Reset backoff counter to initial state.
   * @note RT-SAFE: No allocation.
   */
  inline void reset() noexcept { n = 1; }
};

} // namespace cpu
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_CPU_HPP
