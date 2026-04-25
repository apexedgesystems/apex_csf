#ifndef APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_STATE_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_STATE_HPP
/**
 * @file ExecutiveState.hpp
 * @brief Consolidated state and output structures for ApexExecutive.
 *
 * Organizes executive data by DataCategory:
 *   - STATE: Runtime state tracked during execution
 *   - OUTPUT: Health/telemetry data for external consumption
 *
 * All structures are designed for registry integration and
 * binary serialization where applicable.
 *
 * @see ExecutiveData.hpp for TUNABLE_PARAM structures
 * @see DataCategory.hpp for category definitions
 */

#include <cstdint>

#include <atomic>

namespace executive {

/* ----------------------------- STATE Structures ----------------------------- */

/**
 * @struct ClockState
 * @brief Runtime state for clock thread.
 *
 * Thread-safe state maintained by clock thread, read by task execution
 * and watchdog threads. Uses atomics for cross-thread visibility.
 *
 * @note DataCategory::STATE
 */
struct ClockState {
  std::atomic<std::uint64_t> cycles{0};       ///< Total clock cycles executed.
  std::atomic<std::int64_t> lastTickNs{0};    ///< Timestamp of last tick (ns since epoch).
  std::atomic<bool> isRunning{false};         ///< Clock synchronization flag.
  std::atomic<std::uint64_t> overrunCount{0}; ///< Frame overrun counter.
};

/**
 * @struct TaskExecutionState
 * @brief Runtime state for task execution thread.
 *
 * Tracks task completion progress and deadline status.
 *
 * @note DataCategory::STATE
 */
struct TaskExecutionState {
  std::atomic<std::uint64_t> cycles{0};          ///< Task execution cycles completed.
  std::atomic<bool> stepPending{false};          ///< Step requested but not consumed.
  std::atomic<bool> lagThresholdExceeded{false}; ///< Lag exceeded maxLagTicks (LAG_TOLERANT).
};

/**
 * @struct WatchdogState
 * @brief Runtime state for watchdog thread.
 *
 * Tracks clock progress monitoring and warning counts.
 *
 * @note DataCategory::STATE
 */
struct WatchdogState {
  std::uint64_t lastClockCycles{0}; ///< Last observed clock cycle count.
  std::uint64_t warningCount{0};    ///< Total warnings issued.
  std::uint32_t intervalMs{1000};   ///< Watchdog check interval (ms).
  int supervisorFd{-1};             ///< Pipe fd for supervisor heartbeat (-1 = no supervisor).
};

/**
 * @struct ExternalIOState
 * @brief Runtime state for external I/O thread.
 *
 * Tracks command processing statistics. Uses atomics for
 * thread-safe updates from I/O thread.
 *
 * @note DataCategory::STATE
 */
struct ExternalIOState {
  std::atomic<std::uint64_t> commandsProcessed{0}; ///< Total commands executed.
  std::atomic<std::uint64_t> unknownCommands{0};   ///< Unrecognized input count.
  std::atomic<std::uint64_t> emptyLines{0};        ///< Empty line count.
  std::atomic<std::uint64_t> pollErrors{0};        ///< Poll error count.
  std::atomic<std::uint64_t> readErrors{0};        ///< Read error count.
};

/**
 * @struct ShutdownState
 * @brief Runtime state for shutdown sequence.
 *
 * Tracks completion of each shutdown stage for coordination
 * between threads.
 *
 * @note DataCategory::STATE
 */
struct ShutdownState {
  std::atomic<bool> stage1Complete{false}; ///< Signal received.
  std::atomic<bool> stage2Complete{false}; ///< Clock stopped.
  std::atomic<bool> stage3Complete{false}; ///< Tasks drained.
  std::atomic<bool> stage4Complete{false}; ///< Resources cleaned.
  std::atomic<bool> stage5Complete{false}; ///< Stats reported.
};

/**
 * @struct ProfilingState
 * @brief Runtime state for profiling subsystem.
 *
 * Tracks timing statistics when profiling is enabled.
 * All times in milliseconds for human readability.
 *
 * @note DataCategory::STATE
 */
struct ProfilingState {
  std::uint32_t sampleEveryN{0}; ///< Profile every Nth tick (0 = disabled).
  double minLoopTimeMs{1e9};     ///< Minimum loop iteration time (ms).
  double maxLoopTimeMs{0.0};     ///< Maximum loop iteration time (ms).
  double totalLoopTimeMs{0.0};   ///< Cumulative loop time for average (ms).
  std::uint64_t sampleCount{0};  ///< Number of profiling samples collected.

