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
  // Single pool with provided config
  std::vector<PoolSpec> specs;
  specs.push_back({"default", 0, poolConfig});
  initPools(std::move(specs));
}

SchedulerMultiThread::SchedulerMultiThread(std::uint16_t ffreq, const std::filesystem::path& logDir,
                                           std::vector<PoolSpec> pools) noexcept
    : SchedulerBase(ffreq), logDir_(logDir) {
  if (pools.empty()) {
    // Default: single pool
    pools.push_back({"default", 0, {}});
  }
  initPools(std::move(pools));
}

void SchedulerMultiThread::initPools(std::vector<PoolSpec> specs) noexcept {
  pools_.reserve(specs.size());
  ctxPools_.reserve(specs.size());
  poolNames_.reserve(specs.size());

  for (auto& spec : specs) {
    const std::size_t NUM_THREADS =
        spec.numThreads > 0 ? spec.numThreads : std::thread::hardware_concurrency();

    pools_.emplace_back(std::make_unique<apex::concurrency::ThreadPool>(NUM_THREADS, spec.config));

    // Each pool gets its own context pool (sized for its workers)
    ctxPools_.emplace_back(std::make_unique<TaskCtxPool>());

    poolNames_.push_back(std::move(spec.name));
  }
}

SchedulerMultiThread::~SchedulerMultiThread() noexcept { shutdown(); }

void SchedulerMultiThread::shutdown() noexcept {
  for (auto& pool : pools_) {
    if (pool) {
      pool->shutdown();
    }
  }
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

  componentLog()->info(label(), "Multi-threaded scheduler constructed");
  componentLog()->info(label(), fmt::format("Fundamental frequency: {} Hz", ffreq_));
  componentLog()->info(label(), fmt::format("Thread pools: {}", pools_.size()));

  // Log pool configuration
  for (std::size_t i = 0; i < pools_.size(); ++i) {
    componentLog()->info(label(), fmt::format("  Pool[{}] '{}': {} workers", i, poolNames_[i],
                                              pools_[i]->workerCount()));
  }

  // Count total task instances across all ticks for pool sizing
  std::size_t totalTaskInstances = 0;
  for (const auto& tickIndices : schedule_) {
    totalTaskInstances += tickIndices.size();
  }

  // Pre-allocate context pools (each pool sized for its workers)
  for (std::size_t i = 0; i < pools_.size(); ++i) {
    const std::size_t POOL_SIZE = pools_[i]->workerCount() * 2;
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

  // Wait for sequencing phase if this task is sequenced
  if (entry != nullptr && entry->isSequenced()) {
    waitForPhase(*entry->seqCounter, entry->seqPhase);
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
  SchedulableTask* task = entry->task;
  std::uint8_t poolId = entry->config.poolId;

  // Clamp to valid pool range
  if (poolId >= pools_.size()) {
    poolId = 0;
  }

  // Mark task as dispatched (for deadline tracking)
  entry->markDispatched();

  // Acquire context from the task's pool
  TaskCtx* ctx = ctxPools_[poolId]->acquire(this, task);
  if (ctx) {
    ctx->tick = tick;
    ctx->poolId = poolId;
    ctx->entry = entry; // Store entry for sequencing in trampoline
    pools_[poolId]->enqueueTask(task->getLabel().data(), {&taskTrampoline, ctx});
  }
}

} // namespace scheduler
} // namespace system_core
