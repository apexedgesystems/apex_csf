/**
 * @file AsyncLogBackend.cpp
 * @brief Implementation of lock-free async logging backend.
 */

#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"
#include "src/system/core/infrastructure/logs/inc/LogDrainService.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/eventfd.h>
#endif

// POSIX signal masking
#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <csignal>
#endif

namespace logs {

/* ----------------------------- LogEntry Methods ----------------------------- */

LogEntry::LogEntry(std::string_view msg) noexcept {
  // A message that does not fit is cut to the buffer. A cut line keeps its
  // terminator in the last stored byte, so every entry written from a
  // terminated message is still one terminated line.
  const std::size_t copyLen = std::min(msg.size(), MAX_MSG_LEN - 1);
  std::memcpy(message, msg.data(), copyLen);
  if (msg.size() > copyLen && msg.back() == '\n') {
    message[copyLen - 1] = '\n';
  }
  message[copyLen] = '\0';
  length = static_cast<std::uint16_t>(copyLen);
}

/* ----------------------------- AsyncLogBackend Methods ----------------------------- */

AsyncLogBackend::AsyncLogBackend(std::string logPath, std::size_t queueSize) noexcept
    : logPath_(std::move(logPath)), queue_(queueSize), capacity_(queueSize) {
  // No file operations in constructor - defer to start()
}

AsyncLogBackend::~AsyncLogBackend() noexcept {
  // Ensure clean shutdown
  if (running_.load(std::memory_order_acquire)) {
    (void)stop();
  }
}

void AsyncLogBackend::blockSignals() noexcept {
#if defined(__unix__) || defined(__APPLE__)
  // Prevent I/O thread from handling process signals (keep them on shutdown thread)
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGHUP);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
#else
  // Non-POSIX platforms: no-op
#endif
}

bool AsyncLogBackend::start() noexcept {
  // Check if already running
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return false; // Already running
  }

  // Open log file with O_APPEND for atomic writes
  logFd_ = ::open(logPath_.c_str(),
                  O_CREAT | O_APPEND | O_WRONLY
#if defined(O_CLOEXEC)
                      | O_CLOEXEC
#endif
                  ,
                  0644);

  if (logFd_ < 0) {
    running_.store(false, std::memory_order_release);
    return false; // File open failed
  }

#if defined(__linux__)
  // Prefer the process-wide drain service: one I/O thread for every
  // backend. Producers wake it through the service's eventfd; this
  // backend owns no thread and no private eventfd in shared mode.
  if (LogDrainService::instance().registerBackend(this)) {
    service_ = &LogDrainService::instance();
    return true;
  }

  // Registry full or service unavailable: dedicated-thread fallback.
  wakeFd_ = ::eventfd(0, EFD_CLOEXEC);
  if (wakeFd_ < 0) {
    ::close(logFd_);
    logFd_ = -1;
    running_.store(false, std::memory_order_release);
    return false;
  }
#endif

  // Launch I/O thread (with signal masking like ThreadPool)
  try {
    ioThread_ = std::thread([this]() {
      blockSignals();
      ioThreadLoop();
    });
  } catch (...) {
    ::close(logFd_);
    logFd_ = -1;
    if (wakeFd_ >= 0) {
      ::close(wakeFd_);
      wakeFd_ = -1;
    }
    running_.store(false, std::memory_order_release);
    return false;
  }

  return true;
}

