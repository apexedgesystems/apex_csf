/**
 * @file SchedulerMultiThread.cpp
 * @brief Multi-threaded scheduler with multi-pool support.
 *
 * Supports multiple thread pools for heterogeneous workloads. Tasks are
 * routed to pools based on their poolId in TaskConfig.
 *
 * Note: Dependency handling is temporarily simplified. Full dependency
 * support will be re-added when scheduler-side dependency management
 * is implemented.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskCtxPool.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <cstdint>

#include <algorithm>
#include <thread>

#include <fmt/core.h>

namespace system_core {
namespace scheduler {

// Import sequencing helpers from schedulable namespace
using schedulable::advancePhase;
using schedulable::waitForPhase;

/* ----------------------------- Construction ----------------------------- */

SchedulerMultiThread::SchedulerMultiThread(std::uint16_t ffreq, const std::filesystem::path& logDir,
                                           const apex::concurrency::PoolConfig& poolConfig) noexcept
    : SchedulerBase(ffreq), logDir_(logDir) {
  // Pool creation is deferred until doInit() so the TPRM workersPerPool value can
  // drive the worker count. PoolSpec.numThreads = 0 means "use TPRM if loaded,
  // else hardware_concurrency()".
  pendingSpecs_.push_back({"default", 0, poolConfig});
}

SchedulerMultiThread::SchedulerMultiThread(std::uint16_t ffreq, const std::filesystem::path& logDir,
                                           std::vector<PoolSpec> pools) noexcept
    : SchedulerBase(ffreq), logDir_(logDir) {
  if (pools.empty()) {
    pools.push_back({"default", 0, {}});
  }
  // Stash; doInit() applies TPRM overrides + spawns the worker pools.
  pendingSpecs_ = std::move(pools);
}

void SchedulerMultiThread::resolveAndInitPoolsFromTprm() noexcept {
  // Idempotent: subsequent calls (e.g. after a TPRM reload that does not resize
  // pools) are no-ops once worker pools exist.
  if (!pools_.empty()) {
    return;
  }
  if (pendingSpecs_.empty()) {
    return; // shouldn't happen -- the constructor always seeds one spec
  }

  // Pull workersPerPool from the loaded TPRM header if present. tprmRaw_ is
  // populated by SchedulerBase::loadTprm before init runs. Pre-TPRM (e.g. unit
  // tests that construct without loading) we skip this and let each
  // PoolSpec.numThreads = 0 fall through to hardware_concurrency() inside
  // initPools -- existing behavior is preserved.
  std::uint8_t tprmWorkersPerPool = 0;
  if (tprmRaw_.size() >= sizeof(SchedulerTprmHeader)) {
    const auto* header = reinterpret_cast<const SchedulerTprmHeader*>(tprmRaw_.data());
    tprmWorkersPerPool = header->workersPerPool;
  }

  // Per-pool override hierarchy:
  //   1. caller-supplied PoolSpec.numThreads > 0 -> use it (explicit override)
  //   2. TPRM workersPerPool > 0                 -> use it (declared config)
  //   3. else fall through to hardware_concurrency() (initPools default)
  for (auto& spec : pendingSpecs_) {
    if (spec.numThreads == 0 && tprmWorkersPerPool > 0) {
      spec.numThreads = tprmWorkersPerPool;
    }
  }

  // Keep the resolved specs: they are the published requirements
  // (poolRequirements reads them after init).
  initPools(pendingSpecs_);
}

void SchedulerMultiThread::initPools(std::vector<PoolSpec> specs) noexcept {
  pools_.reserve(specs.size());
  ctxPools_.reserve(specs.size());
  poolNames_.reserve(specs.size());

  for (auto& spec : specs) {
    const std::size_t NUM_THREADS =
        spec.numThreads > 0 ? spec.numThreads : std::thread::hardware_concurrency();

    // Queue capacity bounds pending dispatches per pool. The preflight burst
    // check warns orders of magnitude before this bound; hitting it means the
    // system has already failed to drain and the enqueue is dropped loudly.
    pools_.emplace_back(std::make_unique<apex::concurrency::ThreadPoolLockFree>(
        NUM_THREADS, POOL_QUEUE_CAPACITY, spec.config));

    // Each pool gets its own context pool (sized for its workers)
    ctxPools_.emplace_back(std::make_unique<TaskCtxPool>());

    poolNames_.push_back(std::move(spec.name));
  }
}

SchedulerMultiThread::~SchedulerMultiThread() noexcept { shutdown(); }

