#ifndef APEX_SYSTEM_CORE_SCHEDULER_TASKCADENCE_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_TASKCADENCE_HPP
/**
 * @file TaskCadence.hpp
 * @brief The run instants of one task on the fundamental tick grid.
 *
 * A task at N/D Hz on a fundamental of Hz ticks per second runs N times
 * in every D seconds: run k falls on tick
 *
 *     offset + floor(k * Hz * D / N)
 *
 * counted from the first tick the scheduler ever dispatched. Successive
 * runs are floor(Hz * D / N) ticks apart, or one more, so the count per
 * pattern period of Hz * D ticks is exact and the spacing is uneven by
 * at most one tick when the rate does not land on whole ticks (30 Hz on
 * a 100 Hz fundamental runs 3, 3, 4 ticks apart and 30 times a second).
 * A rate that does land on whole ticks has a single spacing, as before.
 *
 * The instant is stepped with a remainder accumulator, so dispatch does
 * no multiplication or division.
 */

#include <cstdint>

namespace system_core {
namespace scheduler {

/**
 * @struct TaskCadence
 * @brief Next-run bookkeeping for one task.
 *
 * Owned by the scheduler's task entry; read and advanced by the tick
 * thread only.
 */
struct TaskCadence {
  std::uint64_t nextRun{0}; ///< Cycle of the next run.
  std::uint32_t whole{1};   ///< floor(Hz * D / N): ticks between runs, rounded down.
  std::uint32_t rem{0};     ///< (Hz * D) mod N, carried in `acc`.
  std::uint32_t acc{0};     ///< Remainder accumulator in [0, N).
  std::uint32_t n{1};       ///< Numerator N: the accumulator's modulus.

  /**
   * @brief Whether a rate request can be scheduled at all.
   *
   * Refused: a zero numerator or denominator, a rate above the
   * fundamental (N > Hz * D), and an offset at or beyond the first run's
   * period (offset * N >= Hz * D). Everything else is admitted, whether or
   * not it lands on whole ticks.
   *
   * @note RT-safe: integer compares.
   */
  [[nodiscard]] static constexpr bool admissible(std::uint32_t hz, std::uint32_t freqN,
                                                 std::uint32_t freqD,
                                                 std::uint32_t offset) noexcept {
    if (hz == 0U || freqN == 0U || freqD == 0U) {
      return false;
    }
    const std::uint64_t HD = static_cast<std::uint64_t>(hz) * freqD;
    if (freqN > HD) {
      return false;
    }
    return static_cast<std::uint64_t>(offset) * freqN < HD;
  }

  /**
   * @brief Pattern period: the span over which the task runs exactly N times.
   * @note RT-safe.
   */
  [[nodiscard]] static constexpr std::uint64_t patternTicks(std::uint32_t hz,
                                                            std::uint32_t freqD) noexcept {
    return static_cast<std::uint64_t>(hz) * freqD;
  }

  /**
   * @brief Set up with the first run at `offset`, counted from cycle 0.
   * @pre admissible(hz, freqN, freqD, offset).
   * @note RT-safe.
   */
  constexpr void init(std::uint32_t hz, std::uint32_t freqN, std::uint32_t freqD,
                      std::uint32_t offset) noexcept {
    const std::uint64_t HD = patternTicks(hz, freqD);
    whole = static_cast<std::uint32_t>(HD / freqN);
    rem = static_cast<std::uint32_t>(HD % freqN);
    acc = 0;
    n = freqN;
    nextRun = offset;
  }

  /**
   * @brief Set up on the same grid as init(), with the first run being the
   *        earliest instant of the pattern at or after `cycle`.
   *
   * A table loaded while the clock is already running keeps every task's
   * phase as if it had been there from the first tick, so a reload never
   * shifts a schedule. Exact for cycle counts below 2^64 / N, which at the
   * widest numerator and 1000 Hz is several thousand years of run time.
   *
   * @pre admissible(hz, freqN, freqD, offset).
   * @note RT-safe: a handful of integer operations; used at table load.
   */
  constexpr void initAt(std::uint32_t hz, std::uint32_t freqN, std::uint32_t freqD,
                        std::uint32_t offset, std::uint64_t cycle) noexcept {
    init(hz, freqN, freqD, offset);
    if (cycle <= offset) {
      return;
    }
    // Smallest k with offset + floor(k * HD / N) >= cycle, which holds
    // exactly when k * HD >= (cycle - offset) * N.
    const std::uint64_t HD = patternTicks(hz, freqD);
    const std::uint64_t NEED = (cycle - offset) * freqN;
    const std::uint64_t K = (NEED + HD - 1) / HD;
    nextRun = offset + (K * HD) / freqN;
    acc = static_cast<std::uint32_t>((K * HD) % freqN);
  }

  /**
   * @brief Whether the task runs on this cycle.
   *
   * At-or-after rather than equal: if a cycle were ever not dispatched,
   * the task runs on its next visit instead of never, and advance() then
   * walks the instant forward one run per visit until it leads again.
   *
   * @note RT-safe: one compare.
   */
  [[nodiscard]] constexpr bool due(std::uint64_t cycle) const noexcept { return cycle >= nextRun; }

  /**
   * @brief Move to the next run instant.
   * @note RT-safe: three additions and a compare.
   */
  constexpr void advance() noexcept {
    nextRun += whole;
    acc += rem;
    if (acc >= n) {
      acc -= n;
      ++nextRun;
    }
  }

  /** @brief True when the rate does not land on whole ticks. RT-safe. */
  [[nodiscard]] constexpr bool uneven() const noexcept { return rem != 0U; }

  /** @brief Shortest spacing between two runs, in ticks. RT-safe. */
  [[nodiscard]] constexpr std::uint32_t shortestGapTicks() const noexcept { return whole; }

  /** @brief Longest spacing between two runs, in ticks. RT-safe. */
  [[nodiscard]] constexpr std::uint32_t longestGapTicks() const noexcept {
    return (rem != 0U) ? whole + 1U : whole;
  }
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_TASKCADENCE_HPP
