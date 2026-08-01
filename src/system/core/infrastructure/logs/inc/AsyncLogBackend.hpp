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
 * @note Messages exceeding MAX_MSG_LEN are truncated (no allocation).
 */
struct LogEntry {
  static constexpr std::size_t MAX_MSG_LEN = 512;

  char message[MAX_MSG_LEN]; ///< Pre-allocated message buffer (no heap)
  std::uint16_t length{0};   ///< Actual message length

  LogEntry() noexcept = default;

  /**
   * @brief Construct entry from string_view (copies up to MAX_MSG_LEN chars).
   * @param msg Message to copy (truncated if > MAX_MSG_LEN).
   */
  explicit LogEntry(std::string_view msg) noexcept;
};

/**
 * @class AsyncLogBackend
 * @brief Async logging backend with dedicated I/O thread.
 *
 * Lifecycle:
 *  1. Construct with file path
 *  2. start() - launches I/O thread
 *  3. tryLog() - RT-safe logging from any thread
 *  4. stop() - graceful shutdown, flushes remaining entries
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
   * @brief Stop I/O thread and flush remaining entries.
   * @return true if clean shutdown; false if timeout occurred.
   *
   * Blocks until queue is drained.
   * Not thread-safe with start().
   * @note NOT RT-safe: Blocks until queue drains, joins thread.
   */
  bool stop() noexcept;

  /**
   * @brief Flush queued entries to disk without stopping I/O thread.
   * @param timeoutMs Maximum time to wait for queue drain (default 5000ms).
   * @return true if queue fully drained; false on timeout.
   *
   * Waits until queueDepth reaches zero, then syncs file to disk.
   * Does not stop the I/O thread - logging continues afterward.
   * Safe to call from any thread.
   * @note NOT RT-safe: Blocks until queue drains.
   */
  bool flush(std::uint32_t timeoutMs = 5000) noexcept;

  /**
   * @brief Try to enqueue log entry (RT-safe, lock-free).
   * @param msg Formatted log message (truncated to 512 bytes if longer).
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
   * @brief Get approximate number of entries currently queued.
   * @return Queue depth (may be stale due to concurrent access).
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

private:
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
  std::atomic<std::size_t> queueDepth_{0};       ///< Approximate queue size
  std::atomic<std::uint64_t> entriesWritten_{0}; ///< Total entries written
  std::atomic<std::uint64_t> writeErrors_{0};    ///< write() failures on drain
};

} // namespace logs

#endif // APEX_SYSTEM_LOGS_ASYNC_LOG_BACKEND_HPP