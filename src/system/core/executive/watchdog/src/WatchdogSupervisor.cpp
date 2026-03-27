/**
 * @file WatchdogSupervisor.cpp
 * @brief Implementation of POSIX process supervisor for Apex executives.
 */

#include "src/system/core/executive/watchdog/inc/WatchdogSupervisor.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace executive {

/* ----------------------------- Constants ----------------------------- */

static constexpr const char* DEFAULT_STATE_DIR = ".apex_fs";
static constexpr const char* STATE_FILENAME = "watchdog.state";
static constexpr const char* LOG_FILENAME = "watchdog.log";
static constexpr std::uint32_t STATE_MAGIC = 0x57444F47; // "WDOG"
static constexpr int RESTART_DELAY_SEC = 2;

/* ----------------------------- File Helpers ----------------------------- */

static void buildPath(char* dest, int maxLen, const char* dir, const char* file) {
  snprintf(dest, static_cast<std::size_t>(maxLen), "%s/%s", dir, file);
}

/* ----------------------------- WatchdogSupervisor Methods ----------------------------- */

WatchdogSupervisor::WatchdogSupervisor(const WatchdogConfig& config) noexcept : config_(config) {
  const char* DIR = (config_.stateDir != nullptr && config_.stateDir[0] != '\0')
                        ? config_.stateDir
                        : DEFAULT_STATE_DIR;

  buildPath(stateFilePath_, MAX_PATH_LEN, DIR, STATE_FILENAME);
  buildPath(logFilePath_, MAX_PATH_LEN, DIR, LOG_FILENAME);

  mkdir(DIR, 0755);
  openLog();
  loadState();
}

WatchdogSupervisor::~WatchdogSupervisor() noexcept {
  if (logFile_ != nullptr) {
    fclose(logFile_);
    logFile_ = nullptr;
  }
}

void WatchdogSupervisor::openLog() noexcept { logFile_ = fopen(logFilePath_, "a"); }

void WatchdogSupervisor::logMsg(const char* level, const char* fmt, ...) noexcept {
  time_t now = time(nullptr);
  struct tm localTm{};
  localtime_r(&now, &localTm);

  char timeBuf[32];
  strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &localTm);

  char msgBuf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msgBuf, sizeof(msgBuf), fmt, ap);
  va_end(ap);

  fprintf(stderr, "[%s] %s: %s\n", timeBuf, level, msgBuf);
  if (logFile_ != nullptr) {
    fprintf(logFile_, "[%s] %s: %s\n", timeBuf, level, msgBuf);
    fflush(logFile_);
  }
}

bool WatchdogSupervisor::loadState() noexcept {
  FILE* f = fopen(stateFilePath_, "rb");
  if (f == nullptr) {
    return false;
  }
  const bool OK = fread(&state_, sizeof(state_), 1, f) == 1 && state_.magic == STATE_MAGIC;
  fclose(f);
  if (!OK) {
    state_ = WatchdogState{};
  }
  return OK;
}

bool WatchdogSupervisor::saveState() noexcept {
  FILE* f = fopen(stateFilePath_, "wb");
  if (f == nullptr) {
    return false;
  }
  const bool OK = fwrite(&state_, sizeof(state_), 1, f) == 1;
  fclose(f);
  return OK;
}

pid_t WatchdogSupervisor::launchChild(char* const argv[], int heartbeatWriteFd) noexcept {
  pid_t pid = fork();
  if (pid < 0) {
    logMsg("ERROR", "fork() failed: %s", strerror(errno));
    return -1;
  }

  if (pid == 0) {
    // Child: set APEX_WATCHDOG_FD so the executive writes heartbeat bytes.
    char fdStr[16];
    snprintf(fdStr, sizeof(fdStr), "%d", heartbeatWriteFd);
    setenv("APEX_WATCHDOG_FD", fdStr, 1);

    execv(argv[0], argv);

    // execv failed.
    fprintf(stderr, "WATCHDOG: execv(%s) failed: %s\n", argv[0], strerror(errno));
    _exit(127);
  }

  return pid;
}

char** WatchdogSupervisor::buildSafeArgv(char* const originalArgv[],
                                         const char* safeConfig) noexcept {
  int n = 0;
  bool replaced = false;

  for (int i = 0; originalArgv[i] != nullptr && n < MAX_CHILD_ARGS - 3; ++i) {
    if (strcmp(originalArgv[i], "--config") == 0 && originalArgv[i + 1] != nullptr) {
      safeArgvStorage_[n++] = originalArgv[i];
      safeArgvStorage_[n++] = const_cast<char*>(safeConfig);
      ++i;
      replaced = true;
    } else {
      safeArgvStorage_[n++] = originalArgv[i];
    }
  }

  if (!replaced) {
    safeArgvStorage_[n++] = const_cast<char*>("--config");
    safeArgvStorage_[n++] = const_cast<char*>(safeConfig);
  }

  safeArgvStorage_[n] = nullptr;
  return safeArgvStorage_;
}

void WatchdogSupervisor::requestShutdown() noexcept { shutdownRequested_ = 1; }

const WatchdogState& WatchdogSupervisor::state() const noexcept { return state_; }

bool WatchdogSupervisor::isShutdownRequested() const noexcept { return shutdownRequested_ != 0; }

