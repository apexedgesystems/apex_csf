#ifndef APEX_EXECUTIVE_WATCHDOG_WATCHDOG_SUPERVISOR_HPP
#define APEX_EXECUTIVE_WATCHDOG_WATCHDOG_SUPERVISOR_HPP
/**
 * @file WatchdogSupervisor.hpp
 * @brief POSIX process supervisor for Apex executive applications.
 *
 * Monitors a child executive via heartbeat pipe. On crash or hang:
 *   1. Increments persistent crash counter
 *   2. Restarts executive with same arguments
 *   3. After safe threshold, switches to degraded-mode TPRM config
 *   4. After max crashes, enters full stop (operator intervention required)
 *
 * Design principles:
 *   - No dynamic allocation after construction
 *   - No shared libraries beyond libc (process isolation from executive)
 *   - Crash state persisted to filesystem
 *   - Signal-safe shutdown via requestShutdown()
 *
 * RT Constraints:
 *   - run() blocks indefinitely (fork/exec/poll loop)
 *   - requestShutdown() is async-signal-safe
 *   - Query methods are RT-safe
 *
 * @note POSIX-only. Not available on bare-metal targets.
 */

#include <csignal>
#include <cstdint>
#include <cstdio>

namespace executive {

/* ----------------------------- WatchdogConfig ----------------------------- */

/**
 * @struct WatchdogConfig
 * @brief Configuration for the watchdog supervisor.
 *
 * All fields have safe defaults. Only childArgv must be set before run().
 */
struct WatchdogConfig {
  int maxCrashes{5};           ///< Full stop after this many consecutive crashes.
  int safeThreshold{2};        ///< Switch to safe config after this many crashes.
  int heartbeatTimeoutSec{10}; ///< Kill child after this many seconds without heartbeat.
  const char* safeConfig{};    ///< TPRM config path for degraded-mode restarts (optional).
  const char* stateDir{};      ///< Directory for state/log files (default: ".apex_fs").
};

/* ----------------------------- WatchdogState ----------------------------- */

/**
 * @struct WatchdogState
 * @brief Persistent crash tracking state.
 *
 * Written to disk after each crash/restart so the watchdog can resume
 * its crash counter across process restarts.
 */
struct WatchdogState {
  std::uint32_t magic{0x57444F47};     ///< "WDOG" magic for file validation.
  std::uint32_t consecutiveCrashes{0}; ///< Crashes since last clean exit.
  std::uint32_t totalCrashes{0};       ///< All-time crash count.
  std::uint32_t totalRestarts{0};      ///< All-time restart count.
  std::int64_t lastCrashEpoch{0};      ///< Unix epoch of most recent crash.
  std::int64_t lastStartEpoch{0};      ///< Unix epoch of most recent launch.
};

/* ----------------------------- WatchdogSupervisor ----------------------------- */

/**
 * @class WatchdogSupervisor
 * @brief Process supervisor that monitors and restarts an Apex executive.
 *
 * Usage:
 *   executive::WatchdogConfig cfg;
 *   cfg.maxCrashes = 5;
 *   cfg.heartbeatTimeoutSec = 10;
 *
 *   executive::WatchdogSupervisor wd(cfg);
 *   char* childArgv[] = { "./ApexHilDemo", "--config", "master.tprm", nullptr };
 *   return wd.run(childArgv);
 *
 * The child process receives APEX_WATCHDOG_FD as an environment variable
 * containing the write end of the heartbeat pipe. The executive writes a
 * byte each watchdog interval; if no byte arrives within heartbeatTimeoutSec,
 * the child is killed and restarted.
 *
 * @note NOT RT-safe (fork/exec/poll).
 */
class WatchdogSupervisor {
public:
  /**
   * @brief Construct supervisor with given configuration.
   * @param config Watchdog configuration.
   * @note NOT RT-safe (opens log file).
   */
  explicit WatchdogSupervisor(const WatchdogConfig& config) noexcept;

  /** @brief Destructor. Closes log file if open. */
  ~WatchdogSupervisor() noexcept;

  /* ----------------------------- Execution ----------------------------- */

  /**
   * @brief Main supervisor loop.
   *
   * Forks and execs the child, monitors heartbeat, restarts on failure.
   * Blocks until clean exit, full stop, or shutdown requested.
   *
   * @param childArgv Null-terminated argument array for the child executable.
   * @return 0 on clean exit, 1 on error.
   * @note NOT RT-safe (fork/exec/poll/waitpid).
   */
  [[nodiscard]] int run(char* const childArgv[]) noexcept;

  /**
   * @brief Request graceful shutdown.
   *
   * Forwards SIGTERM to the child and exits the supervisor loop.
   * Safe to call from a signal handler.
   *
   * @note Async-signal-safe.
   */
  void requestShutdown() noexcept;

  /* ----------------------------- Query ----------------------------- */

  /**
   * @brief Get current watchdog state (crash counters, timestamps).
   * @return Copy of current state.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const WatchdogState& state() const noexcept;

  /**
   * @brief Check if shutdown has been requested.
   * @return true if requestShutdown() has been called.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isShutdownRequested() const noexcept;

  /* ----------------------------- Non-copyable ----------------------------- */

  WatchdogSupervisor(const WatchdogSupervisor&) = delete;
  WatchdogSupervisor& operator=(const WatchdogSupervisor&) = delete;
  WatchdogSupervisor(WatchdogSupervisor&&) = delete;
  WatchdogSupervisor& operator=(WatchdogSupervisor&&) = delete;

private:
  static constexpr int MAX_CHILD_ARGS = 128;
  static constexpr int MAX_PATH_LEN = 128;

  WatchdogConfig config_;
  WatchdogState state_;
  FILE* logFile_{};
  volatile sig_atomic_t shutdownRequested_{0};

  char stateFilePath_[MAX_PATH_LEN]{};
  char logFilePath_[MAX_PATH_LEN]{};
  char* safeArgvStorage_[MAX_CHILD_ARGS]{};

  void openLog() noexcept;
  void logMsg(const char* level, const char* fmt, ...) noexcept
      __attribute__((format(printf, 3, 4)));

  bool loadState() noexcept;
  bool saveState() noexcept;

  pid_t launchChild(char* const argv[], int heartbeatWriteFd) noexcept;
  char** buildSafeArgv(char* const originalArgv[], const char* safeConfig) noexcept;
};

} // namespace executive

#endif // APEX_EXECUTIVE_WATCHDOG_WATCHDOG_SUPERVISOR_HPP