bool AsyncLogBackend::stop() noexcept {
  // Check if running
  if (!running_.load(std::memory_order_acquire)) {
    return true; // Already stopped
  }

#if defined(__linux__)
  if (service_ != nullptr) {
    // Hand-off: after unregisterBackend returns the drain thread can no
    // longer touch this ring, so leftovers drain inline here.
    running_.store(false, std::memory_order_release);
    service_->unregisterBackend(this);
    service_ = nullptr;
    while (drainBatch(SIZE_MAX) > 0) {
    }
    if (logFd_ >= 0) {
      ::fsync(logFd_);
      ::close(logFd_);
      logFd_ = -1;
    }
    return outstanding_.load(std::memory_order_relaxed) == 0;
  }
#endif

  // Signal I/O thread to stop (like ThreadPool: set flag under lock, notify outside)
  {
    std::lock_guard<std::mutex> lock(stopMutex_);
    running_.store(false, std::memory_order_release);
  }

  // Wake I/O thread immediately (OUTSIDE mutex like ThreadPool)
  stopCv_.notify_one();
#if defined(__linux__)
  if (wakeFd_ >= 0) {
    (void)::eventfd_write(wakeFd_, 1);
  }
#endif

  // Join I/O thread (it will drain queue then exit)
  if (ioThread_.joinable()) {
    ioThread_.join();
  }

#if defined(__linux__)
  if (wakeFd_ >= 0) {
    ::close(wakeFd_);
    wakeFd_ = -1;
  }
#endif

  // Flush and close file descriptor
  if (logFd_ >= 0) {
    ::fsync(logFd_); // Best effort flush
    ::close(logFd_);
    logFd_ = -1;
  }

  // Every accepted entry has had its write attempted
  const std::size_t remaining = outstanding_.load(std::memory_order_relaxed);
  return (remaining == 0);
}

bool AsyncLogBackend::flush(std::uint32_t timeoutMs) noexcept {
  // Check if running
  if (!running_.load(std::memory_order_acquire)) {
    return true; // Not running, nothing to flush
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

  // Wake I/O thread to ensure it processes remaining entries
  stopCv_.notify_one();
#if defined(__linux__)
  if (service_ != nullptr) {
    service_->notify();
  } else if (wakeFd_ >= 0) {
    (void)::eventfd_write(wakeFd_, 1);
  }
#endif

  // Wait until no accepted entry is outstanding. The drain notifies under
  // the mutex when it completes the last one; a producer that retires an
  // admission does not notify (it takes no lock), so the wait also
  // re-checks every millisecond.
  std::unique_lock<std::mutex> lock(stopMutex_);
  while (outstanding_.load(std::memory_order_relaxed) != 0) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false; // Timeout
    }
    stopCv_.wait_for(lock, std::chrono::milliseconds(1));
  }

  lock.unlock();

  // Queue drained - now force kernel flush
  if (logFd_ >= 0) {
    ::fsync(logFd_);
  }

  return true;
}

