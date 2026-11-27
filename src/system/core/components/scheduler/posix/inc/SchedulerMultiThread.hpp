#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERMULTITHREAD_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERMULTITHREAD_HPP
/**
 * @file SchedulerMultiThread.hpp
 * @brief Multi-threaded scheduler with ThreadPool and TaskCtxPool.
 *
 * Supports multiple thread pools for heterogeneous workloads. Tasks are
 * routed to pools based on their poolId in TaskConfig.
 *
 * Note: Dependency handling is temporarily simplified. Full scheduler-side
 * dependency management will be added in a future update.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerBase.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskCtxPool.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace system_core {
namespace scheduler {

/// Pending-dispatch bound per pool (power-of-two ring slots). Far above any
/// healthy per-tick burst; the preflight burst check warns long before this.
inline constexpr std::size_t POOL_QUEUE_CAPACITY = 1024;

/**
 * @struct PoolSpec
 * @brief Specification for a thread pool in the scheduler.
 */
struct PoolSpec {
  std::string name{"default"};            ///< Pool name for logging/debugging.
  std::size_t numThreads{0};              ///< Worker count (0 = hardware_concurrency).
  apex::concurrency::PoolConfig config{}; ///< RT configuration for workers.
};

class SchedulerMultiThread : public SchedulerBase {
public:
  /**
   * @brief Construct multi-threaded scheduler with single default pool.
   * @param ffreq Fundamental frequency for scheduling (ticks per second).
   * @param logDir Directory for scheduler log file.
   * @param poolConfig Optional RT configuration for worker threads.
   */
  explicit SchedulerMultiThread(std::uint16_t ffreq, const std::filesystem::path& logDir,
                                const apex::concurrency::PoolConfig& poolConfig = {}) noexcept;

  /**
   * @brief Construct multi-threaded scheduler with multiple pools.
   * @param ffreq Fundamental frequency for scheduling (ticks per second).
   * @param logDir Directory for scheduler log file.
   * @param pools Pool specifications (empty = single default pool).
   */
  explicit SchedulerMultiThread(std::uint16_t ffreq, const std::filesystem::path& logDir,
                                std::vector<PoolSpec> pools) noexcept;

  ~SchedulerMultiThread() noexcept override;

  /**
   * @brief Execute the tasks due on a cycle through the thread pools.
   * @param cycle Full cycle count since the first dispatch.
   */
  [[nodiscard]] virtual Status executeTasksOnCycleMulti(std::uint64_t cycle) noexcept;

  /**
   * @brief Execute the tasks due on a tick index, for callers that dispatch
   *        every tick in order and keep no cycle count of their own.
   * @param tick Tick index within one second, [0, ffreq).
   */
  [[nodiscard]] Status executeTasksOnTickMulti(std::uint16_t tick) noexcept;

  /**
   * @brief Task completion callback (placeholder for future dependency support).
   */
  inline void onTaskComplete(SchedulableTask* /*task*/, std::uint16_t /*tick*/) noexcept {
    // Dependency handling will be added when scheduler-side dependencies are implemented
  }

  /**
   * @brief Check if any pool has running workers.
   */
  [[nodiscard]] bool threadsRunning() const noexcept;

  /**
   * @brief Get number of pools.
   */
  [[nodiscard]] std::size_t poolCount() const noexcept { return pools_.size(); }

  /**
   * @brief Check if any task has a period deadline violation.
   *
   * For HARD_PERIOD_COMPLETE mode: Returns true if any task that should
   * run this tick is still running from a previous invocation.
   */
  [[nodiscard]] bool hasPeriodViolation() const noexcept { return periodViolationsThisTick_ > 0; }

