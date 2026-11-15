#ifndef APEX_SYSTEM_LOGS_SYSTEM_LOG_HPP
#define APEX_SYSTEM_LOGS_SYSTEM_LOG_HPP
/**
 * @file SystemLog.hpp
 * @brief Logging facility with fast-path controls suitable for real-time systems.
 *
 * Design:
 *  - Single-pass formatting; file and console share the same buffer.
 *  - Severity threshold to bypass formatting when below the configured level.
 *  - Integer verbosity levels for granular debug control.
 *  - Two modes: SYNC (blocking writes) and ASYNC (RT-safe lock-free queue).
 *
 * RT Lifecycle Constraints:
 *  - ASYNC mode: info()/debug()/warning()/error()/fatal() are RT-safe.
 *  - SYNC mode: Logging calls block on I/O (NOT RT-safe).
 *  - Below-threshold messages skip formatting (~40ns, RT-safe in both modes).
 *  - setLevel()/setVerbosity() are RT-safe (atomic stores).
 *  - flush() is NOT RT-safe (blocks until queue drains or fsync completes).
 *  - Construct and destroy outside RT phase.
 */

#include "src/system/core/infrastructure/logs/inc/LogBase.hpp"
#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp" // COMPAT_LIKELY
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

namespace logs {

/**
 * @class SystemLog
 * @brief Provides categorized system-level logging with real-time constraints in mind.
 *
 * Characteristics:
 *  - Public APIs are noexcept and return a @ref Status.
 *  - Supports threshold filtering and console echo.
 *  - Integer verbosity levels for debug messages (0-255).
 *  - Async mode uses lock-free queue for real-time safety.
 */
class SystemLog : public LogBase {
public:
  /// Severity levels in ascending order of importance.
  enum class Level : std::uint8_t { DEBUG = 0, INFO, WARNING, ERROR, FATAL };

  /// Logging mode: synchronous (blocking) vs asynchronous (RT-safe).
  enum class Mode : std::uint8_t {
    SYNC = 0, ///< Traditional blocking writes (default, backward compatible)
    ASYNC     ///< Lock-free async via I/O thread (RT-safe, never blocks)
  };

  /**
   * @brief Construct a SystemLog bound to a file (sync mode).
   * @param logPath Path to the log file.
   *
   * Backward compatible: defaults to SYNC mode.
   * The outcome of the open attempt is available via @ref lastOpenStatus().
   */
  explicit SystemLog(const std::string& logPath) noexcept;

  /**
   * @brief Construct a SystemLog with async mode support.
   * @param logPath Path to the log file.
   * @param mode SYNC (blocking) or ASYNC (lock-free, RT-safe).
   * @param asyncQueueSize Ring buffer capacity for async mode (default 4096).
   *
   * ASYNC mode provides real-time safety:
   *  - tryLog() completes in <1us (never blocks on disk)
   *  - The shared drain thread handles disk writes (a dedicated thread
   *    when the drain's registry is full)
   *  - Queue overflow drops entries (increments counter)
   *
   * Falls back to SYNC mode if async backend fails to start.
   */
  explicit SystemLog(const std::string& logPath, Mode mode,
                     std::size_t asyncQueueSize = 4096) noexcept;

  /**
   * @brief Destructor ensures clean shutdown.
   *
   * For ASYNC mode: stops I/O thread and flushes remaining entries (may block).
   */
  ~SystemLog() override;

  /**
   * @brief Force buffered data to disk.
   * @return Status::OK on success; Status::ERROR_SYNC on failure.
   *
   * For ASYNC mode: waits until every entry accepted before the call has
   * had its write attempted, then syncs; ERROR_SYNC when entries are still
   * outstanding after timeoutMs. OK does not report a write the file
   * refused (see the backend's writeErrorCount()) or the sync result.
   * For SYNC mode: calls fsync() on file descriptor (timeoutMs is unused).
   * Call before program exit to hand all accepted lines to the file.
   *
   * @param timeoutMs Longest wait for outstanding async entries; it does
   *        not bound the sync that follows.
   *
   * NOTE: This shadows LogBase::flush() (not virtual override).
   */
  Status flush(std::uint32_t timeoutMs = 5000) noexcept;

  /**
   * @brief Set the minimum severity to log.
   * Messages below this level are discarded without formatting.
   * @note RT-safe: Atomic store.
   */
  void setLevel(Level lvl) noexcept { minLevel_.store(lvl, std::memory_order_relaxed); }

  /**
   * @brief Retrieve the current minimum severity level.
   * @note RT-safe: Atomic load.
   */
  Level level() const noexcept { return minLevel_.load(std::memory_order_relaxed); }

