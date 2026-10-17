#ifndef APEX_SYSTEM_CORE_SCHEDULER_TASKCONFIG_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_TASKCONFIG_HPP
/**
 * @file TaskConfig.hpp
 * @brief Scheduler-owned task configuration (frequency, priority, pool).
 *
 * Design rationale:
 *   Configuration lives in the scheduler, not on tasks. This enables:
 *   - Minimal task objects (just callable + label)
 *   - Scheduler-side optimization (adjust rates, reorder, load balance)
 *   - Single source of truth for scheduling metadata
 *   - Zero allocations in task hot paths
 *
 * RT-Safety:
 *   - All fields are POD
 *   - No heap allocations
 *   - All accessors are inline and noexcept
 *
 * Note: Thread affinity/policy is configured at pool level (PoolConfig),
 *       not per-task. This eliminates 10-30us syscall overhead per task.
 */

#include "src/utilities/helpers/inc/Cpu.hpp"

#include <cstdint>

#include <atomic>
#include <memory>

namespace system_core {

// Forward declaration
namespace schedulable {
class SchedulableTask;
} // namespace schedulable

namespace scheduler {

/* ----------------------------- Priority Constants ----------------------------- */

/// Logical priority range for scheduler ordering (not POSIX priority)
using TaskPriority = std::int8_t;
constexpr TaskPriority PRIORITY_LOWEST = -128;
constexpr TaskPriority PRIORITY_LOW = -64;
constexpr TaskPriority PRIORITY_NORMAL = 0;
constexpr TaskPriority PRIORITY_HIGH = 63;
constexpr TaskPriority PRIORITY_HIGHEST = 127;

/* ----------------------------- TaskConfig ----------------------------- */

/**
 * @struct TaskConfig
 * @brief Scheduling configuration for a task (owned by scheduler).
 *
 * All fields are POD for RT-safety. No heap allocations.
 * Scheduler uses this to:
 *   - Compute schedule geometry (which ticks the task runs on)
 *   - Route tasks to appropriate thread pool (poolId)
 *   - Perform frequency decimation during execution
 */
struct TaskConfig {
  /* ----------------------------- Frequency ----------------------------- */

  std::uint16_t freqN{1};  ///< Frequency numerator (Hz).
  std::uint16_t freqD{1};  ///< Frequency denominator (>=1). Effective freq = freqN/freqD.
  std::uint16_t offset{0}; ///< Tick offset within period [0, periodTicks).

  /* ----------------------------- Priority/Pool ----------------------------- */

  TaskPriority priority{PRIORITY_NORMAL}; ///< Logical priority [-128, 127].
  std::uint8_t poolId{0};                 ///< Thread pool ID (0 = default pool).

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default: 1Hz, normal priority, default pool. */
  TaskConfig() noexcept = default;

  /** @brief Construct with frequency parameters. */
  TaskConfig(std::uint16_t n, std::uint16_t d = 1, std::uint16_t off = 0) noexcept
      : freqN(n), freqD(d > 0 ? d : 1), offset(off) {}

  /** @brief Full construction. */
  TaskConfig(std::uint16_t n, std::uint16_t d, std::uint16_t off, TaskPriority prio,
             std::uint8_t pool = 0) noexcept
      : freqN(n), freqD(d > 0 ? d : 1), offset(off), priority(prio), poolId(pool) {}

  /* ----------------------------- Helpers ----------------------------- */

  /** @brief Computed frequency as float (N/D). */
  [[nodiscard]] float frequency() const noexcept {
    return static_cast<float>(freqN) / static_cast<float>(freqD);
  }
};

/* ----------------------------- TaskEntry ----------------------------- */

/**
 * @struct TaskEntry
 * @brief Scheduler-owned entry combining task pointer with config and runtime state.
 *
 * The scheduler maintains a collection of TaskEntry objects. Each entry contains:
 *   - Non-owning pointer to the task (caller manages task lifetime)
 *   - Scheduling configuration (frequency, priority, poolId)
 *   - Whether the scheduler consults a cadence for it (multi-second patterns)
 *
 * This design keeps tasks minimal while giving scheduler full control over scheduling.
 */
struct TaskEntry {
  schedulable::SchedulableTask* task{nullptr}; ///< Non-owning task pointer.
  TaskConfig config{};                         ///< Scheduling configuration.
  std::uint32_t fullUid{0};           ///< Owner component's full UID (for contention detection).
  const char* componentName{nullptr}; ///< Owner component name (for logging).
  std::uint8_t taskUid{0};            ///< Task UID within component (for registry registration).
  /// False for a denominator of one: such a task's pattern repeats every
  /// second, so its table slots are exact and every visit is a run. True
  /// for a multi-second pattern, whose cadence (kept by the scheduler
  /// beside the entries) says which visits are runs.
  bool gated{false};

  /* ----------------------------- Deadline Tracking ----------------------------- */

  /* ----------------------------- Per-Task Stats ----------------------------- */

