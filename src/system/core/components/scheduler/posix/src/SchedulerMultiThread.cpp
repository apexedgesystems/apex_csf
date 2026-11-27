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
#include <chrono>

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

    // The pool reports its own start-up: no workers at all is a launch
    // failure doInit() refuses. A worker with a setting that did not apply
    // runs with part of its requested configuration, which the schedule's
    // timing assumes it does not, so it is a coded warning naming the pool;
    // the pool does not record which setting failed, and the warning does
    // not guess.
    const auto& POOL = *pools_.back();
    if (!POOL.isValid()) {
      poolStartFailed_ = true;
      componentLog()->error(label(), static_cast<std::uint8_t>(Status::ERROR_THREAD_LAUNCH_FAIL),
                            fmt::format("Pool '{}': start-up failed ({} workers requested); the "
                                        "workers that had started were joined, none remain",
                                        spec.name, NUM_THREADS));
    } else if (POOL.workerConfigFailures() != 0) {
      componentLog()->warning(
          label(), static_cast<std::uint8_t>(Status::WARN_WORKER_CONFIG_FAIL),
          fmt::format("Pool '{}': {} of {} workers did not apply one or more of their requested "
                      "settings (policy {}, priority {}, {} affinity entries, thread name); the "
                      "settings that applied stand, the failed one is not identified",
                      spec.name, POOL.workerConfigFailures(), POOL.workerCount(),
                      spec.config.policy, spec.config.priority, spec.config.affinity.size()));
    }

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
  if (poolStartFailed_) {
    return static_cast<std::uint8_t>(Status::ERROR_THREAD_LAUNCH_FAIL);
  }

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

  std::size_t totalTaskInstances = 0;
  for (const auto& tickIndices : schedule_) {
    totalTaskInstances += tickIndices.size();
  }

  sizeContextPools();
#ifdef NDEBUG
  for (auto& ctxPool : ctxPools_) {
    ctxPool->setFallbackAlloc(false);
  }
#endif

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

/* ----------------------------- Context Capacity ----------------------------- */

void SchedulerMultiThread::sizeContextPools() noexcept {
  // The largest single-tick burst is the number of contexts one tick can
  // demand before any completion returns one to its pool.
  std::size_t maxTickBurst = 0;
  for (const auto& tickIndices : schedule_) {
    maxTickBurst = std::max(maxTickBurst, tickIndices.size());
  }

  // Capacity must cover the densest tick with every one of its tasks
  // dispatched before any completes, plus headroom for tasks still in
  // flight from earlier ticks. Sizing from worker count alone starves
  // dense ticks, and in production (fallback allocation disabled) a
  // starved acquire has no heap escape.
  for (std::size_t i = 0; i < ctxPools_.size(); ++i) {
    ctxPools_[i]->reserve(maxTickBurst + pools_[i]->workerCount() * 2);
  }
}

Status SchedulerMultiThread::addTask(SchedulableTask& task, const TaskConfig& config) noexcept {
  const Status RC = SchedulerBase::addTask(task, config);
  if (RC == Status::SUCCESS) {
    sizeContextPools();
  }
  return RC;
}

/* ----------------------------- Execution ----------------------------- */

Status SchedulerMultiThread::executeTasksOnTickMulti(std::uint16_t tick) noexcept {
  return executeTasksOnCycleMulti(cycleForTick(tick));
}