void SchedulerMultiThread::shutdown() noexcept {
  // One-shot: the destructor calls shutdown() again after an explicit
  // shutdown, and by then the sequence groups (whose counters the
  // sentinel drive writes) may legitimately be gone -- their contract
  // ends at the FIRST shutdown, not at scheduler destruction.
  if (!stopping_.exchange(true, std::memory_order_acq_rel)) {
    // Release parked phase waiters. atomic wait absorbs notifies while
    // the value is unchanged, so a bare notify cannot free a waiter --
    // the counter must actually change. Drive every group counter to
    // the terminal shutdown value: waiters wake on the change, observe
    // the stopping flag (ordered before this store), and abort.
    for (const auto& entry : entries_) {
      if (entry.seqCounter != nullptr) {
        entry.seqCounter->store(schedulable::SEQ_SHUTDOWN, std::memory_order_release);
        apex::compat::atom::notifyAll(*entry.seqCounter);
      }
    }
  }

  for (auto& pool : pools_) {
    if (pool) {
      pool->shutdown();
    }
  }
}

std::vector<std::uint16_t> SchedulerMultiThread::poolWorkerCounts() const noexcept {
  std::vector<std::uint16_t> counts;
  counts.reserve(pools_.size());
  for (const auto& pool : pools_) {
    counts.push_back(pool ? static_cast<std::uint16_t>(pool->workerCount())
                          : static_cast<std::uint16_t>(0U));
  }
  return counts;
}

std::vector<PoolRequirementRowTlm> SchedulerMultiThread::poolRequirements() const noexcept {
  std::vector<PoolRequirementRowTlm> rows;
  rows.reserve(pendingSpecs_.size());
  for (std::size_t i = 0; i < pendingSpecs_.size(); ++i) {
    const auto& spec = pendingSpecs_[i];
    PoolRequirementRowTlm row{};
    row.poolId = static_cast<std::uint8_t>(i);
    row.policy = static_cast<std::uint8_t>(spec.config.policy);
    row.priority = spec.config.priority;
    row.workers = i < pools_.size() && pools_[i]
                      ? static_cast<std::uint16_t>(pools_[i]->workerCount())
                      : static_cast<std::uint16_t>(0U);
    std::uint64_t mask = 0;
    for (const std::uint8_t CPU : spec.config.affinity) {
      if (CPU < 64U) {
        mask |= (1ULL << CPU);
      }
    }
    row.affinityMask = mask;
    rows.push_back(row);
  }
  return rows;
}

bool SchedulerMultiThread::threadsRunning() const noexcept {
  for (const auto& pool : pools_) {
    if (pool && pool->threadsRunning()) {
      return true;
    }
  }
  return false;
}

/* ----------------------------- Initialization ----------------------------- */

std::uint8_t SchedulerMultiThread::doInit() noexcept {
  initSchedulerLog(logDir_);

  // Spawn worker pools now that the TPRM header is available (loaded by
  // SchedulerBase::loadTprm before doInit runs in the normal apex lifecycle).
  resolveAndInitPoolsFromTprm();

  // The table may have loaded before the log and the pools existed; the
  // verdicts latched then were computed against an empty pool set.
  // Re-analyze now that construction is complete, and emit.
  if (!entries_.empty()) {
    runTablePreflight();
  }

  componentLog()->info(label(), "Multi-threaded scheduler constructed");
  componentLog()->info(label(), fmt::format("Fundamental frequency: {} Hz", ffreq_));
  componentLog()->info(label(), fmt::format("Thread pools: {}", pools_.size()));

  // Log pool configuration
  for (std::size_t i = 0; i < pools_.size(); ++i) {
    componentLog()->info(label(), fmt::format("  Pool[{}] '{}': {} workers", i, poolNames_[i],
                                              pools_[i]->workerCount()));
  }

  // Count total task instances across all ticks for pool sizing, and the
  // largest single-tick burst -- the number of contexts one tick can demand
  // before any completion returns a context to the pool.
  std::size_t totalTaskInstances = 0;
  std::size_t maxTickBurst = 0;
  for (const auto& tickIndices : schedule_) {
    totalTaskInstances += tickIndices.size();
    maxTickBurst = std::max(maxTickBurst, tickIndices.size());
  }

  // Pre-allocate context pools. Capacity must cover the densest tick with
  // every one of its tasks dispatched before any completes, plus headroom
  // for tasks still in flight from earlier ticks. Sizing from worker count
  // alone starves dense ticks, and in production (fallback allocation
  // disabled) a starved acquire has no heap escape.
  for (std::size_t i = 0; i < pools_.size(); ++i) {
    const std::size_t POOL_SIZE = maxTickBurst + pools_[i]->workerCount() * 2;
    ctxPools_[i]->preallocate(POOL_SIZE);

#ifdef NDEBUG
    ctxPools_[i]->setFallbackAlloc(false);
#endif
  }

  componentLog()->info(label(),
                       fmt::format("Total task instances in schedule: {}", totalTaskInstances));

#ifdef NDEBUG
  componentLog()->info(label(), "TaskCtx fallback allocation: DISABLED (production mode)");
#else
  componentLog()->info(label(), "TaskCtx fallback allocation: ENABLED (debug mode)");
#endif

  componentLog()->info(label(), "");

  // Calculate total workers for mode description
  std::size_t totalWorkers = 0;
  for (const auto& pool : pools_) {
    totalWorkers += pool->workerCount();
  }
  logScheduleLayout(
      fmt::format("Multi-threaded ({} workers, {} pools)", totalWorkers, pools_.size()));

  return static_cast<std::uint8_t>(Status::SUCCESS);
}

