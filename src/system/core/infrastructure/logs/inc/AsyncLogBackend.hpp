#ifndef APEX_SYSTEM_LOGS_ASYNC_LOG_BACKEND_HPP
#define APEX_SYSTEM_LOGS_ASYNC_LOG_BACKEND_HPP
/**
 * @file AsyncLogBackend.hpp
 * @brief Lock-free async logging backend for real-time safety.
 *
 * Architecture:
 *  - RT threads push log entries to lock-free queue (never blocks)
 *  - Dedicated I/O thread drains queue and writes to disk (can block)
 *  - Configurable ring buffer size (default 4096 entries)
 *  - Overflow strategy: drop newest entry with counter
 *
 * Real-time guarantees:
 *  - tryLog() completes in O(1) bounded time (~250-750ns typical)
 *  - Never blocks on disk I/O
 *  - Never allocates memory in hot path
 *  - Lock-free MPMC queue for thread safety
 *
 * Optimizations:
 *  - Batched notifications: Only wakes I/O thread when queue was empty
 *  - Zero-copy interface: string_view avoids unnecessary allocations
 *  - Efficient flush: Condition variable instead of busy-wait polling
 *
 * Wakeup path (Linux): producers signal the I/O thread through an eventfd
 * write -- a non-blocking syscall with counter semantics -- so the RT hot
 * path never shares a lock with the I/O thread or flush()/stop() callers.
 * The condition variable remains for control-plane waiters only (flush
 * drain notification). Non-Linux builds keep the mutex+notify producer
 * path.
 */

#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace logs {

class LogDrainService;

/**
 * @struct LogEntry
 * @brief Self-contained log entry for async queue.
 *
 * Size: ~520 bytes (512 for message + overhead)
 * Capacity: 4096 entries = ~2.1MB memory per backend
 *
 * @note A message of MAX_MSG_LEN bytes or more is cut to MAX_MSG_LEN - 1
 *       bytes (no allocation). When the cut message ended in a newline,
 *       the last stored byte is that newline.
 */
struct LogEntry {
  static constexpr std::size_t MAX_MSG_LEN = 512;

  char message[MAX_MSG_LEN]; ///< Pre-allocated message buffer (no heap)
  std::uint16_t length{0};   ///< Actual message length

  LogEntry() noexcept = default;

  /**
   * @brief Construct entry from string_view (copies up to MAX_MSG_LEN - 1 chars).
   * @param msg Message to copy (cut if it does not fit; a cut line keeps
   *            its newline).
   */
  explicit LogEntry(std::string_view msg) noexcept;
};

/**
 * @class AsyncLogBackend
 * @brief Async logging backend: a lock-free ring emptied by the shared
 *        drain service, or by a dedicated I/O thread when the service's
 *        registry is full.
 *
 * Lifecycle:
 *  1. Construct with file path
 *  2. start() - opens the file, joins the drain (or starts the I/O thread)
 *  3. tryLog() - RT-safe logging from any thread
 *  4. stop() - graceful shutdown, writes remaining entries
 *
 * Thread-safety:
 *  - tryLog() is lock-free and RT-safe
 *  - start()/stop() are not thread-safe (call from single thread)
 *  - I/O thread is internal and managed automatically
 */
class AsyncLogBackend {
public:
  /**
   * @brief Construct async backend for given log file.
   * @param logPath Path to log file.
   * @param queueSize Ring buffer capacity (default 4096).
   *
   * Does not open file or start I/O thread. Call start() explicitly.
   * @note NOT RT-safe: May allocate queue memory.
   */
  explicit AsyncLogBackend(std::string logPath, std::size_t queueSize = 4096) noexcept;

  /**
   * @brief Destructor ensures clean shutdown.
   *
   * Calls stop() if not already stopped. This may block to flush remaining entries.
   */
  ~AsyncLogBackend() noexcept;