  /**
   * @brief Set debug verbosity level.
   * Debug messages at or below this level will be logged, when the
   * severity threshold admits DEBUG. At the default of 0 only debug
   * messages of level 0 are logged.
   * @param level Maximum debug level to log (0-255).
   * @note RT-safe: Atomic store.
   */
  void setVerbosity(std::uint8_t level) noexcept {
    verbosityLevel_.store(level, std::memory_order_relaxed);
  }

  /**
   * @brief Retrieve current debug verbosity level.
   * @note RT-safe: Atomic load.
   */
  std::uint8_t verbosity() const noexcept {
    return verbosityLevel_.load(std::memory_order_relaxed);
  }

  /* ----------------------------- Logging API ----------------------------- */

  /**
   * @brief Log a warning message with an error code.
   * @note RT-safe in ASYNC mode; blocks on I/O in SYNC mode.
   */
  Status warning(std::string_view src, std::uint8_t ec, std::string_view msg,
                 bool echoConsole = false) noexcept;

  /**
   * @brief Log an error message with an error code.
   * @note RT-safe in ASYNC mode; blocks on I/O in SYNC mode.
   */
  Status error(std::string_view src, std::uint8_t ec, std::string_view msg,
               bool echoConsole = false) noexcept;

  /**
   * @brief Log a fatal message with an error code.
   * @note RT-safe in ASYNC mode (unless fatal-flush is enabled); blocks on
   *       I/O in SYNC mode.
   * @note With setFatalFlush(true), the call drains the async queue before
   *       returning so a FATAL logged immediately before a crash cannot
   *       vanish with the queue -- at the cost of blocking the caller.
   */
  Status fatal(std::string_view src, std::uint8_t ec, std::string_view msg,
               bool echoConsole = false) noexcept;

  /**
   * @brief Configure whether fatal() drains the async queue before returning.
   *
   * Default off: fatal() is RT-safe like the other levels and a
   * crash-adjacent FATAL may be lost with the queue. Enabled: fatal()
   * calls flush() (NOT RT-safe -- blocks until drained and synced), the
   * right trade for control-plane supervisors that log FATAL and abort.
   * @note RT-safe: atomic store.
   */
  void setFatalFlush(bool enabled) noexcept {
    fatalFlush_.store(enabled, std::memory_order_relaxed);
  }

  /**
   * @brief Log an informational message.
   * @note RT-safe in ASYNC mode; blocks on I/O in SYNC mode.
   * @note echoConsole=true writes to stdout via stdio on the calling
   *       thread -- NOT RT-safe regardless of mode; reserve for boot and
   *       diagnostics contexts.
   */
  Status info(std::string_view src, std::string_view msg, bool echoConsole = false) noexcept;

  /**
   * @brief Log a debug message at the specified verbosity level.
   * @param src Source identifier.
   * @param msg Message text.
   * @param level Verbosity level for this message (default 0).
   * @note RT-safe in ASYNC mode; blocks on I/O in SYNC mode.
   * @note Skips formatting if level > verbosity() (~40ns, always RT-safe).
   */
  Status debug(std::string_view src, std::string_view msg, std::uint8_t level = 0) noexcept;

  // Async mode accessors ------------------------------------------------------

  /**
   * @brief Get current logging mode.
   */
  Mode mode() const noexcept { return mode_; }

  /**
   * @brief Check if async mode is active.
   */
  bool isAsync() const noexcept { return mode_ == Mode::ASYNC && asyncBackend_ != nullptr; }

  /**
   * @brief Get async backend (nullptr if sync mode).
   */
  std::shared_ptr<AsyncLogBackend> asyncBackend() noexcept { return asyncBackend_; }

private:
  /**
   * @brief Internal helper performing single-pass formatting and emission.
   * @param lvl Severity level.
   * @param levelName String representation of the severity.
   * @param src Source identifier.
   * @param msg Message text.
   * @param ec Optional error code (nullptr if not applicable).
   * @param toConsole If true, echo to console as well as the log file.
   *
   * Skips immediately if the severity is below the configured threshold.
   */
  Status logLine(Level lvl, const char* levelName, std::string_view src, std::string_view msg,
                 const std::uint8_t* ec, bool toConsole) noexcept;

  Mode mode_{Mode::SYNC};                           ///< Logging mode (SYNC or ASYNC)
  std::shared_ptr<AsyncLogBackend> asyncBackend_{}; ///< Async backend (nullptr if SYNC)
  std::atomic<Level> minLevel_{Level::INFO};        ///< Default: suppress DEBUG output.
  std::atomic<std::uint8_t> verbosityLevel_{0};     ///< Default: no debug messages.
  std::atomic<bool> fatalFlush_{false};             ///< fatal() drains the queue when set.
};

} // namespace logs

#endif // APEX_SYSTEM_LOGS_SYSTEM_LOG_HPP