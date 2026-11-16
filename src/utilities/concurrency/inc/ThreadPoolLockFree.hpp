#ifndef APEX_UTILITIES_CONCURRENCY_THREAD_POOL_LOCK_FREE_HPP
#define APEX_UTILITIES_CONCURRENCY_THREAD_POOL_LOCK_FREE_HPP
/**
 * @file ThreadPoolLockFree.hpp
 * @brief Lock-free thread pool using bounded MPMC queue.
 *
 * Design goals:
 *  - Lock-free task submission (no mutex on push path).
 *  - Idle workers park on a futex (no busy spin, no stored wake per task).
 *  - Bounded capacity for predictable memory usage.
 *  - Every task accepted runs before shutdown() returns.
 *  - Same API as ThreadPool where possible.
 *
 * Trade-offs vs ThreadPool:
 *  - Pro: No mutex on the submission path.
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
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/inc/PoolBase.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace apex {
namespace concurrency {

/* ----------------------------- ThreadPoolLockFree ----------------------------- */

/**
 * @class ThreadPoolLockFree
 * @brief Lock-free thread pool with bounded MPMC queue.
 *
 * Uses LockFreeQueue for task storage. Task submission takes no lock and
 * allocates nothing; it may issue a futex wake for an idle worker. A worker
 * that finds the ring empty parks until a producer publishes or shutdown()
 * begins. The workers, the in-flight count and the task errors are
 * PoolBase's: isValid(), workerCount(), threadsRunning(),
 * workerConfigFailures(), hasErrors() and collectErrors() are inherited.
 */
class ThreadPoolLockFree : public PoolBase {
public:
  /**
   * @brief Construct a lock-free thread pool.
   * @param numThreads Number of worker threads to spawn.
   * @param queueCapacity Maximum pending tasks (rounded to power-of-two,
   *        at most MAX_QUEUE_CAPACITY).
   * @param config Optional RT configuration applied to all workers at startup.
   * @note NOT RT-safe: Spawns threads, allocates queue storage.
   */
  explicit ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity = 1024,
                              const PoolConfig& config = {});

  /// The largest ring the pool builds: half a turn of the 31-bit wake
  /// counter, which leaves the other half for producer calls in progress
  /// (see WakeState for the bound this is one term of).
  static constexpr std::size_t MAX_QUEUE_CAPACITY = std::size_t{1} << 30;

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
   * @note RT-DEPENDS: no allocation and no pool mutex: one read-modify-write
   *       to enter the producer count, the lock-free push, one to advance the
   *       wake generation and one to leave. Waking an idle worker issues a
   *       futex syscall, and the push's compare-and-swap has no per-caller
   *       retry bound, so enqueue latency is not bounded per caller. SUCCESS
   *       means a worker runs the task before shutdown() returns.
   */
  [[nodiscard]] PoolStatus tryEnqueue(const char* label, TaskFn task) noexcept;

  /**
   * @brief Enqueue a task, ignoring status (for API compatibility).
   * @param label Static or long-lived string identifying the task (non-owning).
   * @param task Delegate encapsulating the task logic.
   * @note RT-DEPENDS: As tryEnqueue().
   * @warning Silently drops task if queue is full or pool is stopped.
   */
  void enqueueTask(const char* label, TaskFn task) noexcept;

  /**
   * @brief Signal shutdown and join all worker threads.
   *
   * Two stages. Admission closes first, so tryEnqueue() returns POOL_STOPPED;
   * then shutdown() parks until the producers admitted before the close have
   * published, sets the quiescent mark in the word the workers wait on and
   * wakes them. A worker exits only when it finds the ring empty after
   * seeing that mark, so every task reported as SUCCESS has run when
   * shutdown() returns.
   *
   * @note NOT RT-safe: parks on the producers, joins threads.
   */
  void shutdown() noexcept;

private:
  struct TaskItem {
    const char* label{nullptr};
    TaskFn task{};
  };

  /// White-box access for the lifecycle regressions in utst/, which build
  /// the admission and ring states the shutdown protocol has to survive.
  friend struct ThreadPoolLockFreeTestAccess;

  /// admission_ is one word: the low bits count the producers inside
  /// tryEnqueue(), the top bit is shutdown()'s first stage. A producer
  /// enters and reads the stage in one read-modify-write, so it either sees
  /// the close or is counted before shutdown() can see zero producers.
  static constexpr std::uint32_t CLOSED =
      1U << 31; ///< tryEnqueue() rejects; nothing new is published.
  static constexpr std::uint32_t PRODUCERS = CLOSED - 1; ///< The producer count's bits.

  /// The wake word's two fields (see WakeState).
  static constexpr std::uint32_t QUIESCENT = 1U; ///< Every admitted producer has published.
  static constexpr std::uint32_t ADVANCE = 2U;   ///< One publication, in the counter's bits.

  /// What an idle worker parks on. `generation` is one 32-bit word, the
  /// width the futex compares: bit 0 is the quiescent mark and the 31 bits
  /// above it count publications. It is not a count of tasks to work off: a
  /// producer adds ADVANCE after publishing and a worker parks only while
  /// the word still equals the value it read before its last look at the
  /// ring, so a publication either is seen by that look or has moved the
  /// word. Nothing is stored per task, and a worker that wakes to an empty
  /// ring parks again after two failed pops, however much it has delivered.
  /// `idle` counts the workers parked or about to park; a producer issues
  /// the futex wake only when it is non-zero.
  ///
  /// The mark is what ends the wait for good. shutdown() sets it once,
  /// after the admitted producers have published, and nothing clears it;
  /// advancing the counter never touches it. Every value a worker read
  /// before shutdown() has the mark clear and every value after has it
  /// set, so no count of publications in between, a full turn of the
  /// counter included, can make the word equal a value saved earlier: a
  /// worker delayed before its wait finds the word changed, and one that
  /// reads the mark does not wait.
  ///
  /// The counter itself turns over after 2^31 advances, and a worker
  /// delayed that long between its last look and its park does park
  /// against a counter that moved. That is harmless while some other
  /// worker's view is current. Every worker is stale at once only if 2^31
  /// advances follow the last worker's read of the word with no pop after
  /// it. Such an advance belongs either to a task still in the ring, at
  /// most C = MAX_QUEUE_CAPACITY of them, or to a task popped before that
  /// read whose producer had not signalled yet, at most D, one per
  /// tryEnqueue() call in progress. So one worker's view is current while
  /// C + D < 2^31, which holds while fewer than 2^30 tryEnqueue() calls are
  /// in progress at once. Each such call is its own thread; that many
  /// threads is the caller's limit to keep, and beyond what a Linux
  /// process can create.
  struct WakeState {
    std::atomic<std::uint32_t> generation{0};
    std::atomic<std::uint32_t> idle{0};
  };

  void workerLoop(WorkerTally& tally);
  static void workerEntry(void* pool, WorkerTally& tally) {
    static_cast<ThreadPoolLockFree*>(pool)->workerLoop(tally);
  }
  /// Tell the workers the ring changed: advance the generation, and wake
  /// one worker if any is parked or parking.
  void signalWork() noexcept;
  /// Leave the producer count; the last one out after the close wakes shutdown().
  void leaveAdmission() noexcept;
  LockFreeQueue<TaskItem> taskQueue_; ///< Task ring (lock-free MPMC).

  // Hot atomics isolated to cache lines.
  cache::AlignCl<std::atomic<std::uint32_t>> admission_{0};
  cache::AlignCl<WakeState> wake_{};
};

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_THREAD_POOL_LOCK_FREE_HPP