  // Non-copyable, non-movable (owns I/O thread)
  AsyncLogBackend(const AsyncLogBackend&) = delete;
  AsyncLogBackend& operator=(const AsyncLogBackend&) = delete;
  AsyncLogBackend(AsyncLogBackend&&) = delete;
  AsyncLogBackend& operator=(AsyncLogBackend&&) = delete;

  /**
   * @brief Start I/O thread and open log file.
   * @return true on success; false if already running or file open failed.
   *
   * Not thread-safe. Call once from initialization code.
   * @note NOT RT-safe: Opens file, spawns thread.
   */
  bool start() noexcept;

  /**
   * @brief Stop draining and attempt the write of remaining entries.
   * @return true if every accepted entry has had its write attempted;
   *         false if some were left.
   *
   * Blocks until queue is drained. Producers must have stopped logging:
   * an entry offered while stop() runs may be neither written nor counted.
   * A failed write is counted in writeErrorCount() and its entry is gone;
   * the return value does not report it, nor the result of the final sync.
   * Not thread-safe with start().
   * @note NOT RT-safe: Blocks until queue drains, joins thread.
   */
  bool stop() noexcept;

  /**
   * @brief Wait for accepted entries to be handed to the file, then sync,
   *        without stopping the drain.
   * @param timeoutMs Longest wait for the outstanding entries (default
   *        5000 ms). It bounds that wait only, not the sync that follows.
   * @return true if every entry accepted before the call has had its write
   *         attempted; false on timeout.
   *
   * An entry counts as outstanding from before it is published until the
   * drain has attempted its write, so the guarantee holds with several
   * producers logging while flush() runs; entries accepted during the call
   * may or may not be covered. A true return is not a durability
   * certificate: a write the file refused is counted in writeErrorCount()
   * and its entry is gone, and the result of the sync is not reported.
   * Logging continues afterward. Safe to call from any thread.
   * @note NOT RT-safe: Blocks until the outstanding entries are attempted.
   */
  bool flush(std::uint32_t timeoutMs = 5000) noexcept;

  /**
   * @brief Try to enqueue log entry (RT-safe, lock-free).
   * @param msg Formatted log message (cut to 511 bytes if longer, keeping
   *            a trailing newline; counted in truncatedCount()).
   * @return true if enqueued; false if queue full (entry dropped).
   *
   * RT guarantees:
   *  - Never blocks
   *  - Never allocates
   *  - O(1) bounded time
   *  - Lock-free for all threads
   *
   * Notification strategy: Only wakes I/O thread if queue was empty,
   * minimizing syscall overhead during high-frequency logging. The wake
   * itself is an eventfd write on Linux: non-blocking, lock-free, safe
   * against priority inversion.
   *
   * On failure (queue full): increments dropCount and returns false.
   * @note RT-safe: Lock-free MPMC enqueue; wake is a non-blocking syscall.
   */
  bool tryLog(std::string_view msg) noexcept;