int WatchdogSupervisor::run(char* const childArgv[]) noexcept {
  logMsg("INFO", "Watchdog starting: max_crashes=%d heartbeat_timeout=%ds", config_.maxCrashes,
         config_.heartbeatTimeoutSec);
  logMsg("INFO", "Child executable: %s", childArgv[0]);
  if (config_.safeConfig != nullptr) {
    logMsg("INFO", "Safe config: %s (threshold=%d)", config_.safeConfig, config_.safeThreshold);
  }

  if (state_.totalCrashes > 0) {
    logMsg("INFO", "Loaded state: consecutive=%u total=%u restarts=%u", state_.consecutiveCrashes,
           state_.totalCrashes, state_.totalRestarts);
  }

  while (!shutdownRequested_) {
    // Full stop check
    if (static_cast<int>(state_.consecutiveCrashes) >= config_.maxCrashes) {
      logMsg("ERROR", "FULL STOP: %u consecutive crashes (max=%d), stopping restarts",
             state_.consecutiveCrashes, config_.maxCrashes);
      logMsg("ERROR", "Operator intervention required. Reset crash counter by deleting %s",
             stateFilePath_);

      while (!shutdownRequested_) {
        sleep(1);
      }
      break;
    }

    // Create heartbeat pipe
    int pipeFd[2];
    if (pipe(pipeFd) != 0) {
      logMsg("ERROR", "pipe() failed: %s", strerror(errno));
      return 1;
    }

    fcntl(pipeFd[0], F_SETFL, O_NONBLOCK);

    // Record launch
    state_.lastStartEpoch = time(nullptr);
    state_.totalRestarts++;
    saveState();

    // Degraded mode check
    const bool DEGRADED = config_.safeConfig != nullptr &&
                          static_cast<int>(state_.consecutiveCrashes) >= config_.safeThreshold;
    char* const* launchArgv = childArgv;
    if (DEGRADED) {
      launchArgv = buildSafeArgv(childArgv, config_.safeConfig);
      logMsg("WARNING", "DEGRADED MODE: using safe config '%s' (crashes=%u >= threshold=%d)",
             config_.safeConfig, state_.consecutiveCrashes, config_.safeThreshold);
    }

    logMsg("INFO", "Launching child (attempt %u, consecutive_crashes=%u%s)", state_.totalRestarts,
           state_.consecutiveCrashes, DEGRADED ? ", DEGRADED" : "");

    pid_t childPid = launchChild(launchArgv, pipeFd[1]);
    if (childPid < 0) {
      close(pipeFd[0]);
      close(pipeFd[1]);
      sleep(1);
      continue;
    }

    // Parent closes write end
    close(pipeFd[1]);

    // Monitor heartbeat
    bool childExited = false;
    bool heartbeatTimeout = false;

    while (!shutdownRequested_ && !childExited && !heartbeatTimeout) {
      struct pollfd pfd{};
      pfd.fd = pipeFd[0];
      pfd.events = POLLIN | POLLHUP;

      int ret = poll(&pfd, 1, config_.heartbeatTimeoutSec * 1000);

      if (ret < 0) {
        if (errno == EINTR) {
          continue;
        }
        logMsg("ERROR", "poll() failed: %s", strerror(errno));
        break;
      }

      if (ret == 0) {
        heartbeatTimeout = true;
        logMsg("WARNING", "Heartbeat timeout (%ds), child PID %d appears hung",
               config_.heartbeatTimeoutSec, childPid);
        break;
      }

      if ((pfd.revents & POLLHUP) != 0) {
        childExited = true;
        break;
      }

      if ((pfd.revents & POLLIN) != 0) {
        char buf[64];
        while (read(pipeFd[0], buf, sizeof(buf)) > 0) {
          // Drain heartbeat bytes
        }
      }
    }

    close(pipeFd[0]);

    // Shutdown requested: forward signal and exit
    if (shutdownRequested_) {
      logMsg("INFO", "Shutdown requested, forwarding SIGTERM to child PID %d", childPid);
      kill(childPid, SIGTERM);
      int status = 0;
      waitpid(childPid, &status, 0);
      logMsg("INFO", "Child exited with status %d", WEXITSTATUS(status));
      break;
    }

    // Heartbeat timeout: kill hung child
    if (heartbeatTimeout) {
      logMsg("WARNING", "Sending SIGKILL to hung child PID %d", childPid);
      kill(childPid, SIGKILL);
      int status = 0;
      waitpid(childPid, &status, 0);
      state_.consecutiveCrashes++;
      state_.totalCrashes++;
      state_.lastCrashEpoch = time(nullptr);
      saveState();
      logMsg("ERROR", "Child killed (hung), consecutive=%u total=%u", state_.consecutiveCrashes,
             state_.totalCrashes);
      continue;
    }

    // Child exited on its own
    int status = 0;
    waitpid(childPid, &status, 0);

    if (WIFEXITED(status)) {
      const int EXIT_CODE = WEXITSTATUS(status);
      if (EXIT_CODE == 0) {
        logMsg("INFO", "Child exited cleanly (exit code 0)");
        state_.consecutiveCrashes = 0;
        saveState();
        break;
      }
      logMsg("WARNING", "Child exited with code %d", EXIT_CODE);
      state_.consecutiveCrashes++;
      state_.totalCrashes++;
      state_.lastCrashEpoch = time(nullptr);
      saveState();
    } else if (WIFSIGNALED(status)) {
      const int SIG = WTERMSIG(status);
      logMsg("ERROR", "Child killed by signal %d (%s)%s", SIG, strsignal(SIG),
             WCOREDUMP(status) ? " (core dumped)" : "");
      state_.consecutiveCrashes++;
      state_.totalCrashes++;
      state_.lastCrashEpoch = time(nullptr);
      saveState();
    }

    logMsg("INFO", "Will restart in %d seconds (consecutive=%u/%d)", RESTART_DELAY_SEC,
           state_.consecutiveCrashes, config_.maxCrashes);
    sleep(RESTART_DELAY_SEC);
  }

  logMsg("INFO", "Watchdog exiting (crashes: consecutive=%u, total=%u, restarts=%u)",
         state_.consecutiveCrashes, state_.totalCrashes, state_.totalRestarts);

  return 0;
}

} // namespace executive
