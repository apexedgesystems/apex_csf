/**
 * @file ThreadPoolLockFree.cpp
 * @brief Implementation of lock-free ThreadPool.
 */

#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"
#include "src/utilities/concurrency/inc/ThreadConfig.hpp"

#include <cstdio>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <pthread.h>
#endif

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPoolLockFree Methods ----------------------------- */

ThreadPoolLockFree::ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity,
                                       const PoolConfig& config)
    : taskQueue_(queueCapacity) {
  blockSignals();
  workers_.reserve(numThreads);
  for (std::size_t i = 0; i < numThreads; ++i) {
    workers_.emplace_back([this, config, i]() {
      applyWorkerConfig(config, i);
      workerLoop();
    });
  }
}

ThreadPoolLockFree::~ThreadPoolLockFree() noexcept { shutdown(); }

PoolStatus ThreadPoolLockFree::tryEnqueue(const char* label, TaskFn task) noexcept {
  if (stop_.value.load(std::memory_order_acquire)) {
    return PoolStatus::POOL_STOPPED;
  }

  TaskItem item{label, task};
  if (!taskQueue_.tryPush(std::move(item))) {
    return PoolStatus::QUEUE_FULL;
  }

  wakeupSem_.release();
  return PoolStatus::SUCCESS;
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
  if (stop_.value.exchange(true, std::memory_order_acq_rel)) {
    return; // Already stopped.
  }

  // Wake all workers so they can exit.
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

void ThreadPoolLockFree::blockSignals() {
#if defined(__unix__) || defined(__APPLE__)
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGSEGV);
  sigaddset(&set, SIGPIPE);
  sigaddset(&set, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
#endif
}

void ThreadPoolLockFree::applyWorkerConfig(const PoolConfig& config, std::size_t workerId) {
  // Generate worker thread name (max 15 chars + null on Linux).
  char name[16];
  std::snprintf(name, sizeof(name), "lf_pool_w%zu", workerId);

  // Use common thread config utility for affinity/policy/priority.
  applyPoolConfig(config, name);
}

} // namespace concurrency
} // namespace apex
