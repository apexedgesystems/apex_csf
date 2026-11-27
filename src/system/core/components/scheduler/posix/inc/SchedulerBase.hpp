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
 *   - Rates are scheduler-level: each entry's TaskCadence names the cycles it runs on
 */

#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IComponentResolver.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerTlm.hpp"
#include "src/system/core/components/scheduler/base/inc/IScheduler.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerPreflight.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/HostRequirements.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskCadence.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"

#include <cstdint>

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace system_core {
namespace scheduler {

struct TaskCtx;

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
   * Hands the full cycle count to executeCycle() and increments it. The
   * count advances through sleep as well, so a multi-second task keeps
   * its phase across one: dispatch is skipped, time is not. A sleeping
   * tick still notes its cycle, so a task admitted during sleep aligns
   * to the cycle that is current, not to the last one dispatched.
   *
   * @note RT-safe: delegates to derived implementation.
   */
  void tick() noexcept override {
    if (!sleeping_.load(std::memory_order_acquire)) {
      executeCycle(tickCount_);
    } else {
      static_cast<void>(beginDispatchCycle(tickCount_));
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

  /**
   * @brief Static feasibility verdicts for the loaded task table.
   * @return Result of the most recent preflight (all-PASS default until
   *         a table is analyzed).
   */
  [[nodiscard]] const TablePreflight& tablePreflight() const noexcept { return tablePreflight_; }

  /**
   * @brief Run the static table preflight and log its verdict section.
   *
   * Called automatically at TPRM table load; public so directly-built
   * tables (addTask path) can be analyzed too.
   * @note Config-time only.
   */
  void runTablePreflight() noexcept;

  /**
   * @brief Emit the stored table verdicts to the component log.
   *
   * Split from the analysis because the table can load before the log
   * exists: the analysis latches its verdicts either way, and emission
   * retries at init so a boot never silently drops the section.
   */
  void logTablePreflight() noexcept;

  /**
   * @brief Refresh the per-task stats snapshot from the live counters.
   * @note Command/INSPECT context; not on the tick path.
   */
  void populateTaskStatsTlm() noexcept;

  /**
   * @brief Execute every task once + repeats, pre-clock, measuring cost.
   * @param repeats Additional timed executions after the cold first run.
   * @return true if the census ran; false if refused (ticks have
   *         already executed -- census is strictly pre-clock).
   *
   * Tasks run sequentially in table order (phase order for sequenced
   * groups, which table preflight validates), directly through
   * execute() -- the census measures task bodies, not pool plumbing.
   * Cold first-run cost (lazy inits: GPU module load, NVML, page
   * faults) is reported separately from the steady estimate, each
   * against the task's period budget.
   * @note Config-time only.
   */
  bool runTaskCensus(std::uint8_t repeats) noexcept;

  /**
   * @brief Names of tasks currently marked in-flight, for loss events.
   * @param cap Maximum names listed.
   * @return "Component:taskUid" list, comma-separated ("none" if empty),
   *         or TABLE_CHANGING_TEXT when the table is being changed.
   *
   * Safe from any thread while the scheduler runs: it reads the table
   * under a TableRead, so a table change (a reload, a component swap, an
   * added task) on the thread that owns the table waits for it, and a
   * call that arrives during a change returns at once without touching
   * the table.
   *
   * @note Diagnostic path only (allocates); never on the tick path.
   */
  [[nodiscard]] std::string inFlightSummary(std::size_t cap) const noexcept;

  /// What inFlightSummary() returns when it finds the table being changed.
  static constexpr const char* TABLE_CHANGING_TEXT = "unavailable (task table changing)";

  /**
   * @class TableRead
   * @brief A read of the task table from a thread that does not own it.
   *
   * The thread that dispatches is the one that changes the table (adds a
   * task, loads a table, rebinds a component's tasks), and it needs no
   * guard for its own reads. Any other thread reads the entries only
   * while it holds a TableRead that tests true. Taking one never blocks:
   * when a change is in progress it tests false and the reader goes
   * without. A change waits until the reads that began before it have
   * ended, so a reader that holds one sees entries that stay where they
   * are and as they are.
   *
   * @note RT-safe: two atomic operations to take, one to release.
   */
  class TableRead {
  public:
    explicit TableRead(const SchedulerBase& sched) noexcept : sched_(sched) {
      // Announce, then look: a change announces itself and then looks
      // for readers, so one of the two always sees the other.
      sched_.tableReaders_.fetch_add(1U, std::memory_order_seq_cst);
      held_ = !sched_.tableChanging_.load(std::memory_order_seq_cst);
      if (!held_) {
        sched_.tableReaders_.fetch_sub(1U, std::memory_order_release);
      }
    }
    ~TableRead() {
      if (held_) {
        sched_.tableReaders_.fetch_sub(1U, std::memory_order_release);
      }
    }
    TableRead(const TableRead&) = delete;
    TableRead& operator=(const TableRead&) = delete;
    TableRead(TableRead&&) = delete;
    TableRead& operator=(TableRead&&) = delete;

    /// True when the entries may be read until this object is destroyed.
    [[nodiscard]] explicit operator bool() const noexcept { return held_; }

  private:
    const SchedulerBase& sched_;
    bool held_{false};
  };

  /**
   * @brief Worker count per constructed pool (index = poolId).
   * @return One entry per pool; single-threaded default reports {1}.
   */
  [[nodiscard]] virtual std::vector<std::uint16_t> poolWorkerCounts() const noexcept {
    return {1U};
  }

  /**
   * @brief Requested host configuration per pool (policy/priority/affinity).
   * @return One row per pool; single-threaded default: one OTHER pool.
   */
  [[nodiscard]] virtual std::vector<PoolRequirementRowTlm> poolRequirements() const noexcept {
    PoolRequirementRowTlm row{};
    row.workers = 1U;
    return {row};
  }

  /**
   * @brief Refresh the published requirements snapshot.
   * @note Config-time; called at table load.
   */
  void populateRequirementsTlm() noexcept;

  /** @brief Component name of the most recent period violator (nullptr = none). */
  [[nodiscard]] virtual const char* lastViolationComponent() const noexcept { return nullptr; }

  /** @brief Task UID of the most recent period violator (0 = none). */
  [[nodiscard]] virtual std::uint8_t lastViolationTaskUid() const noexcept { return 0U; }

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
  [[nodiscard]] system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override;

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
   * @brief Bind every entry of a component to a replacement instance.
   *
   * Used during component hot-swap. For each entry matching the fullUid
   * the replacement must provide the task with the entry's taskUid, and,
   * for a sequenced entry, a sequence group that holds that task at the
   * entry's phase and maximum phase. Entries that shared a sequence
   * counter must resolve to one counter again, and entries of different
   * counters to different ones: a sequence is neither split nor merged.
   * The entries change only if every one resolves: the task pointer, the sequence counter and the
   * component name are then all the replacement's, so no entry is left
   * pointing into the instance it replaced.
   *
   * @param fullUid Component fullUid to match.
   * @param newComponent Instance to bind the entries to.
   * @return Number of entries bound; 0, with nothing changed, when an
   *         entry has no counterpart in the replacement.
   *
   * @note NOT RT-safe: Control-plane only; allocates. Call on the
   *       dispatching thread with every dispatched task settled.
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
   * @brief Execute the tasks due on a cycle.
   * @param cycle Full cycle count since the first dispatch; the table
   *        index is `cycle mod ffreq`.
   * @note Pure virtual: SchedulerMultiThread dispatches to thread pools,
   *       SchedulerSingleThread executes sequentially.
   */
  virtual void executeCycle(std::uint64_t cycle) noexcept = 0;

  /** @brief Tick counter (incremented by tick()). */
  std::uint64_t tickCount_{0};

  /**
   * @brief Note the cycle a dispatch is about to serve and return its table index.
   *
   * Every dispatch entry calls this first, before any early return, so
   * the cycle a late addTask aligns to is always the one after the last
   * served, whether or not that cycle had work. On the expected cycle the
   * index is carried forward without division; a skipped or repeated
   * cycle recomputes it.
   *
   * @note RT-safe: compares, increments and one store; a division only
   *       when the cycle is not the expected one.
   */
  [[nodiscard]] std::uint16_t beginDispatchCycle(std::uint64_t cycle) noexcept {
    std::uint16_t index = expectedTick_;
    if (cycle != nextDispatchCycle_) {
      index = (ffreq_ == 0U) ? 0U : static_cast<std::uint16_t>(cycle % ffreq_);
    }
    nextDispatchCycle_ = cycle + 1;
    expectedTick_ = (static_cast<std::uint32_t>(index) + 1U >= ffreq_)
                        ? static_cast<std::uint16_t>(0U)
                        : static_cast<std::uint16_t>(index + 1U);
    return index;
  }

  /**
   * @brief Cycle for a caller that supplies only a tick index.
   *
   * For callers that dispatch every tick in order: the next cycle, at or
   * after the one expected, whose index is `tick`. Such a caller cannot
   * report whole seconds it skipped, so a caller that keeps a cycle count
   * passes it to the cycle entry instead.
   *
   * @note RT-safe: a compare on the expected path; one division otherwise.
   */
  [[nodiscard]] std::uint64_t cycleForTick(std::uint16_t tick) const noexcept {
    if (tick == expectedTick_ || ffreq_ == 0U) {
      return nextDispatchCycle_;
    }
    const std::uint64_t BASE = nextDispatchCycle_ - (nextDispatchCycle_ % ffreq_);
    std::uint64_t cycle = BASE + tick;
    if (cycle < nextDispatchCycle_) {
      cycle += ffreq_;
    }
    return cycle;
  }

  /** @brief Cycle the next dispatch will be given; a late addTask aligns to it. */
  std::uint64_t nextDispatchCycle_{0};
  /** @brief nextDispatchCycle_ mod ffreq_, carried so the expected path divides nothing. */
  std::uint16_t expectedTick_{0};

  /**
   * @brief Each entry's cadence, index-aligned with entries_.
   *
   * Kept beside the entries rather than inside them so the per-visit walk
   * over denominator-one tasks touches no more of each entry than it did
   * before cadences existed; only a gated entry's cadence is read.
   */
  std::vector<TaskCadence> cadences_{};

  /**
   * @brief Per slot: whether any gated entry is placed on it.
   *
   * A slot with none dispatches with the plain loop, which tests nothing
   * per visit; only a slot that holds a multi-second task pays the
   * per-visit cadence check. Index-aligned with schedule_.
   */
  std::vector<std::uint8_t> slotHasGated_{};

  /**
   * @brief Per slot: whether any sequenced entry is placed on it.
   *
   * A sequenced task waits for the tasks of its group that are dispatched
   * on the same tick at an earlier phase, so a slot that holds one is
   * dispatched in two steps: which entries run this tick, then each
   * sequenced entry's place among them. A slot with none keeps the single
   * loop. Index-aligned with schedule_.
   */
  std::vector<std::uint8_t> slotHasSequenced_{};

  /// End of a chain through slotDispatch_.
  static constexpr std::uint32_t NO_SLOT_DISPATCH = 0xFFFFFFFFU;

  /**
   * @brief A sequenced entry a pool accepted on the tick being dispatched.
   *
   * AHEAD is how many accepted entries of its group it follows, as far
   * as that is known when it is taken: exact while the group's entries
   * come in ascending phase order. NEXT chains the accepted entries of
   * one group, newest first, as indices into slotDispatch_, for the
   * group whose entries do not.
   */
  struct SlotDispatch {
    TaskEntry* entry{nullptr};
    TaskCtx* ctx{nullptr};
    std::uint32_t next{NO_SLOT_DISPATCH};
    std::uint32_t ahead{0};
  };

  /**
   * @brief The sequenced entries accepted on the tick being dispatched, in slot order.
   *
   * Tick thread only. Its capacity follows the largest slot, set where
   * entries are placed, so a dispatch allocates nothing.
   */
  std::vector<SlotDispatch> slotDispatch_{};

  /**
   * @brief One sequence group: its entries, and what the dispatch of the
   *        current tick has found of it.
   *
   * The fields after ENTRIES belong to the tick thread and to the tick
   * whose number STAMP holds; a group met for the first time on a tick
   * has them set afresh.
   */
  struct SeqGroupState {
    std::vector<std::size_t> entries;     ///< The group's entries, by index into entries_.
    std::uint64_t stamp{0};               ///< Sequenced dispatch the fields below are of.
    std::uint32_t head{NO_SLOT_DISPATCH}; ///< Newest accepted entry in slotDispatch_.
    std::uint32_t together{0};            ///< Entries of the group accepted so far.
    std::uint32_t aheadOfPhase{0};        ///< Accepted entries below the phase last accepted.
    int lastPhase{0};                     ///< Phase of the entry last accepted.
    bool busy{false};                     ///< A task of an earlier tick was still running.
    bool unordered{false};                ///< An entry came after one of a higher phase.
  };

  /// The sequence groups, by TaskEntry::seqGroup.
  std::vector<SeqGroupState> seqGroups_{};

  /// Number of the sequenced dispatch under way; tick thread only.
  std::uint64_t seqStamp_{0};

  /**
   * @brief Recompute slotHasSequenced_, each entry's group index and
   *        seqGroups_ from the entries' sequencing.
   */
  void markSequencedSlots() noexcept;

  /**
   * @class TableChange
   * @brief A change to the task table, on the thread that owns it.
   *
   * Held by everything that moves, grows, clears or rewrites the entries.
   * It turns new TableRead requests away and waits for the reads already
   * begun, which are bounded walks of the table. Nests on its thread, so
   * a table load that adds tasks holds one change throughout.
   *
   * @note NOT RT-safe: waits for readers.
   */
  class TableChange {
  public:
    explicit TableChange(SchedulerBase& sched) noexcept;
    ~TableChange();
    TableChange(const TableChange&) = delete;
    TableChange& operator=(const TableChange&) = delete;
    TableChange(TableChange&&) = delete;
    TableChange& operator=(TableChange&&) = delete;

  private:
    SchedulerBase& sched_;
  };

  mutable std::atomic<std::uint32_t> tableReaders_{0}; ///< TableRead objects that test true.
  std::atomic<bool> tableChanging_{false};             ///< A TableChange is held.
  std::uint32_t tableChangeDepth_{0};                  ///< Nested changes; owning thread only.

  /** @brief All registered task entries (scheduler owns these). */
  std::vector<TaskEntry> entries_{};

  /// Component names the entries point to. An entry's name outlives the
  /// component that supplied it (a violation report may still be naming
  /// it), so the scheduler keeps its own copy of each distinct name.
  std::deque<std::string> componentNames_{};

  /// The scheduler's own copy of NAME (nullptr for nullptr).
  [[nodiscard]] const char* internComponentName(const char* name) noexcept;

  /** @brief Table of tick index -> vector of entry indices scheduled at that tick. */
  std::vector<std::vector<std::size_t>> schedule_{};
  TablePreflight tablePreflight_{};      ///< Verdicts from the last table analysis.
  bool tablePreflightPending_{false};    ///< Analyzed but not yet emitted to the log.
  SchedulerTaskStatsTlm taskStatsTlm_{}; ///< Snapshot for GET_TASK_STATS/INSPECT.
  SchedulerCensusTlm censusTlm_{};       ///< Pre-clock census report.
  SchedulerRequirementsTlm requirementsTlm_{};
  system_component::HostRequirements
      hostRequirements_{}; ///< Generic published record. ///< Declared host requirements.

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
