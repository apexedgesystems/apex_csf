#ifndef APEX_SYSTEM_CORE_EXECUTIVE_RTMODE_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_RTMODE_HPP
/**
 * @file RTMode.hpp
 * @brief Real-time execution mode definitions for the executive.
 *
 * Defines the various real-time enforcement modes that control how the
 * executive handles task deadline misses and frame overruns.
 *
 * Modes are divided into two categories:
 *   - HARD modes: Trigger immediate shutdown on deadline miss
 *   - SOFT modes: Allow graceful degradation with logging
 */

#include <cstdint>
#include <string_view>

namespace executive {

/* ----------------------------- RTMode Enum ----------------------------- */

/**
 * @enum RTMode
 * @brief Real-time execution mode controlling deadline enforcement behavior.
 */
enum class RTMode : std::uint8_t {
  /**
   * @brief HARD: All tasks on tick N must complete before tick N+1.
   *
   * Strictest mode. Checks `poolActive || stepPending` at each tick.
   * Any incomplete work triggers immediate emergency shutdown.
   *
   * Use case: Ultra-strict determinism, safety-critical systems where
   * every tick boundary is a hard deadline.
   */
  HARD_TICK_COMPLETE = 0,

  /**
   * @brief HARD: Each task must complete before its next scheduled invocation.
   *
   * Recommended default. A 5Hz task has 200ms to complete (not 10ms).
   * Tracks per-task deadlines based on task period.
   *
   * Use case: Mixed-rate systems, typical embedded RT applications.
   */
  HARD_PERIOD_COMPLETE = 1,

  /**
   * @brief SOFT: Skip invocation if task is still running from previous call.
   *
   * Prevents task pileup. Logs skipped invocations but never fails.
   * Tracks skip count per task for diagnostics.
   *
   * Use case: Sensor polling, telemetry, non-critical periodic work.
   */
  SOFT_SKIP_ON_BUSY = 2,

  /**
   * @brief SOFT: Allow clock to lead task execution by configurable amount.
   *
   * Clock ticks continue even if tasks fall behind. Tracks lag
   * (clockCycles - taskExecutionCycles). Fails only if lag exceeds threshold.
   *
   * Use case: Systems that tolerate burst overloads but must catch up.
   */
  SOFT_LAG_TOLERANT = 3,

  /**
   * @brief SOFT: Log all deadline misses but never trigger failure.
   *
   * Same deadline tracking as HARD_PERIOD_COMPLETE but only logs.
   * Useful for development, debugging, and system characterization.
   *
   * Use case: Development, profiling, production monitoring.
   */
  SOFT_LOG_ONLY = 4,
};

/* ----------------------------- RTConfig Struct ----------------------------- */

/**
 * @struct RTConfig
 * @brief Configuration for real-time execution behavior.
 */
struct RTConfig {
  RTMode mode{RTMode::HARD_PERIOD_COMPLETE}; ///< Active RT mode
  std::uint32_t maxLagTicks{10};             ///< Max allowed lag for LAG_TOLERANT mode

  /**
   * @brief Check if mode is a HARD mode (triggers shutdown on miss).
   */
  [[nodiscard]] constexpr bool isHardMode() const noexcept {
    return mode == RTMode::HARD_TICK_COMPLETE || mode == RTMode::HARD_PERIOD_COMPLETE;
  }

  /**
   * @brief Check if mode is a SOFT mode (graceful degradation).
   */
  [[nodiscard]] constexpr bool isSoftMode() const noexcept { return !isHardMode(); }

  /**
   * @brief Check if mode requires per-task deadline tracking.
   */
  [[nodiscard]] constexpr bool needsDeadlineTracking() const noexcept {
    return mode == RTMode::HARD_PERIOD_COMPLETE || mode == RTMode::SOFT_SKIP_ON_BUSY ||
           mode == RTMode::SOFT_LOG_ONLY;
  }
};

/* ----------------------------- Helper Functions ----------------------------- */

/**
 * @brief Convert RTMode to string for logging/display.
 * @param mode The RT mode to convert.
 * @return String representation of the mode.
 */
[[nodiscard]] constexpr std::string_view rtModeToString(RTMode mode) noexcept {
  switch (mode) {
  case RTMode::HARD_TICK_COMPLETE:
    return "HARD_TICK_COMPLETE";
  case RTMode::HARD_PERIOD_COMPLETE:
    return "HARD_PERIOD_COMPLETE";
  case RTMode::SOFT_SKIP_ON_BUSY:
    return "SOFT_SKIP_ON_BUSY";
  case RTMode::SOFT_LAG_TOLERANT:
    return "SOFT_LAG_TOLERANT";
  case RTMode::SOFT_LOG_ONLY:
    return "SOFT_LOG_ONLY";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Parse RTMode from command-line string.
 * @param str String to parse (case-insensitive).
 * @param outMode Output mode if parsing succeeds.
 * @return true if parsing succeeded, false otherwise.
 */
[[nodiscard]] inline bool parseRTMode(std::string_view str, RTMode& outMode) noexcept {
  if (str == "tick-complete" || str == "HARD_TICK_COMPLETE") {
    outMode = RTMode::HARD_TICK_COMPLETE;
    return true;
  }
  if (str == "period-complete" || str == "HARD_PERIOD_COMPLETE") {
    outMode = RTMode::HARD_PERIOD_COMPLETE;
    return true;
  }
  if (str == "skip-on-busy" || str == "SOFT_SKIP_ON_BUSY") {
    outMode = RTMode::SOFT_SKIP_ON_BUSY;
    return true;
  }
  if (str == "lag-tolerant" || str == "SOFT_LAG_TOLERANT") {
    outMode = RTMode::SOFT_LAG_TOLERANT;
    return true;
  }
  if (str == "log-only" || str == "SOFT_LOG_ONLY") {
    outMode = RTMode::SOFT_LOG_ONLY;
    return true;
  }
  return false;
}

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_RTMODE_HPP
