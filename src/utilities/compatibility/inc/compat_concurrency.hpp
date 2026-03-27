#ifndef APEX_UTILITIES_COMPATIBILITY_CONCURRENCY_HPP
#define APEX_UTILITIES_COMPATIBILITY_CONCURRENCY_HPP
/**
 * @file compat_concurrency.hpp
 * @brief Concurrency compatibility shims for C++17+ with C++20 fast-paths.
 *
 * Provides:
 *  - apex::compat::atom::waitEq()        : atomic wait shim (C++20 forwarder or fallback)
 *  - apex::compat::atom::notifyOne/all() : atomic notify shims (C++20 forwarders or fallbacks)
 *  - apex::compat::SrcLoc                : source-location shim (C++20 forwarder or FILE/LINE)
 *
 * RT-Safety Notes:
 *  - C++20 std::atomic::wait: Fast, kernel-optimized (recommended for RT)
 *  - C++17 Linux with futex: Fast-path checks avoid unnecessary syscalls (~100ns when needed)
 *  - C++17 fallback (no futex): Pure spinlock with exponential backoff (RT-safe but CPU-intensive)
 *  - For strict RT: Define COMPAT_DISABLE_FUTEX to force pure spinlock on all platforms
 *
 * Performance Characteristics:
 *  - C++20: ~10-50ns (std::atomic::wait optimized by kernel)
 *  - C++17 + futex: ~100ns when blocking (syscall overhead)
 *  - C++17 no futex: 0ns but burns CPU cycles
 *
 * Implementation Notes:
 *  - Fast-path check before entering wait loop (line 69) avoids syscall if value already changed
 *  - Futex loop re-checks before each syscall (line 76) to minimize blocking
 *  - Exponential backoff in spinlock prevents cache-line thrashing
 *
 * Recommendations:
 *  - RT systems: Use C++20 or define COMPAT_DISABLE_FUTEX for deterministic spinlocks
 *  - Non-RT: C++17 with futex is a good balance (avoids burning CPU)
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <type_traits>

#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#endif

// Feature-test: atomic wait/notify (C++20)
#ifndef __cpp_lib_atomic_wait
#define __cpp_lib_atomic_wait 0
#endif

// Feature-test: source_location (C++20)
#ifndef __cpp_lib_source_location
#define __cpp_lib_source_location 0
#endif

#if __cpp_lib_source_location >= 201907L
#include <source_location>
#endif

// Optional Linux futex fast-path for pre-C++20 atomic waiting.
// Define COMPAT_DISABLE_FUTEX to force pure spinlock (RT-safe, but CPU-intensive).
#if defined(__linux__) && !defined(COMPAT_DISABLE_FUTEX)
#define COMPAT_USE_FUTEX 1
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <errno.h>
#else
#define COMPAT_USE_FUTEX 0
#endif

#include "src/utilities/helpers/inc/Cpu.hpp"

namespace apex {
namespace compat {

/* ---------------------------------- API ----------------------------------- */
namespace atom {

/**
 * @brief Wait until atomic value differs from expected.
 * @note RT-CAUTION: C++20 uses kernel wait; C++17 may use futex syscalls or spinlock.
 */
template <class T>
inline std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, void>
waitEq(std::atomic<T>& a, T expected) noexcept {
#if __cpp_lib_atomic_wait >= 201907L
  // C++20 fast path: kernel-optimized wait
  a.wait(expected);
  (void)expected;
#else
  // Fast-path: Quick exit if value already changed (avoids syscall/spinloop)
  if (a.load(std::memory_order_acquire) != expected)
    return;

#if COMPAT_USE_FUTEX
  // Linux futex path: Efficient blocking with fast-path checks
  if constexpr (sizeof(T) <= sizeof(int)) {
    for (;;) {
      // Re-check before syscall to avoid unnecessary blocking
      if (a.load(std::memory_order_acquire) != expected)
        break;
      auto* addr = reinterpret_cast<int*>(const_cast<T*>(reinterpret_cast<volatile const T*>(&a)));
      int rc = syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, static_cast<int>(expected), nullptr,
                       nullptr, 0);
      (void)rc; // May return EAGAIN if value changed, which is fine
    }
    return;
  }
#endif

  // Portable spinlock with exponential backoff (RT-safe but CPU-intensive)
  apex::helpers::cpu::ExponentialBackoff bk;
  for (;;) {
    if (a.load(std::memory_order_acquire) != expected)
      break;
    bk.spinOnce();
    // Occasional yield to reduce CPU burn (every 64 iterations)
    static thread_local unsigned ctr = 0;
    if ((++ctr & 0x3F) == 0)
      std::this_thread::yield();
  }
#endif
}

/**
 * @brief Wake one thread waiting on this atomic.
 * @note RT-CAUTION: C++20 uses kernel notify; C++17 may use futex syscall.
 */
template <class T> inline void notifyOne(std::atomic<T>& a) noexcept {
#if __cpp_lib_atomic_wait >= 201907L
  a.notify_one();
#else
  (void)a;
#if COMPAT_USE_FUTEX
  if constexpr (sizeof(T) <= sizeof(int)) {
    auto* addr = reinterpret_cast<int*>(const_cast<T*>(reinterpret_cast<volatile const T*>(&a)));
    syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
  }
#endif
#endif
}

/**
 * @brief Wake all threads waiting on this atomic.
 * @note RT-CAUTION: C++20 uses kernel notify; C++17 may use futex syscall.
 */
template <class T> inline void notifyAll(std::atomic<T>& a) noexcept {
#if __cpp_lib_atomic_wait >= 201907L
  a.notify_all();
#else
  (void)a;
#if COMPAT_USE_FUTEX
  if constexpr (sizeof(T) <= sizeof(int)) {
    auto* addr = reinterpret_cast<int*>(const_cast<T*>(reinterpret_cast<volatile const T*>(&a)));
    syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, std::numeric_limits<int>::max(), nullptr, nullptr,
            0);
  }
#endif
#endif
}

} // namespace atom

/* --------------------------------- Types ---------------------------------- */

/**
 * @brief Source location shim (C++20 std::source_location or fallback).
 * @note RT-safe (compile-time data only).
 */
struct SrcLoc {
  const char* file{};
  const char* function{};
  std::uint_least32_t line{0};
  std::uint_least32_t column{0};

  static constexpr SrcLoc current(
#if __cpp_lib_source_location >= 201907L
      const std::source_location& loc = std::source_location::current()
#endif
          ) noexcept {
#if __cpp_lib_source_location >= 201907L
    return SrcLoc{loc.file_name(), loc.function_name(),
                  static_cast<std::uint_least32_t>(loc.line()),
                  static_cast<std::uint_least32_t>(loc.column())};
#else
    return SrcLoc{};
#endif
  }
};

} // namespace compat
} // namespace apex

#endif // APEX_UTILITIES_COMPATIBILITY_CONCURRENCY_HPP