  /**
   * @brief Check if profiling is active.
   * @return true if sampleEveryN > 0.
   */
  [[nodiscard]] bool isActive() const noexcept { return sampleEveryN > 0; }

  /**
   * @brief Calculate average loop time.
   * @return Average time per loop (ms), or 0 if no samples.
   */
  [[nodiscard]] double averageLoopTimeMs() const noexcept {
    return (sampleCount > 0) ? (totalLoopTimeMs / static_cast<double>(sampleCount)) : 0.0;
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
 * @struct ControlState
 * @brief Runtime state for control flags.
 *
 * Atomic flags for pause/resume and other control operations.
 *
 * @note DataCategory::STATE
 */
struct ControlState {
  std::atomic<bool> startupRequested{false};  ///< Startup trigger.
  std::atomic<bool> shutdownRequested{false}; ///< Shutdown trigger.
  std::atomic<bool> pauseRequested{false};    ///< Pause request.
  std::atomic<bool> isPaused{false};          ///< Currently paused.
  std::atomic<bool> restartPending{false};    ///< Deferred execv (set by RELOAD_EXECUTIVE).
};

/* ----------------------------- OUTPUT Structures ----------------------------- */

#pragma pack(push, 1)

/**
 * @struct ExecutiveHealthPacket
 * @brief Health/telemetry packet for external consumption.
 *
 * Packed structure suitable for binary transmission. Contains
 * snapshot of executive state for health monitoring.
 *
 * Size: 48 bytes (packed).
 *
 * @note DataCategory::OUTPUT
 */
struct ExecutiveHealthPacket {
  // Timing/cycle counters (24 bytes)
  std::uint64_t clockCycles{0};         ///< Current clock cycle count.
  std::uint64_t taskExecutionCycles{0}; ///< Tasks cycles completed.
  std::uint64_t frameOverrunCount{0};   ///< Frame overrun count.

  // Watchdog (8 bytes)
  std::uint64_t watchdogWarningCount{0}; ///< Watchdog warning count.

  // Configuration snapshot (4 bytes)
  std::uint16_t clockFrequencyHz{0}; ///< Current clock frequency.
  std::uint8_t rtMode{0};            ///< Active RT mode.
  std::uint8_t flags{0};             ///< Bit flags (see below).

  // External I/O stats (8 bytes)
  std::uint64_t commandsProcessed{0}; ///< Commands handled.

  // Reserved for future use (4 bytes)
  std::uint32_t reserved{0};

  // Flag bit definitions
  static constexpr std::uint8_t FLAG_CLOCK_RUNNING = 0x01;      ///< Clock is running.
  static constexpr std::uint8_t FLAG_PAUSED = 0x02;             ///< System is paused.
  static constexpr std::uint8_t FLAG_LAG_EXCEEDED = 0x04;       ///< Lag threshold exceeded.
  static constexpr std::uint8_t FLAG_SHUTDOWN_REQUESTED = 0x08; ///< Shutdown in progress.
  static constexpr std::uint8_t FLAG_PROFILING_ACTIVE = 0x10;   ///< Profiling enabled.
  static constexpr std::uint8_t FLAG_SLEEPING = 0x20;           ///< Sleep mode active.

  /**
   * @brief Set a flag bit.
   * @param flag Flag constant to set.
   */
  void setFlag(std::uint8_t flag) noexcept { flags |= flag; }

  /**
   * @brief Clear a flag bit.
   * @param flag Flag constant to clear.
   */
  void clearFlag(std::uint8_t flag) noexcept { flags &= ~flag; }

  /**
   * @brief Check if a flag is set.
   * @param flag Flag constant to check.
   * @return true if flag is set.
   */
  [[nodiscard]] bool hasFlag(std::uint8_t flag) const noexcept { return (flags & flag) != 0; }
};

#pragma pack(pop)

static_assert(sizeof(ExecutiveHealthPacket) == 48,
              "ExecutiveHealthPacket size changed - update protocol documentation");

/* ----------------------------- INPUT Enums ----------------------------- */

/**
 * @enum ExecutiveCommand
 * @brief Commands that can be sent to the executive.
 *
 * These correspond to INPUT data category - external control inputs.
 *
 * @note DataCategory::INPUT
 */
enum class ExecutiveCommand : std::uint8_t {
  NONE = 0,         ///< No command.
  PAUSE = 1,        ///< Pause execution.
  RESUME = 2,       ///< Resume from pause.
  FAST_FORWARD = 3, ///< Enter fast-forward mode (non-RT).
  SHUTDOWN = 4,     ///< Request graceful shutdown.
  SET_FREQUENCY = 5 ///< Change clock frequency (requires payload).
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_STATE_HPP
