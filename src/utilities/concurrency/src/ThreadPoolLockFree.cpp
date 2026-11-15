/**
 * @file ThreadPoolLockFree.cpp
 * @brief Implementation of lock-free ThreadPool.
 */

#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"
#include "src/utilities/concurrency/inc/Latch.hpp"
#include "src/utilities/concurrency/inc/ThreadConfig.hpp"

#include <cstdio>
#include <system_error>
#include <thread>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPoolLockFree Methods ----------------------------- */

ThreadPoolLockFree::ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity,
                                       const PoolConfig& config)
    : taskQueue_(queueCapacity < MAX_QUEUE_CAPACITY ? queueCapacity : MAX_QUEUE_CAPACITY) {
  spawnWorkers(numThreads, config);
}

void ThreadPoolLockFree::spawnWorkers(std::size_t numThreads, const PoolConfig& config) {
  // The workers inherit the blocked process signals; the scope hands the
  // constructing thread its own mask back when the spawn is over, success
  // or not.
  WorkerSignalScope signals;
  // Each worker counts down once its configuration is applied and never
  // touches the latch again. The constructor parks on it rather than
  // yielding, so a worker configured below the constructing thread's
  // priority, on the same CPU, still gets to run.
  Latch configured(numThreads);
  workers_.reserve(numThreads);
  try {
    for (std::size_t i = 0; i < numThreads; ++i) {
      workers_.emplace_back([this, config, i, &configured]() {
        if (!applyWorkerConfig(config, i)) {
          configFailures_.fetch_add(1, std::memory_order_acq_rel);
        }
        configured.countDown();
        workerLoop();
      });
    }
  } catch (const std::system_error&) {
    // A thread could not start (the process or thread limit): the pool is
    // reported invalid rather than left with joinable threads to terminate
    // on. The workers that did start are joined here, so none outlives the
    // latch.
    shutdown();
    return;
  }
  // Every worker has applied its configuration before the pool is handed
  // over, so workerConfigFailures() is final for the owner's start-up log.
  configured.wait();
}

ThreadPoolLockFree::~ThreadPoolLockFree() noexcept { shutdown(); }

PoolStatus ThreadPoolLockFree::tryEnqueue(const char* label, TaskFn task) noexcept {
  // Enter the producer count and read the shutdown stage in one
  // read-modify-write: a producer that does not see the close is counted
  // before shutdown() can observe zero producers, so its publication is
  // waited for.
  if ((admission_.value.fetch_add(1, std::memory_order_acq_rel) & CLOSED) != 0) {
    leaveAdmission();
    return PoolStatus::POOL_STOPPED;
  }

  TaskItem item{label, task};
  const bool PUSHED = taskQueue_.tryPush(std::move(item));
  if (PUSHED) {
    signalWork();
  }
  leaveAdmission();
  return PUSHED ? PoolStatus::SUCCESS : PoolStatus::QUEUE_FULL;
}

void ThreadPoolLockFree::enqueueTask(const char* label, TaskFn task) noexcept {
  // Fire-and-forget version, ignores status.
  (void)tryEnqueue(label, task);
}

void ThreadPoolLockFree::signalWork() noexcept {
  // Advance, then look for an idle worker. The worker does the mirror
  // image in workerLoop(): it announces itself idle, then looks at the
  // ring and at the generation. All four are sequentially consistent, so
  // either this load sees the announcement and wakes the worker, or the
  // worker's look comes after this advance and it does not park.
  wake_.value.generation.fetch_add(ADVANCE, std::memory_order_seq_cst);
  if (wake_.value.idle.load(std::memory_order_seq_cst) != 0) {
    wake_.value.generation.notify_one();
  }
}

void ThreadPoolLockFree::leaveAdmission() noexcept {
  // The last producer out after the close is the one shutdown() may be
  // parked on; the release orders this producer's publication before
  // shutdown()'s acquire of the count.
  const std::uint32_t BEFORE = admission_.value.fetch_sub(1, std::memory_order_acq_rel);
  if ((BEFORE & CLOSED) != 0 && (BEFORE & PRODUCERS) == 1) {
    admission_.value.notify_all();
  }
}

bool ThreadPoolLockFree::threadsRunning() const noexcept {
  return activeTasks_.value.load(std::memory_order_acquire) > 0;
}

