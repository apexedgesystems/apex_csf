#ifndef APEX_SYSTEM_CORE_EXECUTIVE_TASKEXECUTION_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_TASKEXECUTION_HPP
/**
 * @file ApexExecutive_TaskExecution.hpp
 * @brief Task execution coordination with optional profiling support.
 *
 * Responsibilities:
 *  - Synchronize with clock thread for tick-driven execution
 *  - Invoke scheduler for each clock tick
 *  - Optional detailed profiling of execution timing
 *  - Pause/resume coordination with clock thread
 *  - Cycle tracking for scheduler tick calculation
 *
 * Profiling overhead (when enabled):
 *  - ~1-2us per sampled frame (0.01-0.02% at 100Hz)
 *  - Zero overhead when disabled (just a branch check)
 *  - Configurable via --enable-profiling --profile-interval N
 */

#include <cstdint>

#include <atomic>

namespace executive {

/**
 * @struct ProfilingStats
 * @brief Runtime profiling statistics for task execution loop.
 *
 * Tracks timing metrics when profiling is enabled.
 * All times in milliseconds for human readability.
 */
struct ProfilingStats {
  double minLoopTimeMs{1e9};    ///< Minimum loop iteration time
  double maxLoopTimeMs{0.0};    ///< Maximum loop iteration time
  double totalLoopTimeMs{0.0};  ///< Cumulative loop time (for average)
  std::uint64_t sampleCount{0}; ///< Number of profiling samples collected

  /**
   * @brief Calculate average loop time.
   * @return Average time per loop (ms), or 0 if no samples
   */
  [[nodiscard]] double averageLoopTimeMs() const noexcept {
    return (sampleCount > 0) ? (totalLoopTimeMs / sampleCount) : 0.0;
  }

  /**
   * @brief Reset all statistics.
   */
  void reset() noexcept {
    minLoopTimeMs = 1e9;
    maxLoopTimeMs = 0.0;
    totalLoopTimeMs = 0.0;
    sampleCount = 0;
  }
};

/**
 * @struct TimingPoints
 * @brief Simplified timing breakdowns for actionable profiling.
 *
 * Captures timestamps at key execution points:
 *  - t0: Loop start (after wait)
 *  - t1: Before scheduler execution
 *  - t2: After scheduler execution
 *  - t3: After atomic operations (loop end)
 *  - tickNs: Clock tick timestamp (for latency measurement)
 *
 * All times in nanoseconds (high resolution).
 *
 * Optimization: Removed t0a, t0b, t2a as they measured <50ns operations
 * that aren't actionable for optimization.
 */
struct TimingPoints {
  std::int64_t t0Ns{0};   ///< Loop start (after wake from wait)
  std::int64_t t1Ns{0};   ///< Before scheduler execution
  std::int64_t t2Ns{0};   ///< After scheduler execution
  std::int64_t t3Ns{0};   ///< After atomic wrap-up (loop end)
  std::int64_t tickNs{0}; ///< Clock tick timestamp (for latency)
};

/**
 * @brief Convert nanoseconds to milliseconds.
 * @param ns Time in nanoseconds
 * @return Time in milliseconds (fractional)
 */
inline constexpr double nsToMs(std::int64_t ns) noexcept {
  return static_cast<double>(ns) / 1'000'000.0;
}

/**
 * @brief Compute profiling metrics from timing points.
 * @param tp Timing points captured during loop
 * @param tick Current tick being executed
 * @param outBuf Output buffer for formatted string (must be >=256 bytes)
 * @return Pointer to outBuf (for convenience)
 *
 * Metrics computed:
 *  - tickToExec: Clock tick to execution start latency (jitter)
 *  - exec: Scheduler execution time (core work)
 *  - wrap: Atomic operations overhead (cleanup)
 *  - loop: Total loop iteration time (frame utilization)
 *
 * Optimization: Uses caller-provided buffer to avoid heap allocation.
 * Format is CSV-like for easy parsing in analysis tools.
 */
inline char* computeProfilingMetrics(const TimingPoints& tp, std::uint16_t tick,
                                     char* outBuf) noexcept {
  const double tickToExecMs = (tp.tickNs > 0) ? nsToMs(tp.t1Ns - tp.tickNs) : -1.0;
  const double execMs = nsToMs(tp.t2Ns - tp.t1Ns);
  const double wrapMs = nsToMs(tp.t3Ns - tp.t2Ns);
  const double loopMs = nsToMs(tp.t3Ns - tp.t0Ns);

  std::snprintf(outBuf, 256, "tick=%u tickToExec=%.3f exec=%.3f wrap=%.3f loop=%.3f ms", tick,
                tickToExecMs, execMs, wrapMs, loopMs);
  return outBuf;
}

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_TASKEXECUTION_HPP