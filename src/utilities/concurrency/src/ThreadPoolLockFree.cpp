/**
 * @file ThreadPoolLockFree.cpp
 * @brief Implementation of lock-free ThreadPool.
 */

#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"

#include <cstdint>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPoolLockFree Methods ----------------------------- */

ThreadPoolLockFree::ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity,
                                       const PoolConfig& config)
    : PoolBase(numThreads),
      taskQueue_(queueCapacity < MAX_QUEUE_CAPACITY ? queueCapacity : MAX_QUEUE_CAPACITY) {
  if (!spawnWorkers(config, "lf_pool_w", &ThreadPoolLockFree::workerEntry, this)) {
    // A thread could not start: the pool is reported invalid rather than left
    // with a partial set of workers.
    shutdown();
  }
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
  joinWorkers();
}

/* ----------------------------- File Helpers ----------------------------- */

void ThreadPoolLockFree::workerLoop(WorkerTally& tally) {
  TaskItem item;
  for (;;) {
    // Drain until a pop fails. A pop can fail while a producer ahead of a
    // published task is still publishing its own cell; that producer's
    // signal brings a worker back to drain both, so no published task is
    // stranded behind a worker that looked too early.
    while (taskQueue_.tryPop(item)) {
      runTask(tally, item.label, item.task);
    }

    // Park, unless the ring changed. The wake word is read first, the
    // worker announces itself idle, and only then looks at the ring again:
    // a task published before that look is found by it, and one published
    // after it has moved the word, so the wait returns at once.
    const std::uint32_t SEEN = wake_.value.generation.load(std::memory_order_seq_cst);
    wake_.value.idle.fetch_add(1, std::memory_order_seq_cst);
    if (taskQueue_.tryPop(item)) {
      wake_.value.idle.fetch_sub(1, std::memory_order_release);
      runTask(tally, item.label, item.task);
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

} // namespace concurrency
} // namespace apex