bool AsyncLogBackend::tryLog(std::string_view msg) noexcept {
  // Early exit if not running -- still an unaccepted entry, so count it.
  if (!running_.load(std::memory_order_acquire)) {
    droppedCount_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  if (!admit()) {
    return false;
  }
  return publishAdmitted(msg);
}

bool AsyncLogBackend::admit() noexcept {
  // Cheap full-check before paying the 520-byte entry copy. The depth is
  // approximate (an entry leaves it when the drain takes it off the ring),
  // so this fires at or within one entry of full -- where dropping is
  // already the policy.
  if (queueDepth_.load(std::memory_order_relaxed) >= capacity_) {
    droppedCount_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  // Counted as outstanding before the entry can reach the drain. The drain
  // releases an entry only after writing it, so the count never falls
  // below the accepted entries still unwritten, whichever producer's
  // increment a given release pairs with.
  outstanding_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool AsyncLogBackend::publishAdmitted(std::string_view msg) noexcept {
  // Create entry (copies message to fixed buffer)
  LogEntry entry(msg);

  // Try to push to queue (lock-free, never blocks)
  if (queue_.tryPush(std::move(entry))) {
    if (msg.size() >= LogEntry::MAX_MSG_LEN) {
      truncatedCount_.fetch_add(1, std::memory_order_relaxed);
    }

    // Batch notifications: only wake thread if queue was previously empty.
    // The depth is counted after the push: the drain polls the ring again
    // after each decrement, so an entry pushed while the depth was not
    // zero is found without a wake.
    const std::size_t prevDepth = queueDepth_.fetch_add(1, std::memory_order_relaxed);

    if (prevDepth == 0) {
#if defined(__linux__)
      // Queue was empty, the drain thread is likely parked on its eventfd.
      // The counter write is non-blocking and cannot be lost: a write that
      // lands before the worker's read unparks it immediately; one that
      // lands while the worker is draining leaves the counter non-zero, so
      // the next read returns without sleeping. No lock is shared with the
      // I/O thread or control-plane callers on this path.
      if (service_ != nullptr) {
        service_->notify();
      } else {
        (void)::eventfd_write(wakeFd_, 1);
      }
#else
      // Notify under the lock so the wakeup cannot be lost in the window
      // between the worker checking its predicate (queueDepth_) and parking
      // on the CV. queueDepth_ stays lock-free; only the empty->non-empty
      // transition takes the lock.
      std::lock_guard<std::mutex> lock(stopMutex_);
      stopCv_.notify_one();
#endif
    }

    return true;
  }

  // Queue full - drop the entry, count it, and retire its admission. No
  // notification: flush() re-checks on its own.
  droppedCount_.fetch_add(1, std::memory_order_relaxed);
  outstanding_.fetch_sub(1, std::memory_order_relaxed);
  return false;
}

void AsyncLogBackend::completeOne() noexcept {
  // The predicate changes outside the mutex; the notification is issued
  // under it, so a flush() that saw a non-zero count has parked (and
  // released the mutex) before this thread can notify.
  if (outstanding_.fetch_sub(1, std::memory_order_relaxed) == 1) {
    std::lock_guard<std::mutex> lock(stopMutex_);
    stopCv_.notify_all();
  }
}

std::size_t AsyncLogBackend::drainBatch(std::size_t maxEntries) noexcept {
  LogEntry entry;
  std::size_t drained = 0;

  while (drained < maxEntries && queue_.tryPop(entry)) {
    // The depth follows the ring; the outstanding count is released only
    // once the write has been attempted, which is what flush() waits for.
    queueDepth_.fetch_sub(1, std::memory_order_relaxed);
    writeEntry(entry);
    entriesWritten_.fetch_add(1, std::memory_order_relaxed);
    ++drained;
    completeOne();
  }
  return drained;
}

void AsyncLogBackend::ioThreadLoop() noexcept {
  LogEntry entry;

  while (true) {
    // Try to pop entry from queue (lock-free, fast path)
    if (queue_.tryPop(entry)) {
      // As in drainBatch(): the depth at the pop, the release after the write.
      queueDepth_.fetch_sub(1, std::memory_order_relaxed);
      writeEntry(entry);
      entriesWritten_.fetch_add(1, std::memory_order_relaxed);
      completeOne();

      continue; // Process next entry immediately
    }

    // Queue empty - park until work arrives or shutdown is signaled
#if defined(__linux__)
    if (!running_.load(std::memory_order_acquire) &&
        queueDepth_.load(std::memory_order_relaxed) == 0) {
      break; // Clean exit: no more work, stop requested
    }
    if (queueDepth_.load(std::memory_order_relaxed) == 0) {
      // Blocking read drains the wake counter; a producer's write that
      // raced ahead of this read leaves the counter non-zero and the read
      // returns immediately. stop() writes the counter too, so shutdown
      // always unparks.
      eventfd_t wakes = 0;
      (void)::eventfd_read(wakeFd_, &wakes);
    }
#else
    {
      std::unique_lock<std::mutex> lock(stopMutex_);

      // Wait until: stop requested OR queue has entries
      stopCv_.wait(lock, [this]() {
        return !running_.load(std::memory_order_acquire) ||
               queueDepth_.load(std::memory_order_relaxed) > 0;
      });

      // Exit condition: stop requested AND queue is empty
      if (!running_.load(std::memory_order_acquire) &&
          queueDepth_.load(std::memory_order_relaxed) == 0) {
        break; // Clean exit: no more work, stop requested
      }
    }
    // Lock released here - loop continues to drain queue
#endif
  }

  // Final flush before exit
  if (logFd_ >= 0) {
    ::fsync(logFd_);
  }
}

void AsyncLogBackend::writeEntry(const LogEntry& entry) noexcept {
  if (logFd_ < 0 || entry.length == 0) {
    return;
  }

  // Write message (already contains trailing newline from SystemLog formatting)
  const char* p = entry.message;
  std::size_t remaining = entry.length;

  while (remaining > 0) {
    const ssize_t N = ::write(logFd_, p, remaining);
    if (N < 0) {
      if (errno == EINTR) {
        continue; // Interrupted, retry
      }
      writeErrors_.fetch_add(1, std::memory_order_relaxed);
      return; // Write error; the entry is lost but the loss is counted
    }
    p += static_cast<std::size_t>(N);
    remaining -= static_cast<std::size_t>(N);
  }
}

} // namespace logs