  /**
   * @brief Wait until no dispatched task is still running.
   *
   * Dispatch is asynchronous: the cycle entry returns once the tasks are
   * enqueued, so a caller that reads task outputs on the tick thread
   * would race the workers still writing them. This is the ownership
   * boundary for such a caller. It returns once every entry has released
   * its in-flight flag, which is also the acquire that publishes the
   * tasks' writes to this thread.
   *
   * Every in-flight entry holds it, whichever cycle dispatched it, and
   * every admitted invocation of it: a soft mode that does not skip can
   * queue a second invocation behind a running first, and the first's
   * completion does not release the second's ownership. A task
   * overrunning from an earlier cycle still owns its data; the period
   * violation the scheduler records for it is an account of the overrun,
   * not the end of that ownership.
   *
   * @param timeoutNs Budget for the wait from the moment of the call; 0
   *        polls once. The call is waitForInFlightTasksUntil with the
   *        instant taken at entry.
   * @return True once nothing is in flight, false on timeout or shutdown
   *         -- in which case the caller must not read task outputs or
   *         write task inputs, because a worker may still be doing so.
   */
  [[nodiscard]] bool waitForInFlightTasks(std::uint64_t timeoutNs) noexcept;

  /**
   * @brief Wait until nothing is in flight or a CLOCK_MONOTONIC instant
   *        passes.
   *
   * The deadline is an instant, not a duration, so a caller that chose it
   * as the end of a frame keeps that end whatever happens between
   * choosing and calling: time lost there is never granted again, and an
   * instant already past is a single poll. The wait is a short spin, then
   * sleeps in slices the caller does not see. Each sleep request is
   * capped at 250 us and at the remainder of the budget as of the clock
   * read before it, and no further request is issued once a read finds
   * the deadline passed. Preemption can delay both issuing a request and
   * its return, and when a sleep returns is the host's to decide, so the
   * deadline bounds the length of each request, not the instant a request
   * is made or the wait returns. Ownership and publication are as for
   * waitForInFlightTasks.
   *
   * @param deadlineNs CLOCK_MONOTONIC ns (apex::helpers::cpu::getMonotonicNs);
   *        0 or any instant already past polls once.
   * @return As waitForInFlightTasks.
   */
  [[nodiscard]] bool waitForInFlightTasksUntil(std::uint64_t deadlineNs) noexcept;

  /** @brief In-flight waits that hit their deadline since startup. Read
   *         from any thread; the tick thread increments it. */
  [[nodiscard]] std::size_t totalInFlightWaitTimeouts() const noexcept {
    return totalInFlightWaitTimeouts_.load(std::memory_order_acquire);
  }

  /**
   * @brief Whether every dispatched invocation has left its worker.
   *
   * True when no entry is in flight and no worker is inside a task call
   * or the record of its completion. Pending completion errors are then
   * logged, as at the start of a dispatch. From a true return until the
   * next dispatch no worker holds a task pointer or a task label this
   * scheduler handed out, so a task object may be replaced or destroyed.
   *
   * @return false while any invocation is queued, running or completing.
   * @note Call on the dispatching thread: it is the only source of new
   *       invocations. One pass over the entries and one load per pool; it
   *       does not wait. NOT RT-safe: logs when errors are pending.
   */
  [[nodiscard]] bool settled() noexcept;

  /** @brief Worker count per constructed pool (index = poolId). */
  [[nodiscard]] std::vector<std::uint16_t> poolWorkerCounts() const noexcept override;

  /** @brief Requested policy/priority/affinity per pool, from the specs. */
  [[nodiscard]] std::vector<PoolRequirementRowTlm> poolRequirements() const noexcept override;

  /** @brief Component name of the most recent period violator (nullptr = none). */
  [[nodiscard]] const char* lastViolationComponent() const noexcept override {
    return lastViolationComponent_.load(std::memory_order_acquire);
  }

  /** @brief Task UID of the most recent period violator. */
  [[nodiscard]] std::uint8_t lastViolationTaskUid() const noexcept override {
    return lastViolationTaskUid_.load(std::memory_order_acquire);
  }

  /**
   * @brief Check and clear sticky period violation flag.
   *
   * Returns true if any period violation occurred since the last call.
   * Atomically clears the flag after reading.
   */
  [[nodiscard]] bool checkAndClearPeriodViolation() noexcept {
    return periodViolationFlag_.exchange(false, std::memory_order_acq_rel);
  }

  void shutdown() noexcept;

  /**
   * @brief Enable skip-on-busy mode.
   *
   * When enabled, tasks that are still running from a previous dispatch
   * will be skipped instead of re-dispatched. This prevents task pileup.
   *
   * @param enable True to enable skip-on-busy, false to disable.
   */
  void setSkipOnBusy(bool enable) noexcept { skipOnBusy_ = enable; }

