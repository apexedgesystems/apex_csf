/**
 * @file AsyncLogBackend.cpp
 * @brief Implementation of lock-free async logging backend.
 */

#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"

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

// POSIX signal masking
#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <csignal>
#endif

namespace logs {

/* ----------------------------- LogEntry Methods ----------------------------- */

LogEntry::LogEntry(std::string_view msg) noexcept {
  // Safe copy with truncation
  const std::size_t copyLen = std::min(msg.size(), MAX_MSG_LEN - 1);
  std::memcpy(message, msg.data(), copyLen);
  message[copyLen] = '\0';
  length = static_cast<std::uint16_t>(copyLen);
}

/* ----------------------------- AsyncLogBackend Methods ----------------------------- */

AsyncLogBackend::AsyncLogBackend(std::string logPath, std::size_t queueSize) noexcept
    : logPath_(std::move(logPath)), queue_(queueSize) {
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

  // Launch I/O thread (with signal masking like ThreadPool)
  try {
    ioThread_ = std::thread([this]() {
      blockSignals();
      ioThreadLoop();
    });
  } catch (...) {
    ::close(logFd_);
    logFd_ = -1;
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

  // Signal I/O thread to stop (like ThreadPool: set flag under lock, notify outside)
  {
    std::lock_guard<std::mutex> lock(stopMutex_);
    running_.store(false, std::memory_order_release);
  }

  // Wake I/O thread immediately (OUTSIDE mutex like ThreadPool)
  stopCv_.notify_one();

  // Join I/O thread (it will drain queue then exit)
  if (ioThread_.joinable()) {
    ioThread_.join();
  }

  // Flush and close file descriptor
  if (logFd_ >= 0) {
    ::fsync(logFd_); // Best effort flush
    ::close(logFd_);
    logFd_ = -1;
  }

  // Check if queue was fully drained
  const std::size_t remaining = queueDepth_.load(std::memory_order_relaxed);
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

  // Wait for queue to drain using condition variable (efficient wait)
  std::unique_lock<std::mutex> lock(stopMutex_);
  bool drained = stopCv_.wait_until(
      lock, deadline, [this]() { return queueDepth_.load(std::memory_order_relaxed) == 0; });

  if (!drained) {
    return false; // Timeout
  }

  lock.unlock();

  // Queue drained - now force kernel flush
  if (logFd_ >= 0) {
    ::fsync(logFd_);
  }

  return true;
}

bool AsyncLogBackend::tryLog(std::string_view msg) noexcept {
  // Early exit if not running
  if (!running_.load(std::memory_order_acquire)) {
    return false;
  }

  // Create entry (copies message to fixed buffer)
  LogEntry entry(msg);

  // Try to push to queue (lock-free, never blocks)
  if (queue_.tryPush(std::move(entry))) {
    // Batch notifications: only wake thread if queue was previously empty
    const std::size_t prevDepth = queueDepth_.fetch_add(1, std::memory_order_relaxed);

    if (prevDepth == 0) {
      // Queue was empty, I/O thread likely sleeping - wake it
      stopCv_.notify_one();
    }

    return true;
  }

  // Queue full - drop entry and increment counter
  droppedCount_.fetch_add(1, std::memory_order_relaxed);
  return false;
}

void AsyncLogBackend::ioThreadLoop() noexcept {
  LogEntry entry;

  while (true) {
    // Try to pop entry from queue (lock-free, fast path)
    if (queue_.tryPop(entry)) {
      const std::size_t prevDepth = queueDepth_.fetch_sub(1, std::memory_order_relaxed);
      writeEntry(entry);
      entriesWritten_.fetch_add(1, std::memory_order_relaxed);

      // If queue just became empty, notify any waiters (flush())
      if (prevDepth == 1) {
        stopCv_.notify_all();
      }

      continue; // Process next entry immediately
    }

    // Queue empty - wait for work or shutdown signal
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
      return; // Write error, give up
    }
    p += static_cast<std::size_t>(N);
    remaining -= static_cast<std::size_t>(N);
  }
}

} // namespace logs