/**
 * @file SystemLog.cpp
 * @brief Implementation of SystemLog with integer verbosity levels.
 */

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <array>
#include <cstdio>
#include <ctime>
#include <string_view>

#include <fmt/compile.h>
#include <fmt/format.h>

namespace logs {

/* ----------------------------- SystemLog Methods ----------------------------- */

SystemLog::SystemLog(const std::string& logPath) noexcept : LogBase(logPath), mode_(Mode::SYNC) {}

SystemLog::SystemLog(const std::string& logPath, Mode mode, std::size_t asyncQueueSize) noexcept
    : LogBase(logPath), mode_(mode) {

  if (mode_ == Mode::ASYNC) {
    // Create and start async backend
    asyncBackend_ = std::make_shared<AsyncLogBackend>(logPath, asyncQueueSize);
    if (!asyncBackend_->start()) {
      // Failed to start async backend - fall back to sync mode
      asyncBackend_.reset();
      mode_ = Mode::SYNC;
    }
  }
}

SystemLog::~SystemLog() {
  // Stop async backend if active (flushes remaining entries)
  if (asyncBackend_) {
    asyncBackend_->stop();
  }
}

Status SystemLog::flush() noexcept {
  if (mode_ == Mode::ASYNC && asyncBackend_) {
    // For async mode: wait for queue to drain and sync to disk
    // This does NOT stop the I/O thread - logging continues afterward
    const bool SUCCESS = asyncBackend_->flush(5000); // 5 second timeout
    return SUCCESS ? Status::OK : Status::ERROR_SYNC;
  } else {
    // Sync mode: flush base class fd
    return LogBase::flush();
  }
}

// Single-pass formatting, lock-free append (no-throw).
Status SystemLog::logLine(Level lvl, const char* levelName, std::string_view src,
                          std::string_view msg, const std::uint8_t* ec, bool toConsole) noexcept {
  // Skip early when below threshold; avoid any formatting work.
  if (COMPAT_LIKELY(lvl < minLevel_.load(std::memory_order_relaxed))) {
    return Status::OK;
  }

  // Precheck: writers are lock-free; this may race with rotate but is safe.
  if (openStatus_ != Status::OK) {
    return openStatus_;
  }

  // Timestamp cache (per thread, 1-second resolution).
  // Avoids repeated time formatting in hot path. Updates only when the second
  // changes, which is sufficiently precise for system logs.
  thread_local std::array<char, 32> tsBuf{}; // "%Y-%m-%d %H:%M:%S" (+ headroom)
  thread_local std::time_t lastSec = 0;      // cached second (mutable by design)
  const std::time_t NOW_SEC = std::time(nullptr);
  if (NOW_SEC != lastSec) {
    lastSec = NOW_SEC;
    std::tm tmLocal{};
#if defined(_WIN32)
    localtime_s(&tmLocal, &lastSec);
#else
    localtime_r(&lastSec, &tmLocal);
#endif
    (void)std::strftime(tsBuf.data(), tsBuf.size(), "%Y-%m-%d %H:%M:%S", &tmLocal);
  }
  const char* ts = tsBuf.data();

  // Thread-local formatting buffer.
  // Reuses capacity across calls to minimize allocations and copies.
  // SBO: 256 bytes inline, grows once then reuses (no reserve() needed).
  thread_local fmt::basic_memory_buffer<char, 256> tlsBuf;
  tlsBuf.clear();
  auto& buf = tlsBuf;

  // Single-pass format of the full line.
  if (ec != nullptr) {
    // "[YYYY-MM-DD HH:MM:SS] WARNING: SRC[EC] - MSG"
    fmt::format_to(std::back_inserter(buf), FMT_COMPILE("[{}] {}: {}[{}] - {}"), ts, levelName, src,
                   *ec, msg);
  } else {
    // "[YYYY-MM-DD HH:MM:SS] INFO: SRC - MSG"
    fmt::format_to(std::back_inserter(buf), FMT_COMPILE("[{}] {}: {} - {}"), ts, levelName, src,
                   msg);
  }
  buf.push_back('\n');

  // Route to appropriate backend.
  Status st = Status::OK;

  if (mode_ == Mode::ASYNC && asyncBackend_) {
    // Async path - RT-safe, never blocks
    // Pass buffer directly as string_view (zero-copy, no allocation)
    if (!asyncBackend_->tryLog(std::string_view(buf.data(), buf.size()))) {
      // Queue full - entry dropped (counter incremented internally)
      // Continue execution - dropping log is better than blocking RT thread
    }
  } else {
    // Sync path - traditional blocking write
    // One syscall via O_APPEND guarantees atomic end-of-file writes per call.
    st = appendBytes(buf.data(), buf.size());
    if (st != Status::OK) {
      return st;
    }
  }

  // Optional console echo (reuses the same bytes; no reformatting).
  if (toConsole) {
    try {
      fmt::print(stdout, "{}", fmt::string_view(buf.data(), buf.size()));
    } catch (...) {
      // Console failures are non-fatal by contract.
    }
  }

  return st;
}

/* ----------------------------- API ----------------------------- */

Status SystemLog::warning(std::string_view src, std::uint8_t ec, std::string_view msg,
                          bool echoConsole) noexcept {
  return logLine(Level::WARNING, "WARNING", src, msg, &ec, echoConsole);
}

Status SystemLog::error(std::string_view src, std::uint8_t ec, std::string_view msg,
                        bool echoConsole) noexcept {
  return logLine(Level::ERROR, "ERROR", src, msg, &ec, echoConsole);
}

Status SystemLog::fatal(std::string_view src, std::uint8_t ec, std::string_view msg,
                        bool echoConsole) noexcept {
  return logLine(Level::FATAL, "FATAL", src, msg, &ec, echoConsole);
}

Status SystemLog::info(std::string_view src, std::string_view msg, bool echoConsole) noexcept {
  return logLine(Level::INFO, "INFO", src, msg, nullptr, echoConsole);
}

Status SystemLog::debug(std::string_view src, std::string_view msg, std::uint8_t level) noexcept {
  // Early return if this debug level is not enabled (zero overhead)
  if (level > verbosityLevel_.load(std::memory_order_relaxed)) {
    return Status::OK;
  }
  return logLine(Level::DEBUG, "DEBUG", src, msg, nullptr, false);
}

} // namespace logs