  /**
   * @brief Check if skip-on-busy mode is enabled.
   */
  [[nodiscard]] bool isSkipOnBusy() const noexcept { return skipOnBusy_; }

  /**
   * @brief Get total number of skipped invocations across all tasks.
   */
  [[nodiscard]] std::size_t totalSkipCount() const noexcept { return totalSkipCount_; }

  /** @brief Dispatches dropped (context exhaustion or ring rejection) since startup. */
  [[nodiscard]] std::size_t totalDispatchDrops() const noexcept { return totalDispatchDrops_; }

  using SchedulerBase::addTask;

  /**
   * @brief Add a task; once the pools exist, keep their contexts in step.
   *
   * The context pools are sized for the table's densest tick. A task
   * added after init(), or a table loaded after it, can make a denser
   * tick, so each addition sizes them again. Every other addTask()
   * overload and loadTprm() add through this one.
   *
   * @note NOT RT-safe: may allocate.
   */
  [[nodiscard]] Status addTask(SchedulableTask& task, const TaskConfig& config) noexcept override;

  /** @brief Contexts preallocated for a pool: its capacity when nothing is allocated on demand. */
  [[nodiscard]] std::size_t ctxPoolReserved(std::size_t poolIdx) const noexcept {
    return poolIdx < ctxPools_.size() ? ctxPools_[poolIdx]->reserved() : 0;
  }

  /**
   * @brief Whether a dispatch that finds its context pool empty allocates.
   *
   * A release build drops such a dispatch and counts it; a debug build
   * allocates a context so the shortfall shows in ctxPoolAllocatedCount().
   * This sets either behavior in either build. Call after init(), which
   * creates the pools and applies the build's default.
   */
  void setContextFallback(bool enable) noexcept {
    for (auto& pool : ctxPools_) {
      pool->setFallbackAlloc(enable);
    }
  }

  /**
   * @brief Contexts ever allocated by a pool (preallocation + any fallback).
   *
   * Debug builds track the count; release builds return 0. A value above
   * the preallocated capacity means fallback allocation fired -- the
   * capacity did not cover the schedule's dispatch demand.
   */
  [[nodiscard]] std::size_t ctxPoolAllocatedCount(std::size_t poolIdx) const noexcept {
    return poolIdx < ctxPools_.size() ? ctxPools_[poolIdx]->allocatedCount() : 0;
  }

  /* ----------------------------- Health Query Overrides ----------------------------- */

  [[nodiscard]] std::size_t totalPeriodViolations() const noexcept override {
    return totalPeriodViolations_;
  }
  [[nodiscard]] std::size_t periodViolationsThisTick() const noexcept override {
    return periodViolationsThisTick_;
  }
  [[nodiscard]] std::size_t totalSkips() const noexcept override { return totalSkipCount_; }
  [[nodiscard]] std::uint8_t numPools() const noexcept override {
    return static_cast<std::uint8_t>(pools_.size());
  }
  [[nodiscard]] bool skipOnBusyEnabled() const noexcept override { return skipOnBusy_; }

protected:
  /** @brief Delegate cycle execution to multi-threaded dispatch. */
  void executeCycle(std::uint64_t cycle) noexcept override {
    static_cast<void>(executeTasksOnCycleMulti(cycle));
  }

  /**
   * @brief Enqueue every entry on one slot in order.
   *
   * One body, four instantiations. The gated ones consult a multi-second
   * task's cadence per visit; the others, used for every slot that holds
   * no such task, test nothing per visit. The sequenced ones, used for a
   * slot that holds a sequenced task, first settle which entries run this
   * tick and then hand them over through enqueueSequencedSlot(); the
   * others enqueue as they go.
   */
  template <bool kGated, bool kSequenced>
  void dispatchSlot(const std::vector<std::size_t>& entryIndices, std::uint64_t cycle,
                    std::uint16_t tick, std::uint64_t dispatchNs, logs::SystemLog* lg,
                    int logLevel) noexcept;

