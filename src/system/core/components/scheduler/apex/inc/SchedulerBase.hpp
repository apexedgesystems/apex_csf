#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERBASE_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERBASE_HPP
/**
 * @file SchedulerBase.hpp
 * @brief Abstract base for task schedulers with TaskConfig-based API.
 *
 * Architecture:
 *   - Inherits lifecycle/state/logging from SystemComponentBase
 *   - Owns TaskEntry objects (task pointer + config + runtime state)
 *   - Maintains tick->tasks schedule table for execution
 *
 * Design:
 *   - Tasks are minimal (just callable + label)
 *   - Scheduler owns all configuration via TaskConfig
 *   - Frequency decimation happens at scheduler level (TaskEntry::shouldRun)
 */

#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IComponentResolver.hpp"
#include "src/system/core/components/scheduler/apex/inc/SchedulerStatus.hpp"
#include "src/system/core/components/scheduler/apex/inc/SchedulerTlm.hpp"
#include "src/system/core/components/scheduler/base/inc/IScheduler.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/system/core/components/scheduler/apex/inc/SchedulerData.hpp"
#include "src/system/core/components/scheduler/apex/inc/TaskConfig.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"

#include <cstdint>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace system_core {
namespace scheduler {

// Import types from schedulable namespace
using ::system_core::schedulable::SchedulableTask;
using ::system_core::schedulable::SchedulableTaskBase;
using ::system_core::schedulable::SeqInfo;
using ::system_core::schedulable::SequenceGroup;

// Re-export binding helpers from schedulable for backward compatibility
using ::system_core::schedulable::bindFreeFunction;
using ::system_core::schedulable::bindLambda;
using ::system_core::schedulable::bindMember;

// Re-export priority/policy constants from TaskConfig for user convenience
// (These are now defined in scheduler, not schedulable)

/** @brief Scheduler log filename constant. */
static constexpr std::string_view SCHED_LOG_FN = "scheduler.log";

/**
 * @class SchedulerBase
 * @brief Base class for task scheduling and management.
 *
 * Maintains a fundamental frequency in ticks-per-second and a tick->tasks table.
 * Owns TaskEntry objects that combine task pointers with scheduling configuration.
 * Tasks are non-owning pointers; task lifetime is managed by the caller.
 */
class SchedulerBase : public system_component::CoreComponentBase, public IScheduler {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component type identifier (1 = Scheduler, system component range 1-100).
  static constexpr std::uint16_t COMPONENT_ID = 1;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "Scheduler";

  /** @brief Get component type identifier. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct with fundamental frequency (ticks/sec).
   * @param ffreq Fundamental frequency for scheduling (ticks per second).
   */
  explicit SchedulerBase(std::uint16_t ffreq) noexcept : ffreq_(ffreq) {
    setConfigured(true); // Simple component - no params file needed
  }

  /** @brief Virtual destructor. */
  ~SchedulerBase() override = default;

  /** @brief Module label (string literal). */
  [[nodiscard]] const char* label() const noexcept override { return "SCHEDULER"; }

  /** @brief Scheduler-typed view of the last status (casts base status()). */
  [[nodiscard]] Status schedStatus() const noexcept { return static_cast<Status>(status()); }

  /** @brief Fundamental frequency (ticks per second). */
  [[nodiscard]] std::uint16_t fundamentalFreq() const noexcept override { return ffreq_; }

  /** @brief Update fundamental frequency. Must be called before addTask/loadTprm. */
  void setFundamentalFreq(std::uint16_t ffreq) noexcept { ffreq_ = ffreq; }

  /* ----------------------------- IScheduler Interface ----------------------------- */

  /**
   * @brief Execute one scheduler tick (IScheduler interface).
   *
   * Computes current tick index, delegates to executeTick(), and increments
   * the tick counter. When sleeping, tick counter still advances (time
   * awareness) but task dispatch is skipped.
   *
   * @note RT-safe: delegates to derived implementation.
   */
  void tick() noexcept override {
    if (!sleeping_.load(std::memory_order_acquire)) {
      const std::uint16_t TICK_IDX = static_cast<std::uint16_t>(tickCount_ % ffreq_);
      executeTick(TICK_IDX);
    }
    ++tickCount_;
  }

  /** @brief Get current tick count (IScheduler interface). */
  [[nodiscard]] std::uint64_t tickCount() const noexcept override { return tickCount_; }

  /** @brief Get number of registered tasks (IScheduler interface). */
  [[nodiscard]] std::size_t taskCount() const noexcept override { return entries_.size(); }

  /* ----------------------------- Sleep Mode ----------------------------- */

  /**
   * @brief Enter sleep mode: clock keeps ticking but tasks are not dispatched.
   *
   * Use for power conservation or safe parking. The clock thread, watchdog,
   * and external I/O remain active. Wake with wake() to resume task dispatch.
   *
   * @note RT-safe: single atomic store.
   */
  void sleep() noexcept { sleeping_.store(true, std::memory_order_release); }

  /**
   * @brief Exit sleep mode: resume normal task dispatch.
   * @note RT-safe: single atomic store.
   */
  void wake() noexcept { sleeping_.store(false, std::memory_order_release); }