Status SchedulerMultiThread::executeTasksOnCycleMulti(std::uint64_t cycle) noexcept {
  // The cycle is noted before any early return, so a late addTask aligns
  // to the cycle after this one whether or not it had work.
  const std::uint16_t tick = beginDispatchCycle(cycle);

  // Track dispatch count for health telemetry (tickCount_ is only incremented
  // by SchedulerBase::tick() which the executive does not call -- it calls
  // the cycle entry directly).
  ++tickCount_;

  // Reset per-tick violation counter
  periodViolationsThisTick_ = 0;

  // Collect errors from async task completions (from previous ticks)
  auto* lg = componentLog();
  const int LOG_LEVEL = lg ? static_cast<int>(lg->level()) : 0;
  // The test for pending errors stays in line: one load per pool on
  // every dispatch, and a call only when a task failed.
  bool hadErrors = false;
  for (const auto& pool : pools_) {
    if (pool->hasErrors()) {
      hadErrors = logCompletionErrors(lg, LOG_LEVEL);
      break;
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

  // One clock read serves every dispatch this tick: the stats/deadline
  // base is the tick's dispatch-decision time, so the producer path
  // pays a single read regardless of table size.
  const std::uint64_t DISPATCH_NS = apex::helpers::cpu::getMonotonicNs();

  // A slot that holds a multi-second task takes a gated loop; every
  // other slot takes one that tests nothing per visit. A slot that holds
  // a sequenced task is dispatched in two steps, so each such task can be
  // placed among the tasks of its group that run with it.
  const bool GATED = slotHasGated_[TIDX] != 0U;
  if (slotHasSequenced_[TIDX] != 0U) {
    if (GATED) {
      dispatchSlot<true, true>(entryIndices, cycle, tick, DISPATCH_NS, lg, LOG_LEVEL);
    } else {
      dispatchSlot<false, true>(entryIndices, cycle, tick, DISPATCH_NS, lg, LOG_LEVEL);
    }
  } else if (GATED) {
    dispatchSlot<true, false>(entryIndices, cycle, tick, DISPATCH_NS, lg, LOG_LEVEL);
  } else {
    dispatchSlot<false, false>(entryIndices, cycle, tick, DISPATCH_NS, lg, LOG_LEVEL);
  }

  const Status OUT = hadErrors ? Status::WARN_TASK_NON_SUCCESS_RET : Status::SUCCESS;
  setStatus(static_cast<std::uint8_t>(OUT));
  return OUT;
}

template <bool kGated, bool kSequenced>
void SchedulerMultiThread::dispatchSlot(const std::vector<std::size_t>& entryIndices,
                                        [[maybe_unused]] std::uint64_t cycle, std::uint16_t tick,
                                        std::uint64_t dispatchNs, logs::SystemLog* lg,
                                        int logLevel) noexcept {
  if constexpr (kSequenced) {
    slotDispatch_.clear();
  }
  for (std::size_t idx : entryIndices) {
    TaskEntry& entry = entries_[idx];

    // Cadence gate: is this cycle one of the task's runs? The instant is
    // consumed here whether or not the task then dispatches.
    if constexpr (kGated) {
      if (entry.gated) {
        if (!cadences_[idx].due(cycle)) {
          continue;
        }
        cadences_[idx].advance();
      }
    }

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
        if (lg && static_cast<int>(logs::SystemLog::Level::WARNING) >= logLevel) {
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
    if constexpr (kSequenced) {
      slotDispatch_.push_back({&entry, nullptr});
    } else {
      enqueueTask<false>(&entry, tick, dispatchNs);
    }
  }
  if constexpr (kSequenced) {
    enqueueSequencedSlot(tick, dispatchNs);
  }
}

void SchedulerMultiThread::enqueueSequencedSlot(std::uint16_t tick,
                                                std::uint64_t dispatchNs) noexcept {
  // Step 1: every entry goes to its pool, in slot order. A sequenced one
  // goes with its wait unset: a worker that takes it holds at the start
  // of the trampoline until step 2 has placed it. A pool can refuse any
  // of them, and which it refuses is known only once all have been tried.
  for (SlotDispatch& item : slotDispatch_) {
    if (item.entry->isSequenced()) {
      item.ctx = enqueueTask<true>(item.entry, tick, dispatchNs);
    } else {
      enqueueTask<false>(item.entry, tick, dispatchNs);
    }
  }

  // Step 2: a group's counter rests at 1 between ticks and each of the
  // group's tasks advances it once when it completes. So a task that must
  // follow AHEAD tasks waits for the counter to reach AHEAD + 1, and the
  // last completion of TOGETHER tasks wraps it back to 1. Both are
  // counted over the entries the pools accepted this tick: a task whose
  // earlier phases are not on this tick, were skipped, or were refused by
  // their pool waits for none of them, and the counter wraps without them.
  for (const SlotDispatch& item : slotDispatch_) {
    if (item.ctx == nullptr) {
      continue;
    }
    int ahead = 0;
    int together = 0;
    for (const SlotDispatch& other : slotDispatch_) {
      if (other.ctx == nullptr || other.entry->seqCounter != item.entry->seqCounter) {
        continue;
      }
      ++together;
      if (other.entry->seqPhase < item.entry->seqPhase) {
        ++ahead;
      }
    }
    item.ctx->seqMaxPhase = together;
    item.ctx->seqPhase.store(ahead + 1, std::memory_order_release);
  }
}

bool SchedulerMultiThread::waitForInFlightTasks(std::uint64_t timeoutNs) noexcept {
  return waitForInFlightTasksUntil(apex::helpers::cpu::getMonotonicNs() + timeoutNs);
}

bool SchedulerMultiThread::logCompletionErrors(logs::SystemLog* lg, int logLevel) noexcept {
  bool hadErrors = false;
  for (auto& pool : pools_) {
    if (pool->hasErrors()) {
      auto errors = pool->collectErrors();
      while (!errors.empty()) {
        hadErrors = true;
        auto err = errors.front();
        errors.pop();
        if (lg && static_cast<int>(logs::SystemLog::Level::WARNING) >= logLevel) {
          lg->warning(label(), static_cast<std::uint8_t>(Status::WARN_TASK_NON_SUCCESS_RET),
                      fmt::format("Task '{}' returned error code {}", err.label, err.errorCode));
        }
      }
    }
  }
  return hadErrors;
}

bool SchedulerMultiThread::settled() noexcept {
  // An entry reads idle once each of its invocations has run to the mark
  // its worker sets inside the task call. A pool reads idle once every
  // worker that entered a task call has left it and has filed the call's
  // completion record. This thread is the only one that dispatches, so
  // with both idle nothing is queued, running or completing.
  for (const TaskEntry& entry : entries_) {
    if (entry.stillRunning()) {
      return false;
    }
  }
  for (const auto& pool : pools_) {
    if (pool->threadsRunning()) {
      return false;
    }
  }

  // The completion records carry task labels; logging them here leaves
  // none behind.
  auto* lg = componentLog();
  static_cast<void>(logCompletionErrors(lg, lg ? static_cast<int>(lg->level()) : 0));
  return true;
}

bool SchedulerMultiThread::waitForInFlightTasksUntil(std::uint64_t deadlineNs) noexcept {
  const std::uint64_t DEADLINE = deadlineNs;
  // Two phases. A short spin catches the common case, a task that
  // completes within tens of microseconds of its dispatch, without a
  // sleep's latency. Past the spin the waiter sleeps in slices whose
  // requested length doubles from SLEEP_MIN_NS to SLEEP_MAX_NS and is
  // capped at what was left of the budget at the preceding clock read: a
  // worker that shares this CPU gets it while the wait holds. A waiter
  // that kept spinning would keep the very worker it waits for off a
  // shared core, and under ordinary scheduling would cost a core's worth
  // of CPU for every held frame. A sleep gives the CPU up under every
  // scheduling policy, where a yield does not under a real-time one. The
  // clock is read before every request and none is issued once a read
  // finds the deadline passed, but a preemption between that read and
  // the sleep call can issue the request late, and when a sleep returns
  // is the host's to decide: the deadline bounds the length of each
  // request, not the instant it is issued or the instant the wait comes
  // back. A completion that lands during a sleep is noticed on the next
  // return.
  constexpr unsigned SPIN_LIMIT = 1024U;
  constexpr std::uint64_t SLEEP_MIN_NS = 20000U;
  constexpr std::uint64_t SLEEP_MAX_NS = 250000U;
  apex::helpers::cpu::ExponentialBackoff bk;
  std::uint64_t sleepNs = 0U;
  for (;;) {
    bool pending = false;
    for (const TaskEntry& entry : entries_) {
      // stillRunning() is the acquire that pairs with each completion's
      // release, and it counts invocations: an entry with a second
      // invocation queued or running behind a completed first is still
      // in flight. Once every entry reads idle, every completed task's
      // writes are published to this thread.
      if (entry.stillRunning()) {
        pending = true;
        break;
      }
    }
    if (!pending) {
      return true;
    }
    if (stopping_.load(std::memory_order_acquire)) {
      return false;
    }
    const std::uint64_t NOW = apex::helpers::cpu::getMonotonicNs();
    if (NOW >= DEADLINE) {
      totalInFlightWaitTimeouts_.fetch_add(1U, std::memory_order_relaxed);
      return false;
    }
    if (bk.n < SPIN_LIMIT) {
      bk.spinOnce();
      continue;
    }
    sleepNs = (sleepNs == 0U) ? SLEEP_MIN_NS : std::min(sleepNs * 2U, SLEEP_MAX_NS);
    const std::uint64_t SLICE_NS = std::min(sleepNs, DEADLINE - NOW);
    std::this_thread::sleep_for(std::chrono::nanoseconds(SLICE_NS));
  }
}

/* ----------------------------- Task Dispatch ----------------------------- */

std::uint8_t SchedulerMultiThread::taskTrampoline(void* raw) noexcept {
  auto* ctx = static_cast<TaskCtx*>(raw);
  TaskEntry* entry = ctx->entry;

  // Wait for sequencing phase if this task is sequenced: first for the
  // tick thread to place this dispatch, then for the counter. An aborted
  // wait (scheduler stopping) skips execution and the phase advance --
  // the chain is dying, and advancing would fake phase completion.
  if (entry != nullptr && entry->isSequenced()) {
    const int WAIT = awaitPlacement(*ctx);
    if (WAIT == 0 || !waitForPhase(*entry->seqCounter, WAIT, &ctx->self->stopping_)) {
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
    advancePhase(*entry->seqCounter, ctx->seqMaxPhase);
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

int SchedulerMultiThread::awaitPlacement(const TaskCtx& ctx) noexcept {
  // The tick thread places a dispatch before it leaves the slot that
  // enqueued it, a few entries' work after the enqueue, so the wait is a
  // spin in all but one case: the tick thread losing its CPU inside the
  // slot. Past the spin the worker sleeps in short slices, which gives
  // the CPU up under every scheduling policy and needs no wake from the
  // tick thread.
  constexpr unsigned SPIN_LIMIT = 1024U;
  constexpr std::uint64_t SLEEP_NS = 20000U;
  apex::helpers::cpu::ExponentialBackoff bk;
  for (;;) {
    const int WAIT = ctx.seqPhase.load(std::memory_order_acquire);
    if (WAIT != 0) {
      return WAIT;
    }
    if (ctx.self->stopping_.load(std::memory_order_acquire)) {
      return 0;
    }
    if (bk.n < SPIN_LIMIT) {
      bk.spinOnce();
    } else {
      std::this_thread::sleep_for(std::chrono::nanoseconds(SLEEP_NS));
    }
  }
}

template <bool kUnplaced>
std::conditional_t<kUnplaced, TaskCtx*, void>
SchedulerMultiThread::enqueueTask(TaskEntry* entry, std::uint16_t tick,
                                  std::uint64_t dispatchNs) noexcept {
  using Out = std::conditional_t<kUnplaced, TaskCtx*, void>;
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
    return static_cast<Out>(nullptr);
  }

  // Mark task as dispatched (for deadline tracking)
  entry->markDispatched(dispatchNs);

  ctx->tick = tick;
  ctx->poolId = poolId;
  ctx->entry = entry; // Store entry for sequencing in trampoline
  if constexpr (kUnplaced) {
    ctx->seqPhase.store(0, std::memory_order_relaxed);
  }
  const auto STATUS = pools_[poolId]->tryEnqueue(task->getLabel().data(), {&taskTrampoline, ctx});
  if (STATUS != apex::concurrency::PoolStatus::SUCCESS) {
    // Bounded ring rejected the dispatch (full ring = the pool stopped
    // draining long before this; stopped pool = shutdown race). Undo the
    // dispatch marks so deadline tracking stays truthful, and count it
    // with the same drop accounting as context exhaustion.
    ctxPools_[poolId]->release(ctx);
    entry->unmarkDispatched();
    ++entry->skipCount;
    ++totalDispatchDrops_;
    auto* lg = componentLog();
    if (lg != nullptr &&
        (totalDispatchDrops_ == 1 || (totalDispatchDrops_ % DISPATCH_DROP_LOG_INTERVAL) == 0U)) {
      lg->error(label(), static_cast<std::uint8_t>(Status::WARN_PERIOD_VIOLATION),
                fmt::format("Dispatch dropped: task '{}' at tick {}, pool ring rejected "
                            "(status {}, total drops: {})",
                            task->getLabel(), tick, static_cast<int>(STATUS), totalDispatchDrops_));
    }
    return static_cast<Out>(nullptr);
  }
  return static_cast<Out>(ctx);
}

} // namespace scheduler
} // namespace system_core