/* ----------------------------- Execution ----------------------------- */

Status SchedulerMultiThread::executeTasksOnTickMulti(std::uint16_t tick) noexcept {
  // Track dispatch count for health telemetry (tickCount_ is only incremented
  // by SchedulerBase::tick() which the executive does not call -- it calls
  // executeTasksOnTickMulti() directly).
  ++tickCount_;

  // Reset per-tick violation counter
  periodViolationsThisTick_ = 0;

  // Collect errors from async task completions (from previous ticks)
  // Check all pools for errors
  bool hadErrors = false;
  auto* lg = componentLog();
  const int LOG_LEVEL = lg ? static_cast<int>(lg->level()) : 0;

  for (auto& pool : pools_) {
    if (pool->hasErrors()) {
      auto errors = pool->collectErrors();
      while (!errors.empty()) {
        hadErrors = true;
        auto err = errors.front();
        errors.pop();
        if (lg && static_cast<int>(logs::SystemLog::Level::WARNING) >= LOG_LEVEL) {
          lg->warning(label(), static_cast<std::uint8_t>(Status::WARN_TASK_NON_SUCCESS_RET),
                      fmt::format("Task '{}' returned error code {}", err.label, err.errorCode));
        }
      }
    }
  }

  const std::size_t SIZE = schedule_.size();
  const std::size_t TIDX = static_cast<std::size_t>(tick);
  if (SIZE == 0U || TIDX >= SIZE) {
    const Status OUT = hadErrors ? Status::WARN_TASK_NON_SUCCESS_RET : Status::SUCCESS;
    setStatus(static_cast<std::uint8_t>(OUT));
    return OUT;
  }

  auto& entryIndices = schedule_[TIDX];
  if (entryIndices.empty()) {
    const Status OUT = hadErrors ? Status::WARN_TASK_NON_SUCCESS_RET : Status::SUCCESS;
    setStatus(static_cast<std::uint8_t>(OUT));
    return OUT;
  }

  // Enqueue all tasks that should run (frequency gate check)
  for (std::size_t idx : entryIndices) {
    TaskEntry& entry = entries_[idx];
    if (entry.shouldRun()) {
      // Skip tasks belonging to locked components
      if (entry.fullUid != 0 && componentResolver_) {
        auto* comp = componentResolver_->getComponent(entry.fullUid);
        if (comp != nullptr && comp->isLocked()) {
          continue;
        }
      }

      // Check for period deadline violation: task still running from previous dispatch
      if (entry.stillRunning()) {
        ++periodViolationsThisTick_;
        ++totalPeriodViolations_;
        if (entry.stats) {
          entry.stats->deadlineViolations.fetch_add(1, std::memory_order_relaxed);
        }
        lastViolationComponent_.store(entry.componentName, std::memory_order_release);
        lastViolationTaskUid_.store(entry.taskUid, std::memory_order_release);
        periodViolationFlag_.store(true, std::memory_order_release);

        if (skipOnBusy_) {
          // SKIP_ON_BUSY mode: skip this invocation instead of dispatching
          ++entry.skipCount;
          ++totalSkipCount_;
          if (lg && static_cast<int>(logs::SystemLog::Level::WARNING) >= LOG_LEVEL) {
            lg->warning(label(), static_cast<std::uint8_t>(Status::WARN_PERIOD_VIOLATION),
                        fmt::format("Skipping task '{}' at tick {} (still running, skip #{})",
                                    entry.task->getLabel(), tick, entry.skipCount));
          }
          continue; // Skip dispatch, move to next task
        }

        // Not in skip mode: violation flag already set above for clock thread to check.
        // No per-violation warning here:
        //  - HARD_PERIOD_COMPLETE: Clock thread will FATAL, warning redundant
        //  - SOFT modes: Violations expected, count tracked for shutdown summary
      }
      enqueueTask(&entry, tick);
    }
  }

  const Status OUT = hadErrors ? Status::WARN_TASK_NON_SUCCESS_RET : Status::SUCCESS;
  setStatus(static_cast<std::uint8_t>(OUT));
  return OUT;
}