  /**
   * @brief Check if scheduler is in sleep mode.
   * @return true if sleeping (clock ticks but tasks are not dispatched).
   * @note RT-safe: single atomic load.
   */
  [[nodiscard]] bool isSleeping() const noexcept {
    return sleeping_.load(std::memory_order_acquire);
  }

  /* ----------------------------- Command Handling ----------------------------- */

  /**
   * @brief Handle commands dispatched to the scheduler.
   *
   * Component-specific opcodes:
   *   - 0x0100: GET_HEALTH - Returns SchedulerHealthTlm (32 bytes).
   *
   * Delegates to base class for common opcodes (0x0080-0x0082).
   *
   * @param opcode Command opcode.
   * @param payload Command payload.
   * @param response Output buffer for response data.
   * @return Status code (0 = success).
   */
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override;

  /* ----------------------------- Health Query Helpers ----------------------------- */

  /** @brief Cumulative period deadline violations. Override in multi-thread. */
  [[nodiscard]] virtual std::size_t totalPeriodViolations() const noexcept { return 0; }

  /** @brief Period violations in the most recent tick. Override in multi-thread. */
  [[nodiscard]] virtual std::size_t periodViolationsThisTick() const noexcept { return 0; }

  /** @brief Cumulative skip-on-busy count. Override in multi-thread. */
  [[nodiscard]] virtual std::size_t totalSkips() const noexcept { return 0; }

  /** @brief Number of thread pools. Override in multi-thread. */
  [[nodiscard]] virtual std::uint8_t numPools() const noexcept { return 1; }

  /** @brief Skip-on-busy mode enabled. Override in multi-thread. */
  [[nodiscard]] virtual bool skipOnBusyEnabled() const noexcept { return false; }

  /* ----------------------------- Component Resolver ----------------------------- */

  /**
   * @brief Set component resolver for task wiring during TPRM loading.
   * @param resolver Pointer to component resolver (owned by caller).
   * @note Call before loadTprm() to enable task lookup by fullUid.
   */
  void setComponentResolver(system_component::IComponentResolver* resolver) noexcept {
    componentResolver_ = resolver;
  }

  /**
   * @brief Get component resolver.
   * @return Pointer to resolver, or nullptr if not set.
   */
  [[nodiscard]] system_component::IComponentResolver* componentResolver() const noexcept {
    return componentResolver_;
  }

  /* ----------------------------- TPRM Loading ----------------------------- */

  /**
   * @brief Load scheduler TPRM and wire tasks.
   *
   * Reads scheduler TPRM from tprmDir, parses task entries, looks up components
   * via componentResolver, and adds tasks to the scheduler.
   *
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success, false on failure.
   * @note Requires componentResolver to be set before calling.
   * @note NOT RT-safe: file I/O, vector resizing.
   */
  [[nodiscard]] bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

protected:
  /**
   * @brief Boot-time initialization hook for schedulers.
   * @return Status code (SUCCESS on success).
   * @note Called by base init() after configuration check.
   * @note Derived classes implement actual initialization logic here.
   */
  [[nodiscard]] uint8_t doInit() noexcept override = 0;

public:
  /**
   * @brief Add a task to the schedule with configuration.
   *
   * @param task   Task to schedule (non-owning pointer, caller manages lifetime).
   * @param config Scheduling configuration (frequency, priority, poolId).
   * @return Scheduler::Status result.
   *
   * The scheduler stores a TaskEntry combining the task pointer with the config.
   * Frequency decimation is handled by the scheduler during execution.
   *
   * @note NOT RT-safe: may resize internal vectors.
   */
  [[nodiscard]] virtual Status addTask(SchedulableTask& task, const TaskConfig& config) noexcept;

  /**
   * @brief Add a task with individual parameters (convenience overload).
   *
   * @param task     Task to schedule (non-owning).
   * @param tfreqn   Task frequency numerator (Hz numerator).
   * @param tfreqd   Task frequency denominator (>=1).
   * @param offset   Offset in ticks [0, periodTicks).
   * @param priority Logical priority [-128, 127].
   * @param poolId   Thread pool ID (0 = default pool).
   * @return Scheduler::Status result.
   *
   * @note NOT RT-safe: may resize internal vectors.
   */
  [[nodiscard]] virtual Status addTask(SchedulableTask& task, std::uint16_t tfreqn,
                                       std::uint16_t tfreqd = 1, std::uint16_t offset = 0,
                                       TaskPriority priority = PRIORITY_NORMAL,
                                       std::uint8_t poolId = 0) noexcept;