void ThreadPoolLockFree::shutdown() noexcept {
  // Stage one: close admission. From here tryEnqueue() returns POOL_STOPPED.
  std::uint32_t state = admission_.value.fetch_or(CLOSED, std::memory_order_acq_rel);
  if ((state & CLOSED) != 0) {
    return; // Already stopped.
  }
  state |= CLOSED;

  // Park until the producers admitted before the close have published.
  // Each one leaving changes the word and the last one notifies, so this
  // is a futex wait, not a spin: a producer below this thread's priority
  // on the same CPU still runs.
  while ((state & PRODUCERS) != 0) {
    admission_.value.wait(state, std::memory_order_acquire);
    state = admission_.value.load(std::memory_order_acquire);
  }

  // Stage two: every accepted task is published. The mark goes into the
  // word the workers wait on, in a bit the publication counter never
  // touches, so the word now differs from every value a worker read before
  // this point, whatever the counter did in between. A parked worker is
  // woken; one still on its way to parking finds the word changed; one
  // that reads the mark exits once it finds the ring empty.
  wake_.value.generation.fetch_or(QUIESCENT, std::memory_order_seq_cst);
  wake_.value.generation.notify_all();

  for (auto& w : workers_) {
    if (w.joinable()) {
      w.join();
    }
  }
  workers_.clear();
}

bool ThreadPoolLockFree::hasErrors() const noexcept {
  return hasErrors_.value.load(std::memory_order_acquire);
}

std::queue<TaskError> ThreadPoolLockFree::collectErrors() {
  std::lock_guard<std::mutex> lk(errorMutex_);
  std::queue<TaskError> out = std::move(errorQueue_);
  errorQueue_ = {};
  hasErrors_.value.store(false, std::memory_order_release);
  return out;
}

/* ----------------------------- File Helpers ----------------------------- */

void ThreadPoolLockFree::workerLoop() {
  TaskItem item;
  for (;;) {
    // Drain until a pop fails. A pop can fail while a producer ahead of a
    // published task is still publishing its own cell; that producer's
    // signal brings a worker back to drain both, so no published task is
    // stranded behind a worker that looked too early.
    while (taskQueue_.tryPop(item)) {
      runTask(item);
    }

    // Park, unless the ring changed. The wake word is read first, the
    // worker announces itself idle, and only then looks at the ring again:
    // a task published before that look is found by it, and one published
    // after it has moved the word, so the wait returns at once.
    const std::uint32_t SEEN = wake_.value.generation.load(std::memory_order_seq_cst);
    wake_.value.idle.fetch_add(1, std::memory_order_seq_cst);
    if (taskQueue_.tryPop(item)) {
      wake_.value.idle.fetch_sub(1, std::memory_order_release);
      runTask(item);
      continue;
    }

    // Exit only once every admitted producer has published and the ring is
    // seen empty after that. The mark read above says the first; the pop
    // that just failed came after that read, so it saw every accepted task.
    // Before the mark, an empty pop may be a publication in progress and
    // the worker parks.
    if ((SEEN & QUIESCENT) != 0) {
      wake_.value.idle.fetch_sub(1, std::memory_order_release);
      return;
    }

    wake_.value.generation.wait(SEEN, std::memory_order_seq_cst);
    wake_.value.idle.fetch_sub(1, std::memory_order_release);
  }
}

void ThreadPoolLockFree::runTask(TaskItem& item) {
  if (!item.task) {
    return; // Ignore empty slots defensively.
  }

  // Track active tasks.
  activeTasks_.value.fetch_add(1, std::memory_order_acq_rel);
  struct DecOnExit {
    std::atomic<std::size_t>* p;
    ~DecOnExit() { p->fetch_sub(1, std::memory_order_acq_rel); }
  } dec{&activeTasks_.value};

  // Execute task.
  try {
    std::uint8_t rc = item.task();
    if (rc != 0) {
      std::lock_guard<std::mutex> el(errorMutex_);
      errorQueue_.push(TaskError{item.label, rc, PoolStatus::TASK_FAILED});
      hasErrors_.value.store(true, std::memory_order_release);
    }
  } catch (const std::exception& /*e*/) {
    std::lock_guard<std::mutex> el(errorMutex_);
    errorQueue_.push(TaskError{item.label, 254, PoolStatus::EXCEPTION_STD});
    hasErrors_.value.store(true, std::memory_order_release);
  } catch (...) {
    std::lock_guard<std::mutex> el(errorMutex_);
    errorQueue_.push(TaskError{item.label, 255, PoolStatus::EXCEPTION_UNKNOWN});
    hasErrors_.value.store(true, std::memory_order_release);
  }
}

bool ThreadPoolLockFree::applyWorkerConfig(const PoolConfig& config, std::size_t workerId) {
  // Generate worker thread name (max 15 chars + null on Linux).
  char name[16];
  std::snprintf(name, sizeof(name), "lf_pool_w%zu", workerId);

  // Use common thread config utility for affinity/policy/priority.
  return applyPoolConfig(config, name);
}

} // namespace concurrency
} // namespace apex