  /**
   * @struct TaskStats
   * @brief Cross-thread runtime counters for one task.
   *
   * Writers: the tick thread stamps dispatch; the completing worker
   * writes runtime/overrun/completion; the tick thread counts deadline
   * violations. Readers snapshot from command/INSPECT context. All
   * fields atomic with relaxed ordering -- these are monotonic
   * telemetry counters, not synchronization.
   */
  struct TaskStats {
    std::atomic<std::uint64_t> dispatchNs{0};         ///< Last dispatch timestamp.
    std::atomic<std::uint32_t> maxRuntimeUs{0};       ///< Worst dispatch-to-complete time.
    std::atomic<std::uint32_t> lastRuntimeUs{0};      ///< Most recent runtime.
    std::atomic<std::uint32_t> completions{0};        ///< Completed executions.
    std::atomic<std::uint32_t> overruns{0};           ///< Runtime exceeded the task period.
    std::atomic<std::uint32_t> deadlineViolations{0}; ///< Still running at next dispatch.
  };

  /// Heap block for the same reason as inFlight: TaskEntry must stay
  /// movable while the counters stay atomic.
  std::unique_ptr<TaskStats> stats{std::make_unique<TaskStats>()};

  std::uint64_t periodNs{0}; ///< Task period in ns (set at addTask; 0 = unknown).

  /// Invocations admitted to a pool and not yet completed. A count, not a
  /// flag: a soft mode that does not skip can admit a second invocation
  /// while the first still runs, and the first's completion must not
  /// read as the entry being idle while the second is queued or running.
  /// Heap block so TaskEntry stays movable (no ref counting overhead).
  std::unique_ptr<std::atomic<std::uint32_t>> inFlight{
      std::make_unique<std::atomic<std::uint32_t>>(0U)};

  /**
   * @brief Mark task as dispatched (in-flight); stamps the dispatch time.
   *
   * The caller supplies the timestamp: the tick thread reads the clock
   * once per tick and every same-tick dispatch shares that base, so the
   * producer path pays one clock read regardless of table size.
   */
  void markDispatched(std::uint64_t dispatchNs) noexcept {
    if (stats) {
      stats->dispatchNs.store(dispatchNs, std::memory_order_relaxed);
    }
    if (inFlight)
      inFlight->fetch_add(1U, std::memory_order_release);
  }

  /**
   * @brief Mark task as completed; records runtime against the period.
   *
   * Runtime is dispatch-to-complete, so queue wait is charged to the
   * task -- the same base the deadline check uses. The dispatch base is
   * the tick's dispatch-decision time (shared across the tick).
   */
  void markCompleted() noexcept {
    if (stats) {
      const std::uint64_t NOW = apex::helpers::cpu::getMonotonicNs();
      const std::uint64_t START = stats->dispatchNs.load(std::memory_order_relaxed);
      if (START != 0 && NOW > START) {
        const auto US = static_cast<std::uint32_t>((NOW - START) / 1000U);
        stats->lastRuntimeUs.store(US, std::memory_order_relaxed);
        std::uint32_t prev = stats->maxRuntimeUs.load(std::memory_order_relaxed);
        while (US > prev &&
               !stats->maxRuntimeUs.compare_exchange_weak(prev, US, std::memory_order_relaxed)) {
        }
        if (periodNs != 0 && (NOW - START) > periodNs) {
          stats->overruns.fetch_add(1, std::memory_order_relaxed);
        }
      }
      stats->completions.fetch_add(1, std::memory_order_relaxed);
    }
    if (inFlight)
      inFlight->fetch_sub(1U, std::memory_order_release);
  }

  /// Undo one admission that never reached a pool (context exhaustion,
  /// ring rejection): the entry is not in flight for it.
  void unmarkDispatched() noexcept {
    if (inFlight)
      inFlight->fetch_sub(1U, std::memory_order_release);
  }

  /**
   * @brief Whether any admitted invocation is still outstanding.
   *
   * The acquire pairs with the release of every completion, so a reader
   * that sees zero also sees the completed invocations' writes.
   */
  [[nodiscard]] bool stillRunning() const noexcept {
    return inFlight && inFlight->load(std::memory_order_acquire) != 0U;
  }

  /** @brief Admitted invocations not yet completed. */
  [[nodiscard]] std::uint32_t inFlightCount() const noexcept {
    return inFlight ? inFlight->load(std::memory_order_acquire) : 0U;
  }

  /* ----------------------------- Skip Tracking ----------------------------- */

  std::uint32_t skipCount{0}; ///< Number of skipped invocations (for SKIP_ON_BUSY mode).

  /* ----------------------------- Sequencing ----------------------------- */

  /// Phase counter owned by the task's SequenceGroup (null = no sequencing).
  /// The group must outlive every entry registered against it -- the same
  /// lifetime the addTask(task, config, group) seam already requires.
  std::atomic<int>* seqCounter{nullptr};
  int seqPhase{0};    ///< Phase to wait for.
  int seqMaxPhase{0}; ///< Max phase (for wrap).

  /** @brief Check if this entry has sequencing enabled. */
  [[nodiscard]] bool isSequenced() const noexcept { return seqCounter != nullptr; }
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_TASKCONFIG_HPP