  /**
   * @brief Add a task with configuration and optional sequencing.
   *
   * @param task    Task to schedule (non-owning pointer).
   * @param config  Scheduling configuration.
   * @param seq     Optional sequence group (nullptr = no sequencing).
   * @param fullUid Owner component's full UID (for contention detection, 0 = unknown).
   * @param taskUid Task UID within component (for registry registration, 0 = unknown).
   * @param componentName Owner component name (for logging, nullptr = unknown).
   * @return Scheduler::Status result.
   *
   * If seq is provided, queries it for task's sequencing info and stores
   * in the TaskEntry for use during execution.
   *
   * @note NOT RT-safe: may resize internal vectors.
   */
  [[nodiscard]] virtual Status addTask(SchedulableTask& task, const TaskConfig& config,
                                       SequenceGroup* seq, std::uint32_t fullUid = 0,
                                       std::uint8_t taskUid = 0,
                                       const char* componentName = nullptr) noexcept;

  /**
   * @brief Get task entry for a scheduled task.
   * @param task Task to look up.
   * @return Pointer to TaskEntry, or nullptr if not found.
   *
   * @note RT-safe: O(n) linear search over entries.
   */
  [[nodiscard]] TaskEntry* getEntry(SchedulableTask& task) noexcept;
  [[nodiscard]] const TaskEntry* getEntry(const SchedulableTask& task) const noexcept;

  /**
   * @brief Replace task pointers for all entries belonging to a component.
   *
   * Used during component hot-swap: after loading a new .so, re-resolves
   * task pointers by calling newComponent->taskByUid(taskUid) for each
   * entry matching the given fullUid.
   *
   * @param fullUid Component fullUid to match.
   * @param newComponent New component instance to resolve tasks from.
   * @return Number of tasks successfully replaced.
   *
   * @note NOT RT-safe: Control-plane only (called while component is locked).
   */
  std::uint8_t replaceComponentTasks(std::uint32_t fullUid,
                                     SystemComponentBase& newComponent) noexcept;

  /**
   * @brief Get all task entries (read-only).
   * @return Const reference to task entry vector.
   *
   * Provides access to all scheduled tasks for iteration (e.g., registry registration).
   *
   * @note RT-safe: returns const reference.
   */
  [[nodiscard]] const std::vector<TaskEntry>& entries() const noexcept { return entries_; }

  /**
   * @brief Clear all task entries and schedule table.
   *
   * Called at the start of loadTprm() to prevent duplicate entries
   * when reloading at runtime. Must only be called while the component
   * is locked (scheduler skips tasks from locked components).
   *
   * @note NOT RT-safe: modifies entries_ and schedule_ vectors.
   */
  void clearTasks() noexcept;

  /**
   * @brief Log TPRM configuration to scheduler log.
   * @param source Source path or "defaults" if using defaults.
   * @param numPools Number of thread pools.
   * @param workersPerPool Workers per pool.
   * @param numTasks Number of task entries.
   * @note Call after init() to ensure log is initialized.
   *
   * @note NOT RT-safe: logging I/O.
   */
  void logTprmConfig(const std::string& source, std::uint8_t numPools, std::uint8_t workersPerPool,
                     std::uint8_t numTasks) noexcept;

  /**
   * @brief Export scheduler state to SDAT binary format.
   *
   * Writes task scheduling configuration and per-tick schedule to a binary file
   * for external analysis by C2 systems.
   *
   * @param dbDir Directory to write sched.rdat file.
   * @return Status::SUCCESS on success, error status on failure.
   * @note NOT RT-safe: file I/O.
   */
  [[nodiscard]] Status exportSchedule(const std::filesystem::path& dbDir) noexcept;

protected:
  /**
   * @brief Initialize scheduler log.
   * @param logDir Directory for scheduler log file.
   */
  void initSchedulerLog(const std::filesystem::path& logDir) noexcept;
  /**
   * @brief Log comprehensive schedule layout to scheduler log.
   * @param modeDescription Execution mode description (e.g., "Sequential (single-threaded)").
   */
  void logScheduleLayout(std::string_view modeDescription) noexcept;

  /**
   * @brief Execute tasks for a specific tick index.
   * @param tick Tick index within one fundamental period [0, ffreq).
   * @note Pure virtual: SchedulerMultiThread dispatches to thread pools,
   *       SchedulerSingleThread executes sequentially.
   */
  virtual void executeTick(std::uint16_t tick) noexcept = 0;

  /** @brief Tick counter (incremented by tick()). */
  std::uint64_t tickCount_{0};

  /** @brief All registered task entries (scheduler owns these). */
  std::vector<TaskEntry> entries_{};

  /** @brief Table of tick index -> vector of entry indices scheduled at that tick. */
  std::vector<std::vector<std::size_t>> schedule_{};

  /** @brief Fundamental frequency in ticks per second. */
  std::uint16_t ffreq_{0};

  /** @brief Path to component log file. */
  std::filesystem::path componentLogPath_;

  /** @brief Component resolver for task lookup during TPRM loading. */
  system_component::IComponentResolver* componentResolver_{nullptr};

  /** @brief Full TPRM binary for INSPECT readback (header + task entries). */
  std::vector<std::uint8_t> tprmRaw_{};

  /** @brief Sleep mode flag: when true, tick() skips task dispatch but still increments counter. */
  std::atomic<bool> sleeping_{false};

  /** @brief Health snapshot for INSPECT OUTPUT readback (populated on GET_HEALTH). */
  SchedulerHealthTlm healthTlm_{};
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERBASE_HPP
