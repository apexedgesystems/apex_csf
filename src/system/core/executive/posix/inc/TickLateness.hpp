#ifndef APEX_SYSTEM_CORE_EXECUTIVE_TICK_LATENESS_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_TICK_LATENESS_HPP
/**
 * @file TickLateness.hpp
 * @brief Fixed-bucket record of how late each clock tick wakes against its deadline.
 *
 * The clock thread sleeps to an absolute deadline and wakes some time
 * after it: timer resolution, timer slack and scheduler latency all land
 * in that gap. This record keeps the distribution of that gap for a whole
 * run with no allocation: one bucket per microsecond across the first
 * millisecond, one overflow bucket beyond it, and the count, sum, minimum
 * and maximum. Percentiles are read back from the buckets at the end of
 * the run, so the per-tick cost is one array increment.
 */

#include <cstddef>
#include <cstdint>

#include <array>
#include <limits>

namespace executive {

/**
 * @struct TickLateness
 * @brief Lateness distribution of clock-tick wake-ups.
 *
 * Ownership: written by the clock thread only. Other threads read it only
 * after the clock thread has exited (the executive joins the clock thread
 * before run() returns), so the fields need no atomics.
 */
struct TickLateness {
  /// Bucket width and count: one microsecond per bucket across [0, 1 ms).
  static constexpr std::int64_t BUCKET_NS = 1'000;
  static constexpr std::size_t NUM_BUCKETS = 1'000;

  std::array<std::uint64_t, NUM_BUCKETS> buckets{}; ///< Wakes late by [i, i+1) us.
  std::uint64_t count{0};                           ///< Wakes recorded, all buckets.
  std::uint64_t overflow{0};                        ///< Wakes a millisecond or more late.
  std::uint64_t early{0};                           ///< Wakes before the deadline (bucket 0).
  std::uint64_t sumNs{0};                           ///< Sum of non-negative lateness.
  std::int64_t minNs{std::numeric_limits<std::int64_t>::max()}; ///< Least lateness seen.
  std::int64_t maxNs{0};                                        ///< Greatest lateness seen.

  /**
   * @brief Record one wake-up.
   * @param lateNs Wake instant minus deadline, in nanoseconds. A negative
   *        value (woke before the deadline) counts in bucket 0 and in `early`.
   * @note RT-safe: a few compares and increments, no allocation.
   */
  void record(std::int64_t lateNs) noexcept {
    ++count;
    if (lateNs < minNs) {
      minNs = lateNs;
    }
    if (lateNs > maxNs) {
      maxNs = lateNs;
    }
    if (lateNs < 0) {
      ++early;
      ++buckets[0];
      return;
    }
    sumNs += static_cast<std::uint64_t>(lateNs);
    const auto IDX = static_cast<std::uint64_t>(lateNs / BUCKET_NS);
    if (IDX >= NUM_BUCKETS) {
      ++overflow;
    } else {
      ++buckets[static_cast<std::size_t>(IDX)];
    }
  }

  /**
   * @brief Upper bound of the lateness at a given percentile.
   *
   * Returns the upper edge of the bucket holding the sample of rank
   * ceil(q * count), so the result reads as "q of all wakes were no later
   * than this". When that rank falls in the overflow bucket the exact
   * maximum is returned instead, since the buckets no longer resolve it.
   *
   * @param q Percentile in [0, 1].
   * @return Lateness bound in nanoseconds; 0 when nothing was recorded.
   * @note RT-safe: one bounded pass over the buckets, no allocation.
   */
  [[nodiscard]] std::int64_t percentileNs(double q) const noexcept {
    if (count == 0) {
      return 0;
    }
    if (q < 0.0) {
      q = 0.0;
    }
    if (q > 1.0) {
      q = 1.0;
    }
    auto rank = static_cast<std::uint64_t>(q * static_cast<double>(count));
    if (static_cast<double>(rank) < q * static_cast<double>(count)) {
      ++rank; // ceil
    }
    if (rank == 0) {
      rank = 1;
    }
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < NUM_BUCKETS; ++i) {
      seen += buckets[i];
      if (seen >= rank) {
        return static_cast<std::int64_t>(i + 1) * BUCKET_NS;
      }
    }
    return maxNs;
  }

  /**
   * @brief Mean of the non-negative lateness values.
   * @return Mean in nanoseconds; 0 when nothing was recorded.
   * @note RT-safe.
   */
  [[nodiscard]] double meanNs() const noexcept {
    const std::uint64_t LATE = count - early;
    return (LATE == 0) ? 0.0 : static_cast<double>(sumNs) / static_cast<double>(LATE);
  }

  /**
   * @brief Forget everything recorded so far.
   * @note RT-safe: fixed-size fill.
   */
  void reset() noexcept {
    buckets.fill(0);
    count = 0;
    overflow = 0;
    early = 0;
    sumNs = 0;
    minNs = std::numeric_limits<std::int64_t>::max();
    maxNs = 0;
  }
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_TICK_LATENESS_HPP
