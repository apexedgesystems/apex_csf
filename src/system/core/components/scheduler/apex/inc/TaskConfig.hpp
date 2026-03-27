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
 *   - Runtime state for frequency decimation (holdCtr)
 *
 * This design keeps tasks minimal while giving scheduler full control over scheduling.
 */
struct TaskEntry {
  schedulable::SchedulableTask* task{nullptr}; ///< Non-owning task pointer.
  TaskConfig config{};                         ///< Scheduling configuration.
  std::uint32_t fullUid{0};           ///< Owner component's full UID (for contention detection).
  const char* componentName{nullptr}; ///< Owner component name (for logging).
  std::uint8_t taskUid{0};            ///< Task UID within component (for registry registration).
  std::uint16_t holdCtr{0};           ///< Frequency decimation counter.

  /* ----------------------------- Deadline Tracking ----------------------------- */

  /// In-flight flag for deadline tracking (unique_ptr: no ref counting overhead).
  std::unique_ptr<std::atomic<bool>> isRunning{std::make_unique<std::atomic<bool>>(false)};

  /** @brief Mark task as dispatched (in-flight). */
  void markDispatched() noexcept {
    if (isRunning)
      isRunning->store(true, std::memory_order_release);
  }

  /** @brief Mark task as completed (no longer in-flight). */
  void markCompleted() noexcept {
    if (isRunning)
      isRunning->store(false, std::memory_order_release);
  }

  /** @brief Check if task is still running from previous dispatch. */
  [[nodiscard]] bool stillRunning() const noexcept {
    return isRunning && isRunning->load(std::memory_order_acquire);
  }

  /* ----------------------------- Skip Tracking ----------------------------- */

  std::uint32_t skipCount{0}; ///< Number of skipped invocations (for SKIP_ON_BUSY mode).

  /* ----------------------------- Sequencing ----------------------------- */

  std::shared_ptr<std::atomic<int>> seqCounter{nullptr}; ///< Shared counter (null = no sequencing).
  int seqPhase{0};                                       ///< Phase to wait for.
  int seqMaxPhase{0};                                    ///< Max phase (for wrap).

  /** @brief Check if this entry has sequencing enabled. */
  [[nodiscard]] bool isSequenced() const noexcept { return seqCounter != nullptr; }

  /** @brief Check if task should run this tick (frequency gate). */
  [[nodiscard]] bool shouldRun() noexcept {
    if (config.freqD <= 1) {
      return true; // No decimation
    }
    ++holdCtr;
    if (holdCtr >= config.freqD) {
      holdCtr = 0;
      return true;
    }
    return false;
  }

  /** @brief Reset decimation counter (call at schedule start). */
  void resetHold() noexcept { holdCtr = 0; }
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_TASKCONFIG_HPP
