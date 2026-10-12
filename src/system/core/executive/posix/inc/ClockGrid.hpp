#ifndef APEX_SYSTEM_CORE_EXECUTIVE_CLOCK_GRID_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_CLOCK_GRID_HPP
/**
 * @file ClockGrid.hpp
 * @brief Exact tick deadlines for an integer-hertz clock, and the wait that honours them.
 *
 * A fundamental of `hz` ticks per second rarely divides a second into
 * whole nanoseconds (600 Hz is 1,666,666.67 ns). Adding a truncated
 * period once per tick would lose the fraction every tick and drift one
 * tick short of a second within minutes. The grid here is computed from
 * the tick index instead: tick n sits at the anchor plus n / hz whole
 * seconds plus the (n mod hz)-th share of a second. Every second boundary
 * is exact and the error inside a second is under one nanosecond, for
 * every hz in [1, 1000].
 */

#include <cerrno>
#include <cstdint>
#include <ctime>

namespace executive {
namespace clockgrid {

inline constexpr std::uint64_t NS_PER_S = 1'000'000'000ULL;

/**
 * @brief Deadline of tick `n` on a grid anchored at `anchorNs`.
 * @param anchorNs Monotonic instant of tick 0, in nanoseconds.
 * @param n Tick index.
 * @param hz Ticks per second, in [1, 1000].
 * @return Monotonic instant of tick `n`, in nanoseconds.
 * @note RT-safe: two divisions and a multiply, no allocation.
 */
[[nodiscard]] constexpr std::uint64_t deadlineNs(std::uint64_t anchorNs, std::uint64_t n,
                                                 std::uint32_t hz) noexcept {
  return anchorNs + (n / hz) * NS_PER_S + ((n % hz) * NS_PER_S) / hz;
}

/**
 * @brief Nominal period of the grid.
 * @return 1 s / hz truncated to whole nanoseconds; the grid itself does
 *         not accumulate this truncation.
 * @note RT-safe.
 */
[[nodiscard]] constexpr std::uint64_t periodNs(std::uint32_t hz) noexcept { return NS_PER_S / hz; }

/**
 * @brief Number of ticks whose deadline has passed by `nowNs`.
 *
 * The largest n with deadlineNs(anchorNs, n, hz) <= nowNs, plus one; in
 * other words, how many ticks of the grid lie in [anchorNs, nowNs].
 *
 * @param anchorNs Monotonic instant of tick 0.
 * @param nowNs Monotonic instant at or after the anchor.
 * @param hz Ticks per second, in [1, 1000].
 * @return Tick count in [anchorNs, nowNs]; 0 when nowNs precedes the anchor.
 * @note RT-safe.
 */
[[nodiscard]] constexpr std::uint64_t ticksElapsed(std::uint64_t anchorNs, std::uint64_t nowNs,
                                                   std::uint32_t hz) noexcept {
  if (nowNs < anchorNs) {
    return 0;
  }
  const std::uint64_t D = nowNs - anchorNs;
  const std::uint64_t S = D / NS_PER_S;
  const std::uint64_t R = D % NS_PER_S;
  // Ticks inside the partial second: the k-th share lands at
  // floor(k * 1e9 / hz) <= R exactly when k * 1e9 < (R + 1) * hz.
  return S * hz + ((R + 1) * hz - 1) / NS_PER_S + 1;
}

/**
 * @brief Block the calling thread until an absolute CLOCK_MONOTONIC instant.
 *
 * On Linux this is one absolute-deadline sleep: the kernel owns the
 * deadline, so there is no gap between reading the clock and arming a
 * relative timer, and a signal that interrupts the wait is retried
 * against the same instant. Elsewhere the remaining interval is read from
 * the same clock and slept relatively, re-read after each interruption,
 * so both halves stay in one clock domain.
 *
 * @param deadlineNs CLOCK_MONOTONIC instant to wake at, in nanoseconds.
 * @note RT-safe apart from the wait itself: no allocation. Each signal
 *       interruption costs one retry; no bound is placed on how many
 *       signals arrive.
 */
inline void sleepUntilMonotonicNs(std::uint64_t deadlineNs) noexcept {
#if defined(__linux__)
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(deadlineNs / NS_PER_S);
  ts.tv_nsec = static_cast<long>(deadlineNs % NS_PER_S);
  while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
  }
#else
  for (;;) {
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    const std::uint64_t NOW_NS =
        static_cast<std::uint64_t>(now.tv_sec) * NS_PER_S + static_cast<std::uint64_t>(now.tv_nsec);
    if (NOW_NS >= deadlineNs) {
      return;
    }
    const std::uint64_t LEFT = deadlineNs - NOW_NS;
    timespec rem{};
    rem.tv_sec = static_cast<time_t>(LEFT / NS_PER_S);
    rem.tv_nsec = static_cast<long>(LEFT % NS_PER_S);
    if (::nanosleep(&rem, nullptr) == 0) {
      return;
    }
    if (errno != EINTR) {
      return;
    }
  }
#endif
}

/**
 * @brief Current CLOCK_MONOTONIC reading in nanoseconds.
 * @note RT-safe: one clock read (vDSO on Linux).
 */
[[nodiscard]] inline std::uint64_t nowNs() noexcept {
  timespec ts{};
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * NS_PER_S + static_cast<std::uint64_t>(ts.tv_nsec);
}

} // namespace clockgrid
} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_CLOCK_GRID_HPP
