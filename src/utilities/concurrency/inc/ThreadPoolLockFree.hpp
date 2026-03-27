#ifndef APEX_UTILITIES_CONCURRENCY_THREAD_POOL_LOCK_FREE_HPP
#define APEX_UTILITIES_CONCURRENCY_THREAD_POOL_LOCK_FREE_HPP
/**
 * @file ThreadPoolLockFree.hpp
 * @brief Lock-free thread pool using bounded MPMC queue.
 *
 * Design goals:
 *  - Lock-free task submission (no mutex on push path).
 *  - Semaphore-based worker wakeup (no busy spin).
 *  - Bounded capacity for predictable memory usage.
 *  - Same API as ThreadPool where possible.
 *
 * Trade-offs vs ThreadPool:
 *  - Pro: 2-3x higher throughput under contention.
 *  - Pro: Predictable memory (fixed queue size).
 *  - Con: Bounded - tryEnqueue returns QUEUE_FULL if full.
 *  - Con: Must specify capacity at construction.
 *
 * Use when:
 *  - High task submission rate from multiple threads.
 *  - Bounded queue is acceptable.
 *  - Memory predictability is required.
 */

#include "src/utilities/concurrency/inc/Cache.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/inc/Semaphore.hpp"
#include "src/utilities/concurrency/inc/ThreadPool.hpp" // For PoolStatus, TaskError

#include <atomic>
#include <cstdint>
#include <queue>
#include <thread>
#include <vector>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPoolLockFree ----------------------------- */

/**
 * @class ThreadPoolLockFree
 * @brief Lock-free thread pool with bounded MPMC queue.
 *
 * Uses LockFreeQueue for task storage and Semaphore for worker wakeup.
 * Task submission is lock-free (CAS only), workers block on semaphore.
 */
class ThreadPoolLockFree {
public:
  using TaskFn = DelegateU8; ///< Zero-allocation callable: uint8_t(void*).

  /**
   * @brief Construct a lock-free thread pool.
   * @param numThreads Number of worker threads to spawn.
   * @param queueCapacity Maximum pending tasks (rounded to power-of-two).
   * @param config Optional RT configuration applied to all workers at startup.
   * @note NOT RT-safe: Spawns threads, allocates queue storage.
   */
  explicit ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity = 1024,
                              const PoolConfig& config = {});

  /**
   * @brief Destructor calls shutdown() if not already stopped.
   * @note NOT RT-safe: Joins worker threads.
   */
  ~ThreadPoolLockFree() noexcept;

  // Non-copyable / non-movable.
  ThreadPoolLockFree(const ThreadPoolLockFree&) = delete;
  ThreadPoolLockFree& operator=(const ThreadPoolLockFree&) = delete;
  ThreadPoolLockFree(ThreadPoolLockFree&&) = delete;
  ThreadPoolLockFree& operator=(ThreadPoolLockFree&&) = delete;

  /**
   * @brief Check if pool was constructed successfully.
   * @return True if at least one worker thread is running.
   * @note RT-safe: Checks worker count.
   */
  [[nodiscard]] bool isValid() const noexcept { return !workers_.empty(); }

  /**
   * @brief Get the number of worker threads.
   * @return Number of workers (0 if construction failed).
   * @note RT-safe: Returns stored size.
   */
  [[nodiscard]] std::size_t workerCount() const noexcept { return workers_.size(); }

  /**
   * @brief Get the queue capacity.
   * @return Maximum pending tasks.
   * @note RT-safe: Returns stored value.
   */
  [[nodiscard]] std::size_t capacity() const noexcept { return taskQueue_.capacity(); }

  /**
   * @brief Try to enqueue a task (lock-free).
   * @param label Static or long-lived string identifying the task (non-owning).
   * @param task Delegate encapsulating the task logic.
   * @return SUCCESS if enqueued, QUEUE_FULL if queue is full, POOL_STOPPED if shutting down.
   * @note RT-safe: Lock-free CAS + semaphore release.
   */
  [[nodiscard]] PoolStatus tryEnqueue(const char* label, TaskFn task) noexcept;

  /**
   * @brief Enqueue a task, ignoring status (for API compatibility).
   * @param label Static or long-lived string identifying the task (non-owning).
   * @param task Delegate encapsulating the task logic.
   * @note RT-safe: Lock-free CAS + semaphore release.
   * @warning Silently drops task if queue is full or pool is stopped.
   */
  void enqueueTask(const char* label, TaskFn task) noexcept;

  /**
   * @brief Check if any worker is currently executing a task.
   * @return True if at least one task is in-flight.
   * @note RT-safe: Single atomic load with acquire ordering.
   */
  [[nodiscard]] bool threadsRunning() const noexcept;

  /**
   * @brief Signal shutdown and join all worker threads.
   * @note NOT RT-safe: Joins threads, blocks until workers exit.
   */
  void shutdown() noexcept;

  /**
   * @brief Check if any errors have been captured (lock-free).
   * @return True if there are pending errors to collect.
   * @note RT-safe: Single atomic load, no mutex.
   */
  [[nodiscard]] bool hasErrors() const noexcept;

  /**
   * @brief Drain and return any task errors captured by workers.
   * @return Queue of errors (moved out, internal queue cleared).
   * @note NOT RT-safe: Acquires mutex, moves queue.
   */
  [[nodiscard]] std::queue<TaskError> collectErrors();

private:
  struct TaskItem {
    const char* label{nullptr};
    TaskFn task{};
  };

  void workerLoop();
  void blockSignals();
  static void applyWorkerConfig(const PoolConfig& config, std::size_t workerId);

  // Task queue (lock-free MPMC)
  LockFreeQueue<TaskItem> taskQueue_;
  Semaphore wakeupSem_{0}; ///< Signals workers when tasks available.

  // Worker infrastructure
  std::vector<std::thread> workers_;

  // Hot atomics isolated to cache lines.
  cache::AlignCl<std::atomic<bool>> stop_{false};
  cache::AlignCl<std::atomic<std::size_t>> activeTasks_{0};

  // Error path (rare, mutex-protected)
  std::queue<TaskError> errorQueue_;
  std::mutex errorMutex_;
  cache::AlignCl<std::atomic<bool>> hasErrors_{false};
};

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_THREAD_POOL_LOCK_FREE_HPP
