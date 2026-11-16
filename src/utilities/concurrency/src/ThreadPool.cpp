/**
 * @file ThreadPool.cpp
 * @brief Implementation of ThreadPool concurrent task execution.
 */

#include "src/utilities/concurrency/inc/ThreadPool.hpp"

#include <mutex>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPool Methods ----------------------------- */

ThreadPool::ThreadPool(std::size_t numThreads, const PoolConfig& config) {
  if (!spawnWorkers(numThreads, config, "pool_w", &ThreadPool::workerEntry, this)) {
    // A thread could not start: the pool is reported invalid rather than left
    // with a partial set of workers.
    shutdown();
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

void ThreadPool::shutdown() noexcept {
  {
    // Set under the queue mutex, so a worker between its check and its wait cannot miss it.
    const std::lock_guard<std::mutex> LOCK(queueMutex_);
    stop_.value.store(true, std::memory_order_release);
  }
  queueCv_.notify_all();
  joinWorkers();
}

/* ----------------------------- File Helpers ----------------------------- */

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

    runTask(item.first, item.second);
  }
}

} // namespace concurrency
} // namespace apex
