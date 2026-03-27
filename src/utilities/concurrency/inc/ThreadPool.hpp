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
 *  - Signal masking for worker stability (POSIX).
 *  - Graceful shutdown with queue draining.
 */

#include "src/utilities/concurrency/inc/Cache.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace apex {
namespace concurrency {

/* ----------------------------- PoolStatus ----------------------------- */

/**
 * @enum PoolStatus
 * @brief Status codes for ThreadPool operations and task errors.
 */
enum class PoolStatus : std::uint8_t {
  SUCCESS = 0,            ///< Operation completed successfully.
  TASK_FAILED = 1,        ///< Task returned non-zero error code.
  POOL_STOPPED = 2,       ///< Pool is shutting down, task rejected.
  QUEUE_FULL = 3,         ///< Queue is full, task rejected (lock-free pool only).
  EXCEPTION_STD = 254,    ///< Task threw std::exception.
  EXCEPTION_UNKNOWN = 255 ///< Task threw unknown exception type.
};

/* ----------------------------- TaskError ----------------------------- */

/**
 * @struct TaskError
 * @brief Captures task execution errors reported by workers.
 */
struct TaskError {
  const char* label;      ///< Non-owning pointer to task label (stable storage).
  std::uint8_t errorCode; ///< Task return code (0 = success, see PoolStatus).
  PoolStatus status;      ///< Categorized error status.
};

/* ----------------------------- PoolConfig ----------------------------- */

/**
 * @struct PoolConfig
 * @brief Configuration applied to all workers at pool construction.
 *
 * Workers are configured once at startup (not per-task), eliminating
 * 10-30us syscall overhead per task dispatch.
 */
struct PoolConfig {
  std::int8_t policy{0};                ///< POSIX policy (SCHED_OTHER=0, SCHED_FIFO=1, SCHED_RR=2).
  std::int8_t priority{0};              ///< POSIX priority (0 for SCHED_OTHER, 1-99 for RT).
  std::vector<std::uint8_t> affinity{}; ///< CPU affinity (empty = all CPUs).
};

/* ----------------------------- ThreadPool ----------------------------- */

/**
 * @class ThreadPool
 * @brief Real-time friendly thread pool for concurrent task execution.
 *
 * Uses mutex + condition-variable protected FIFO for dispatch. Tasks are
 * represented by DelegateU8 to avoid allocations and type erasure overhead.
 */
class ThreadPool {
public:
  using TaskFn = DelegateU8; ///< Zero-allocation callable: uint8_t(void*).

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
   * @brief Check if any worker is currently executing a task.
   * @return True if at least one task is in-flight.
   * @note RT-safe: Single atomic load with acquire ordering.
   */
  [[nodiscard]] bool threadsRunning() const noexcept;

  /**
   * @brief Signal shutdown and join all worker threads.
   * @note NOT RT-safe: Joins threads, blocks until queue is drained.
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
  void workerLoop();
  void blockSignals();
  static void applyWorkerConfig(const PoolConfig& config, std::size_t workerId);

  // Worker infrastructure
  std::vector<std::thread> workers_;                     ///< Worker threads.
  std::queue<std::pair<const char*, TaskFn>> taskQueue_; ///< FIFO of pending tasks.
  std::mutex queueMutex_;                                ///< Protects taskQueue_.
  std::condition_variable queueCv_;                      ///< Worker wakeup signal.

  // Hot atomics isolated to cache lines to avoid false sharing.
  cache::AlignCl<std::atomic<bool>> stop_{false};           ///< Stop flag.
  cache::AlignCl<std::atomic<std::size_t>> activeTasks_{0}; ///< In-flight task count.

  // Error path (rare, mutex-protected)
  std::queue<TaskError> errorQueue_;                   ///< Collected errors.
  std::mutex errorMutex_;                              ///< Protects errorQueue_.
  cache::AlignCl<std::atomic<bool>> hasErrors_{false}; ///< Lock-free error flag.
};

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_THREAD_POOL_HPP
