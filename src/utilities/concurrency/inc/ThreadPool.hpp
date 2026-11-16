#ifndef APEX_UTILITIES_CONCURRENCY_THREAD_POOL_HPP
#define APEX_UTILITIES_CONCURRENCY_THREAD_POOL_HPP
/**
 * @file ThreadPool.hpp
 * @brief Real-time optimized thread pool for concurrent task execution.
 *
 * Design goals:
 *  - Zero-allocation tasks via DelegateU8 (fnptr + context).
 *  - Condition-variable queue for predictable wakeups (no busy spin).
 *  - Cacheline-aligned hot atomics to reduce false sharing.
 *  - Workers block the process signals; the constructing thread keeps its mask (POSIX).
 *  - Graceful shutdown with queue draining.
 */

#include "src/utilities/concurrency/inc/Cache.hpp"
#include "src/utilities/concurrency/inc/PoolBase.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPool ----------------------------- */

/**
 * @class ThreadPool
 * @brief Real-time friendly thread pool for concurrent task execution.
 *
 * Uses mutex + condition-variable protected FIFO for dispatch. Tasks are
 * represented by DelegateU8 to avoid allocations and type erasure overhead.
 * The workers, the in-flight count and the task errors are PoolBase's:
 * isValid(), workerCount(), threadsRunning(), workerConfigFailures(),
 * hasErrors() and collectErrors() are inherited.
 */
class ThreadPool : public PoolBase {
public:
  /**
   * @brief Construct a thread pool with the specified number of workers.
   * @param numThreads Number of worker threads to spawn.
   * @param config Optional RT configuration applied to all workers at startup.
   * @note NOT RT-safe: Spawns threads, allocates worker storage.
   */
  explicit ThreadPool(std::size_t numThreads, const PoolConfig& config = {});

  /**
   * @brief Destructor calls shutdown() if not already stopped.
   * @note NOT RT-safe: Joins worker threads.
   */
  ~ThreadPool() noexcept;

  // Non-copyable / non-movable (owns threads and synchronization primitives).
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  /**
   * @brief Enqueue a task for execution.
   * @param label Static or long-lived string identifying the task (non-owning).
   * @param task Delegate encapsulating the task logic.
   * @note NOT RT-safe: Acquires mutex, std::queue may allocate on growth.
   */
  void enqueueTask(const char* label, TaskFn task);

  /**
   * @brief Try to enqueue a task, returning status.
   * @param label Static or long-lived string identifying the task (non-owning).
   * @param task Delegate encapsulating the task logic.
   * @return SUCCESS if enqueued, POOL_STOPPED if shutting down.
   * @note NOT RT-safe: Acquires mutex, std::queue may allocate on growth.
   */
  [[nodiscard]] PoolStatus tryEnqueue(const char* label, TaskFn task);

  /**
   * @brief Signal shutdown and join all worker threads.
   * @note NOT RT-safe: Joins threads, blocks until queue is drained.
   */
  void shutdown() noexcept;

private:
  void workerLoop(WorkerTally& tally);
  static void workerEntry(void* pool, WorkerTally& tally) {
    static_cast<ThreadPool*>(pool)->workerLoop(tally);
  }

  std::queue<std::pair<const char*, TaskFn>> taskQueue_; ///< FIFO of pending tasks.
  std::mutex queueMutex_;                                ///< Protects taskQueue_.
  std::condition_variable queueCv_;                      ///< Worker wakeup signal.

  cache::AlignCl<std::atomic<bool>> stop_{false}; ///< Stop flag, on a cache line of its own.
};

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_THREAD_POOL_HPP
