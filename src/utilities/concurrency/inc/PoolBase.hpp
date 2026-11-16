#ifndef APEX_UTILITIES_CONCURRENCY_POOL_BASE_HPP
#define APEX_UTILITIES_CONCURRENCY_POOL_BASE_HPP
/**
 * @file PoolBase.hpp
 * @brief What the two thread pools share: the worker threads and their
 *        start-up, the count of tasks in flight, and the errors tasks report.
 *
 * ThreadPool and ThreadPoolLockFree differ in their queue, in how a worker is
 * woken and in how the pool stops. Everything else is here once.
 */

#include "src/utilities/concurrency/inc/Cache.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <atomic>
#include <cstddef>
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
 * @brief Status codes for thread pool operations and task errors.
 */
enum class PoolStatus : std::uint8_t {
  SUCCESS = 0,      ///< Operation completed successfully.
  TASK_FAILED = 1,  ///< Task returned non-zero error code.
  POOL_STOPPED = 2, ///< Pool is shutting down, task rejected.
  QUEUE_FULL = 3    ///< Queue is full, task rejected (lock-free pool only).
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

/* ----------------------------- PoolBase ----------------------------- */

/**
 * @class PoolBase
 * @brief Workers, in-flight count and task errors of a thread pool.
 *
 * A pool derives from this, starts its workers on its own loop with
 * spawnWorkers(), runs each task it takes off its queue with runTask(), and
 * joins the workers from its shutdown(). A task reports failure through its
 * return code: its type is a noexcept function pointer, so no exception
 * reaches the pool from a task.
 */
class PoolBase {
public:
  using TaskFn = DelegateU8; ///< Zero-allocation callable: uint8_t(void*).

  // Non-copyable / non-movable (owns threads and synchronization primitives).
  PoolBase(const PoolBase&) = delete;
  PoolBase& operator=(const PoolBase&) = delete;
  PoolBase(PoolBase&&) = delete;
  PoolBase& operator=(PoolBase&&) = delete;

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
   * @brief Check if any worker is currently executing a task.
   * @return True if at least one task is in-flight.
   * @note RT-safe: Single atomic load with acquire ordering.
   */
  [[nodiscard]] bool threadsRunning() const noexcept {
    return activeTasks_.value.load(std::memory_order_acquire) > 0;
  }

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
   * @brief Check if any errors have been captured (lock-free).
   * @return True if there are pending errors to collect.
   * @note RT-safe: Single atomic load, no mutex.
   */
  [[nodiscard]] bool hasErrors() const noexcept {
    return hasErrors_.value.load(std::memory_order_acquire);
  }

  /**
   * @brief Drain and return any task errors captured by workers.
   * @return Queue of errors (moved out, internal queue cleared).
   * @note NOT RT-safe: Acquires mutex, moves queue.
   */
  [[nodiscard]] std::queue<TaskError> collectErrors();

protected:
  PoolBase() = default;
  ~PoolBase() = default;

  /// A worker's body: called once on each worker thread with the pool it belongs to.
  using WorkerEntry = void (*)(void* pool);

  /**
   * @brief Start the workers.
   * @param numThreads Number of worker threads.
   * @param config Settings each worker applies to itself before its loop.
   * @param namePrefix Worker i is named `<namePrefix><i>` (15 characters at most on Linux).
   * @param entry The worker's body.
   * @param pool Passed to `entry`.
   * @return True when every worker started. False when a thread could not
   *         start (the process or thread limit): the workers that did start
   *         are running `entry`, and the caller stops them and calls
   *         joinWorkers(), which leaves the pool invalid.
   *
   * Returns only once every started worker has applied its configuration, so
   * workerConfigFailures() is final for the owner's start-up log. The workers
   * inherit the blocked process signals; the calling thread's mask is as it
   * was when this returns.
   * @note NOT RT-safe: Spawns threads, allocates worker storage.
   */
  [[nodiscard]] bool spawnWorkers(std::size_t numThreads, const PoolConfig& config,
                                  const char* namePrefix, WorkerEntry entry, void* pool);

  /**
   * @brief Join every worker and forget them. The pool is not valid afterwards.
   * @note NOT RT-safe: Joins threads. The caller has already told its workers to stop.
   */
  void joinWorkers() noexcept;

  /**
   * @brief Run one task on the calling worker: count it in flight, call it,
   *        record a non-zero return code as a TASK_FAILED error. An empty
   *        delegate returns zero from its call, so it runs as a task that
   *        succeeds.
   * @note RT-safe unless the task fails: two atomic updates around the call.
   *       Recording a failure takes the error mutex and may allocate.
   */
  void runTask(const char* label, TaskFn task) {
    activeTasks_.value.fetch_add(1, std::memory_order_acq_rel);
    const std::uint8_t RC = task();
    if (RC != 0) {
      recordFailure(label, RC);
    }
    activeTasks_.value.fetch_sub(1, std::memory_order_acq_rel);
  }

private:
  void recordFailure(const char* label, std::uint8_t code);

  std::vector<std::thread> workers_;           ///< Worker threads.
  std::atomic<std::size_t> configFailures_{0}; ///< Workers with a setting that did not apply.

  cache::AlignCl<std::atomic<std::size_t>> activeTasks_{0}; ///< In-flight task count.

  // Error path (rare, mutex-protected)
  std::queue<TaskError> errorQueue_;                   ///< Collected errors.
  std::mutex errorMutex_;                              ///< Protects errorQueue_.
  cache::AlignCl<std::atomic<bool>> hasErrors_{false}; ///< Lock-free error flag.
};

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_POOL_BASE_HPP
