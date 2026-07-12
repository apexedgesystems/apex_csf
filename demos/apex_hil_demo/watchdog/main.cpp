/**
 * @file main.cpp
 * @brief Entry point for the ApexWatchdog standalone supervisor binary.
 *
 * Parses CLI arguments, constructs a WatchdogSupervisor, and runs it.
 * This is a thin wrapper -- all logic lives in WatchdogSupervisor.
 */

#include "WatchdogSupervisor.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* ----------------------------- Constants ----------------------------- */

static executive::WatchdogSupervisor* gSupervisor = nullptr;

/* ----------------------------- Signal Handling ----------------------------- */

static void handleSignal(int /*sig*/) {
  if (gSupervisor != nullptr) {
    gSupervisor->requestShutdown();
  }
}

/* ----------------------------- Usage ----------------------------- */

static void printUsage(const char* argv0) {
  fprintf(stderr,
          "Usage: %s [options] -- <executable> [args...]\n"
          "\n"
          "Options:\n"
          "  --max-crashes N          Max consecutive crashes before full stop (default: 5)\n"
          "  --heartbeat-timeout S    Seconds without heartbeat before kill (default: 10)\n"
          "  --safe-config PATH       TPRM config for degraded-mode restarts\n"
          "  --safe-threshold N       Switch to safe config after N crashes (default: 2)\n"
          "  --state-dir PATH         Directory for state/log files (default: .apex_fs)\n"
          "  --help                   Show this help\n"
          "\n"
          "Example:\n"
          "  %s --max-crashes 5 --safe-config tprm/safe_master.tprm --safe-threshold 2 \\\n"
          "    -- ./ApexHilDemo --config tprm/master.tprm\n",
          argv0, argv0);
}

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  executive::WatchdogConfig config;

  // Parse watchdog arguments (before --)
  int childArgStart = -1;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--") == 0) {
      childArgStart = i + 1;
      break;
    }
    if (strcmp(argv[i], "--max-crashes") == 0 && i + 1 < argc) {
      config.maxCrashes = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--heartbeat-timeout") == 0 && i + 1 < argc) {
      config.heartbeatTimeoutSec = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--safe-config") == 0 && i + 1 < argc) {
      config.safeConfig = argv[++i];
    } else if (strcmp(argv[i], "--safe-threshold") == 0 && i + 1 < argc) {
      config.safeThreshold = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) {
      config.stateDir = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0) {
      printUsage(argv[0]);
      return 0;
    }
  }

  if (childArgStart < 0 || childArgStart >= argc) {
    fprintf(stderr, "Error: no child executable specified after --\n");
    printUsage(argv[0]);
    return 1;
  }

  // Signal handling
  struct sigaction sa{};
  sa.sa_handler = handleSignal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);

  // Run supervisor
  executive::WatchdogSupervisor supervisor(config);
  gSupervisor = &supervisor;

  const int RESULT = supervisor.run(&argv[childArgStart]);

  gSupervisor = nullptr;
  return RESULT;
}
