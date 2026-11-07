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
 *  - Every task accepted runs before shutdown() returns.
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
   * @note RT-safe: one read-modify-write to enter the producer count, the
   *       lock-free push, a semaphore release and one read-modify-write to
   *       leave; no lock. SUCCESS means a worker runs the task before
   *       shutdown() returns.
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
   * @brief Workers for which a requested setting (name, affinity, policy
   *        with priority) did not apply at start-up.
   * @return The count, 0 when every worker runs as configured. Final once
   *         the constructor returns: it waits for every worker to have
   *         applied its configuration.
   * @note RT-safe: Single atomic load. The settings are applied one by one
   *       and a failure leaves the others as they are, so a counted worker
   *       may run with part of its configuration; which setting failed is
   *       not recorded. A requested policy needs CAP_SYS_NICE and an
   *       affinity needs CPUs the process may use.
   */
  [[nodiscard]] std::size_t workerConfigFailures() const noexcept {
    return configFailures_.load(std::memory_order_acquire);
  }

  /**
   * @brief Signal shutdown and join all worker threads.
   *
   * Two stages. Admission closes first, so tryEnqueue() returns POOL_STOPPED;
   * then shutdown() parks until the producers admitted before the close have
   * published, marks the pool quiescent and wakes the workers. A worker
   * exits only when it finds the ring empty after seeing that mark, so every
   * task reported as SUCCESS has run when shutdown() returns.
   *
   * @note NOT RT-safe: parks on the producers, joins threads.
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

  /// White-box access for the lifecycle regressions in utst/, which build
  /// the admission and ring states the shutdown protocol has to survive.
  friend struct ThreadPoolLockFreeTestAccess;

  /// admission_ is one word: the low bits count the producers inside
  /// tryEnqueue(), the high bits are shutdown()'s two stages. A producer
  /// enters and reads the stage in one read-modify-write, so it either sees
  /// the close or is counted before shutdown() can see zero producers.
  static constexpr std::uint32_t CLOSED =
      1U << 31; ///< tryEnqueue() rejects; nothing new is published.
  static constexpr std::uint32_t QUIESCENT = 1U << 30; ///< Every admitted producer has published.
  static constexpr std::uint32_t PRODUCERS = QUIESCENT - 1; ///< The producer count's bits.

  void workerLoop();
  void runTask(TaskItem& item);
  /// Leave the producer count; the last one out after the close wakes shutdown().
  void leaveAdmission() noexcept;
  /// Spawn the workers; on a failed thread start, stop and join the ones
  /// that started and leave workers_ empty.
  void spawnWorkers(std::size_t numThreads, const PoolConfig& config);
  [[nodiscard]] static bool applyWorkerConfig(const PoolConfig& config, std::size_t workerId);

  // Task queue (lock-free MPMC)
  LockFreeQueue<TaskItem> taskQueue_;
  /// Wake hints for the workers: one per published task, one per worker at
  /// shutdown. A woken worker drains the ring rather than taking one task.
  Semaphore wakeupSem_{0};

  // Worker infrastructure
  std::vector<std::thread> workers_;

  // Hot atomics isolated to cache lines.
  cache::AlignCl<std::atomic<std::uint32_t>> admission_{0};
  cache::AlignCl<std::atomic<std::size_t>> activeTasks_{0};
  std::atomic<std::size_t> configFailures_{0}; ///< Workers with a setting that did not apply.

  // Error path (rare, mutex-protected)
  std::queue<TaskError> errorQueue_;
  std::mutex errorMutex_;
  cache::AlignCl<std::atomic<bool>> hasErrors_{false};
};

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_THREAD_POOL_LOCK_FREE_HPP