  /**
   * @brief Enqueue the entries a sequenced slot runs this tick.
   *
   * Two steps. Every entry in slotDispatch_ goes to its pool in slot
   * order, a sequenced one with its wait unset, which holds its worker at
   * the start of the trampoline. Then each sequenced entry its pool
   * accepted is given its place among the accepted entries of its group:
   * it waits for those whose phase is lower than its own, and a task
   * alone in its group waits for nothing. An entry its pool refused is a
   * counted drop and takes no part: no task waits for it and the group's
   * counter wraps without it.
   */
  void enqueueSequencedSlot(std::uint16_t tick, std::uint64_t dispatchNs) noexcept;

  /**
   * @brief Initialize the scheduler. Called by base init().
   */
  [[nodiscard]] std::uint8_t doInit() noexcept override;

  /**
   * @brief Hand one entry to its pool.
   * @tparam kUnplaced Whether the entry is sequenced. Such a dispatch
   *         goes out with its wait unset and returns its context, null
   *         when the pool refused it, for enqueueSequencedSlot() to
   *         place. Any other returns nothing and stores no more than an
   *         unsequenced dispatch needs.
   */
  template <bool kUnplaced>
  std::conditional_t<kUnplaced, TaskCtx*, void> enqueueTask(TaskEntry* entry, std::uint16_t tick,
                                                            std::uint64_t dispatchNs) noexcept;

  /**
   * @brief Wait, on a worker, for the tick thread to place a sequenced dispatch.
   * @return The counter value to wait for, or 0 when the scheduler is stopping.
   */
  [[nodiscard]] static int awaitPlacement(const TaskCtx& ctx) noexcept;

  static std::uint8_t taskTrampoline(void* raw) noexcept;

private:
  void initPools(std::vector<PoolSpec> specs) noexcept;

  /// Bring each pool's contexts up to the table's densest tick plus two
  /// per worker. Does nothing before the pools exist.
  void sizeContextPools() noexcept;

  /// Log (at LOG_LEVEL's say) and discard the pools' pending completion
  /// errors. @return whether there were any.
  bool logCompletionErrors(logs::SystemLog* lg, int logLevel) noexcept;

  // Resolve PoolSpec.numThreads for each pending spec using the hierarchy:
  // caller-supplied non-zero > TPRM workersPerPool > hardware_concurrency().
  // Then call initPools(). Idempotent: a second call no-ops once pools exist.
  void resolveAndInitPoolsFromTprm() noexcept;

  // Pool specs captured at construction. Consumed by doInit() once the TPRM has
  // been loaded so workersPerPool can drive sizing.
  std::vector<PoolSpec> pendingSpecs_;

  /// Lock-free dispatch pools: producer-side enqueue is a CAS push + a
  /// waiter-gated futex wake, so the tick thread's per-task cost stays flat
  /// as the table grows. FIFO order is preserved by the MPMC ring.
  std::vector<std::unique_ptr<apex::concurrency::ThreadPoolLockFree>> pools_;
  std::vector<std::unique_ptr<TaskCtxPool>> ctxPools_;
  std::vector<std::string> poolNames_;
  bool poolStartFailed_{false}; ///< A pool reported no workers; doInit() refuses.
  std::filesystem::path logDir_;
  std::size_t periodViolationsThisTick_{0};      ///< Count of period violations detected this tick.
  std::size_t totalPeriodViolations_{0};         ///< Cumulative period violations since startup.
  std::atomic<bool> periodViolationFlag_{false}; ///< Sticky flag for period violations.
  std::atomic<bool> stopping_{false};            ///< Shutdown in progress; aborts phase waits.

  /// Identity of the most recent period violator, for shutdown-cause
  /// attribution (component name is a registry-lifetime pointer).
  std::atomic<const char*> lastViolationComponent_{nullptr};
  std::atomic<std::uint8_t> lastViolationTaskUid_{0};
  bool skipOnBusy_{false};            ///< Skip tasks still running (SKIP_ON_BUSY mode).
  std::size_t totalSkipCount_{0};     ///< Total skipped invocations across all tasks.
  std::size_t totalDispatchDrops_{0}; ///< Dropped dispatches (ctx exhaustion or ring reject).
  std::atomic<std::size_t> totalInFlightWaitTimeouts_{0}; ///< Waits that hit their deadline.
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERMULTITHREAD_HPP