  /**
   * @brief Get number of entries not accepted (queue full, or backend not
   *        running).
   * @return Dropped entry count (monotonic).
   * @note RT-safe: Atomic load.
   */
  std::uint64_t droppedCount() const noexcept {
    return droppedCount_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get number of accepted entries that were cut to fit an entry.
   * @return Truncated entry count (monotonic).
   * @note RT-safe: Atomic load.
   */
  std::uint64_t truncatedCount() const noexcept {
    return truncatedCount_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get approximate number of entries waiting on the ring.
   * @return Queue depth (may be stale due to concurrent access). An entry
   *         leaves it when the drain takes it off the ring, before it is
   *         written; flush() tells when accepted entries were attempted.
   * @note RT-safe: Atomic load.
   */
  std::size_t queueDepth() const noexcept { return queueDepth_.load(std::memory_order_relaxed); }

  /**
   * @brief Check if I/O thread is running.
   * @note RT-safe: Atomic load.
   */
  bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }

  /**
   * @brief Count of write() failures observed by the I/O thread.
   * @note RT-safe: Atomic load. Monotonic; disk-full shows up here.
   */
  std::uint64_t writeErrorCount() const noexcept {
    return writeErrors_.load(std::memory_order_relaxed);
  }

  /**
   * @brief True when the shared drain service empties this ring (no
   *        dedicated I/O thread).
   * @note RT-safe: simple read (set at start(), cleared at stop()).
   */
  bool usesSharedDrain() const noexcept { return service_ != nullptr; }

  /**
   * @brief Drain up to maxEntries from the ring to this backend's file.
   * @return Entries written.
   *
   * Called by the shared drain service's sweep (or internally during
   * stop() to empty leftovers). Single-consumer discipline: only the
   * owning drain thread -- or stop() after unregistration -- may call it.
   * @note NOT RT-safe: blocking file I/O.
   */
  std::size_t drainBatch(std::size_t maxEntries) noexcept;

protected:
  /**
   * @brief First step of tryLog(): count one entry as outstanding.
   * @return false when the queue is full (the refusal is counted).
   *
   * An admitted entry holds flush() and stop() back until publishAdmitted()
   * has handed it to the drain and its write has been attempted, or until
   * publishAdmitted() fails. Every admit() that returns true is followed by
   * exactly one publishAdmitted(). The two steps are separate members so a
   * test can stand between them, where a preempted producer would be.
   * @note RT-safe: Lock-free.
   */
  bool admit() noexcept;

  /**
   * @brief Second step of tryLog(): publish an admitted entry to the drain.
   * @return true if published; false if the ring refused it (counted as a
   *         drop, and the admission is retired).
   * @note RT-safe: Lock-free MPMC enqueue; wake is a non-blocking syscall.
   */
  bool publishAdmitted(std::string_view msg) noexcept;

private:
  /**
   * @brief Release one outstanding entry after its write was attempted, and
   *        notify flush() when it was the last.
   */
  void completeOne() noexcept;

  /**
   * @brief Block process signals in I/O thread (keeps them on shutdown thread).
   *
   * Called by I/O thread during startup to ensure signals like SIGINT/SIGTERM
   * are only delivered to the main shutdown thread, not the async logging thread.
   * Mirrors ThreadPool signal masking strategy.
   */
  void blockSignals() noexcept;

  /**
   * @brief I/O thread main loop.
   *
   * Drains queue and writes to disk. Can block on write().
   * Terminates when running_ becomes false and queue is empty.
   */
  void ioThreadLoop() noexcept;

  /**
   * @brief Write entry to disk (blocking I/O allowed).
   */
  void writeEntry(const LogEntry& entry) noexcept;

  std::string logPath_;                              ///< Target log file path
  apex::concurrency::LockFreeQueue<LogEntry> queue_; ///< Lock-free ring buffer
  std::thread ioThread_;                             ///< Dedicated I/O thread
  int logFd_{-1};                                    ///< File descriptor
  int wakeFd_{-1}; ///< eventfd the producers signal (Linux; -1 elsewhere)

  std::mutex stopMutex_;           ///< Guards stop condition
  std::condition_variable stopCv_; ///< Wakes I/O thread on stop

  std::size_t capacity_{0};           ///< Ring capacity (drop precheck bound)
  LogDrainService* service_{nullptr}; ///< Shared drain (null = own thread)

  std::atomic<bool> running_{false};             ///< I/O thread control
  std::atomic<std::uint64_t> droppedCount_{0};   ///< Entries not accepted
  std::atomic<std::uint64_t> truncatedCount_{0}; ///< Accepted entries cut to fit
  std::atomic<std::size_t> queueDepth_{0};       ///< Approximate queue size
  std::atomic<std::size_t> outstanding_{0};      ///< Admitted, write not yet attempted
  std::atomic<std::uint64_t> entriesWritten_{0}; ///< Total entries written
  std::atomic<std::uint64_t> writeErrors_{0};    ///< write() failures on drain
};

} // namespace logs

#endif // APEX_SYSTEM_LOGS_ASYNC_LOG_BACKEND_HPP