/* ----------------------------- Task Dispatch ----------------------------- */

std::uint8_t SchedulerMultiThread::taskTrampoline(void* raw) noexcept {
  auto* ctx = static_cast<TaskCtx*>(raw);
  TaskEntry* entry = ctx->entry;

  // Wait for sequencing phase if this task is sequenced. An aborted
  // wait (scheduler stopping) skips execution and the phase advance --
  // the chain is dying, and advancing would fake phase completion.
  if (entry != nullptr && entry->isSequenced()) {
    if (!waitForPhase(*entry->seqCounter, entry->seqPhase, &ctx->self->stopping_)) {
      entry->markCompleted();
      const std::uint8_t poolIdAbort = ctx->poolId;
      if (poolIdAbort < ctx->self->ctxPools_.size()) {
        ctx->self->ctxPools_[poolIdAbort]->release(ctx);
      }
      return 0;
    }
  }

  const std::uint8_t rc = ctx->task->execute();

  // Advance sequencing counter after task completion
  if (entry != nullptr && entry->isSequenced()) {
    advancePhase(*entry->seqCounter, entry->seqMaxPhase);
  }

  // Mark task as completed (for deadline tracking)
  if (entry != nullptr) {
    entry->markCompleted();
  }

  ctx->self->onTaskComplete(ctx->task, ctx->tick);

  // Release context back to the correct pool
  const std::uint8_t poolId = ctx->poolId;
  if (poolId < ctx->self->ctxPools_.size()) {
    ctx->self->ctxPools_[poolId]->release(ctx);
  }
  return rc;
}

void SchedulerMultiThread::enqueueTask(TaskEntry* entry, std::uint16_t tick) noexcept {
  // First drop and every interval-th after: surfaces sustained exhaustion
  // without logging at tick rate from the dispatch path. Power of two so the
  // modulo compiles to a mask; the totalDispatchDrops_ counter stays exact.
  constexpr std::size_t DISPATCH_DROP_LOG_INTERVAL = 4096U;
  SchedulableTask* task = entry->task;
  std::uint8_t poolId = entry->config.poolId;

  // Clamp to valid pool range
  if (poolId >= pools_.size()) {
    poolId = 0;
  }

  // Acquire the context before marking the entry in-flight: a dispatch that
  // never enqueues must leave the entry idle. Marking first poisons the
  // entry's deadline tracking on a failed acquire -- it reads as running
  // forever, which starves the task silently in soft modes and triggers
  // emergency shutdown in hard modes.
  TaskCtx* ctx = ctxPools_[poolId]->acquire(this, task);
  if (ctx == nullptr) {
    ++totalDispatchDrops_;
    auto* lg = componentLog();
    if (lg != nullptr &&
        (totalDispatchDrops_ == 1 || (totalDispatchDrops_ % DISPATCH_DROP_LOG_INTERVAL) == 0U)) {
      lg->error(label(), static_cast<std::uint8_t>(Status::WARN_PERIOD_VIOLATION),
                fmt::format("Dispatch dropped: task '{}' at tick {}, context pool exhausted "
                            "(total drops: {})",
                            task->getLabel(), tick, totalDispatchDrops_));
    }
    return;
  }

  // Mark task as dispatched (for deadline tracking)
  entry->markDispatched();

  ctx->tick = tick;
  ctx->poolId = poolId;
  ctx->entry = entry; // Store entry for sequencing in trampoline
  const auto STATUS = pools_[poolId]->tryEnqueue(task->getLabel().data(), {&taskTrampoline, ctx});
  if (STATUS != apex::concurrency::PoolStatus::SUCCESS) {
    // Bounded ring rejected the dispatch (full ring = the pool stopped
    // draining long before this; stopped pool = shutdown race). Undo the
    // dispatch marks so deadline tracking stays truthful, and count it
    // with the same drop accounting as context exhaustion.
    ctxPools_[poolId]->release(ctx);
    if (entry->isRunning) {
      entry->isRunning->store(false, std::memory_order_release);
    }
    ++entry->skipCount;
    ++totalDispatchDrops_;
    auto* lg = componentLog();
    if (lg != nullptr &&
        (totalDispatchDrops_ == 1 || (totalDispatchDrops_ % DISPATCH_DROP_LOG_INTERVAL) == 0U)) {
      lg->error(label(), static_cast<std::uint8_t>(Status::WARN_PERIOD_VIOLATION),
                fmt::format("Dispatch dropped: task '{}' at tick {}, pool ring rejected "
                            "(status {}, total drops: {})",
                            task->getLabel(), tick, static_cast<int>(STATUS),
                            totalDispatchDrops_));
    }
  }
}

} // namespace scheduler
} // namespace system_core
