/**
 * @file ThreadPool.cpp
 * @brief Implementation of ThreadPool concurrent task execution.
 */

#include "src/utilities/concurrency/inc/ThreadPool.hpp"
#include "src/utilities/concurrency/inc/ThreadConfig.hpp"

#include <cstdio>
#include <system_error>
#include <thread>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPool Methods ----------------------------- */

ThreadPool::ThreadPool(std::size_t numThreads, const PoolConfig& config) {
  spawnWorkers(numThreads, config);
}

void ThreadPool::spawnWorkers(std::size_t numThreads, const PoolConfig& config) {
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

ThreadPool::~ThreadPool() noexcept { shutdown(); }

PoolStatus ThreadPool::tryEnqueue(const char* label, TaskFn task) {
  {
    std::lock_guard<std::mutex> lk(queueMutex_);
    if (stop_.value.load(std::memory_order_acquire)) {
      return PoolStatus::POOL_STOPPED;
    }
    taskQueue_.emplace(label, task);
  }
  queueCv_.notify_one();
  return PoolStatus::SUCCESS;
}

void ThreadPool::enqueueTask(const char* label, TaskFn task) {
  // Fire-and-forget version, ignores POOL_STOPPED status.
  (void)tryEnqueue(label, task);
}

bool ThreadPool::threadsRunning() const noexcept {
  return activeTasks_.value.load(std::memory_order_acquire) > 0;
}

void ThreadPool::shutdown() noexcept {
  {
    // Set stop and wake all workers so waiters can exit promptly.
    std::lock_guard<std::mutex> lk(queueMutex_);
    if (stop_.value.exchange(true, std::memory_order_acq_rel)) {
      // already stopped
    }
  }
  queueCv_.notify_all();

  for (auto& w : workers_) {
    if (w.joinable())
      w.join();
  }
  workers_.clear();
}

bool ThreadPool::hasErrors() const noexcept {
  return hasErrors_.value.load(std::memory_order_acquire);
}

std::queue<TaskError> ThreadPool::collectErrors() {
  std::lock_guard<std::mutex> lk(errorMutex_);
  std::queue<TaskError> out = std::move(errorQueue_);
  errorQueue_ = {};
  hasErrors_.value.store(false, std::memory_order_release);
  return out;
}

/* ----------------------------- File Helpers ----------------------------- */

bool ThreadPool::applyWorkerConfig(const PoolConfig& config, std::size_t workerId) {
  // Generate worker thread name (max 15 chars + null on Linux).
  char name[16];
  std::snprintf(name, sizeof(name), "pool_w%zu", workerId);

  // Use common thread config utility for affinity/policy/priority.
  return applyPoolConfig(config, name);
}

void ThreadPool::workerLoop() {
  for (;;) {
    std::pair<const char*, TaskFn> item;

    {
      std::unique_lock<std::mutex> lk(queueMutex_);

      // Park until either stop is set or there is work to do.
      queueCv_.wait(lk, [this]() {
        return stop_.value.load(std::memory_order_acquire) || !taskQueue_.empty();
      });

      if (stop_.value.load(std::memory_order_acquire) && taskQueue_.empty()) {
        return; // clean exit: no more work, stop requested
      }

      item = std::move(taskQueue_.front());
      taskQueue_.pop();
    }

    const char* label = item.first;
    TaskFn& task = item.second;
    if (!task)
      continue; // ignore empty slots defensively

    // Ensure activeTasks_ is decremented even if the task throws.
    activeTasks_.value.fetch_add(1, std::memory_order_acq_rel);
    struct DecOnExit {
      std::atomic<size_t>* p;
      ~DecOnExit() { p->fetch_sub(1, std::memory_order_acq_rel); }
    } dec{&activeTasks_.value};

    try {
      std::uint8_t rc = task();
      if (rc != 0) {
        std::lock_guard<std::mutex> el(errorMutex_);
        errorQueue_.push(TaskError{label, rc, PoolStatus::TASK_FAILED});
        hasErrors_.value.store(true, std::memory_order_release);
      }
    } catch (const std::exception& /*e*/) {
      // Exception details available via debugger; no I/O in RT context.
      std::lock_guard<std::mutex> el(errorMutex_);
      errorQueue_.push(TaskError{label, 254, PoolStatus::EXCEPTION_STD});
      hasErrors_.value.store(true, std::memory_order_release);
    } catch (...) {
      std::lock_guard<std::mutex> el(errorMutex_);
      errorQueue_.push(TaskError{label, 255, PoolStatus::EXCEPTION_UNKNOWN});
      hasErrors_.value.store(true, std::memory_order_release);
    }
  }
}

} // namespace concurrency
} // namespace apex
