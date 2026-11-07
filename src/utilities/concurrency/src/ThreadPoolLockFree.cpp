/**
 * @file ThreadPoolLockFree.cpp
 * @brief Implementation of lock-free ThreadPool.
 */

#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"
#include "src/utilities/concurrency/inc/ThreadConfig.hpp"

#include <cstdio>
#include <system_error>
#include <thread>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPoolLockFree Methods ----------------------------- */

ThreadPoolLockFree::ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity,
                                       const PoolConfig& config)
    : taskQueue_(queueCapacity) {
  spawnWorkers(numThreads, config);
}

void ThreadPoolLockFree::spawnWorkers(std::size_t numThreads, const PoolConfig& config) {
  // The workers inherit the blocked process signals; the scope hands the
  // constructing thread its own mask back when the spawn is over, success
  // or not.
  WorkerSignalScope signals;
  workers_.reserve(numThreads);
  try {
    for (std::size_t i = 0; i < numThreads; ++i) {
      workers_.emplace_back([this, config, i]() {
        if (!applyWorkerConfig(config, i)) {
          configFailures_.fetch_add(1, std::memory_order_acq_rel);
        }
        started_.fetch_add(1, std::memory_order_release);
        workerLoop();
      });
    }
  } catch (const std::system_error&) {
    // A thread could not start (the process or thread limit): the pool is
    // reported invalid rather than left with joinable threads to terminate on.
    shutdown();
    return;
  }
  // Every worker has applied its configuration before the pool is handed
  // over, so workerConfigFailures() is final for the owner's start-up log.
  while (started_.load(std::memory_order_acquire) < workers_.size()) {
    std::this_thread::yield();
  }
}

ThreadPoolLockFree::~ThreadPoolLockFree() noexcept { shutdown(); }

PoolStatus ThreadPoolLockFree::tryEnqueue(const char* label, TaskFn task) noexcept {
  // Register as an in-flight producer before reading stop: shutdown() sets
  // stop and then waits for this count to reach zero, so a producer that read
  // stop clear pushes and wakes a worker before the workers are told to exit.
  // Both sides are seq_cst so neither can miss the other's store.
  producers_.value.fetch_add(1, std::memory_order_seq_cst);
  if (stop_.value.load(std::memory_order_seq_cst)) {
    producers_.value.fetch_sub(1, std::memory_order_seq_cst);
    return PoolStatus::POOL_STOPPED;
  }

  TaskItem item{label, task};
  const bool PUSHED = taskQueue_.tryPush(std::move(item));
  if (PUSHED) {
    wakeupSem_.release();
  }
  producers_.value.fetch_sub(1, std::memory_order_seq_cst);
  return PUSHED ? PoolStatus::SUCCESS : PoolStatus::QUEUE_FULL;
}

void ThreadPoolLockFree::enqueueTask(const char* label, TaskFn task) noexcept {
  // Fire-and-forget version, ignores status.
  (void)tryEnqueue(label, task);
}

bool ThreadPoolLockFree::threadsRunning() const noexcept {
  return activeTasks_.value.load(std::memory_order_acquire) > 0;
}

void ThreadPoolLockFree::shutdown() noexcept {
  // Set stop flag.
  if (stop_.value.exchange(true, std::memory_order_seq_cst)) {
    return; // Already stopped.
  }

  // Let the producers already past the stop check finish their push: each
  // one either saw stop and returned POOL_STOPPED, or pushes and wakes a
  // worker before leaving the count. A producer's stay is a few instructions.
  while (producers_.value.load(std::memory_order_seq_cst) != 0) {
    std::this_thread::yield();
  }

  // Wake all workers so they can exit once the queue is drained.
  wakeupSem_.release(workers_.size());

  // Join all workers.
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
  for (;;) {
    // Wait for work or stop signal.
    wakeupSem_.acquire();

    // Try to pop a task first (drain queue before checking stop).
    TaskItem item;
    if (!taskQueue_.tryPop(item)) {
      // No task available - check if we should exit.
      if (stop_.value.load(std::memory_order_acquire)) {
        return;
      }
      // Spurious wakeup - continue waiting.
      continue;
    }

    if (!item.task) {
      continue; // Ignore empty slots defensively.
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
