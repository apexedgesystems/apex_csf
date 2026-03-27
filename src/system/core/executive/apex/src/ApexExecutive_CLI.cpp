/**
 * @file ApexExecutive_CLI.cpp
 * @brief Implementation of CLI argument processing for ApexExecutive.
 *
 * Contains processArgs() which handles all command-line argument parsing
 * and validation. Separated from ApexExecutive.cpp to keep CLI logic isolated.
 */

#include "src/system/core/executive/apex/inc/ApexExecutive.hpp"
#include "src/system/core/executive/apex/inc/ExecutiveStatus.hpp"
#include "src/system/core/executive/apex/inc/RTMode.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <fmt/core.h>

// Use Status enum without qualification
using enum executive::Status;

namespace executive {

// Process command-line arguments and populate configuration
std::uint8_t ApexExecutive::processArgs() noexcept {
  std::string errRet;

  // Convert args_ to string_view for parseArgs
  std::vector<std::string_view> argViews;
  argViews.reserve(args_.size());
  for (const auto& arg : args_) {
    argViews.push_back(arg);
  }

  // Parse executive arguments
  if (!apex::helpers::args::parseArgs(argViews, ARG_MAP, parsedArgs_, errRet)) {
    setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
    sysLog_->error(label(), status(), fmt::format("Failed to parse arguments: [{}]", errRet));
    return status();
  }

  // Log parsed arguments for reproducibility
  sysLog_->info(label(), "=== Parsed Command-Line Arguments ===");
  if (parsedArgs_.empty()) {
    sysLog_->info(label(), "No arguments provided");
  } else {
    for (const auto& [key, values] : parsedArgs_) {
      auto it = ARG_MAP.find(key);
      if (it == ARG_MAP.end()) {
        continue;
      }

      const std::string flagName(it->second.flag);
      if (values.empty()) {
        sysLog_->info(label(), fmt::format("  {}", flagName));
      } else {
        std::string argLine = "  " + flagName;
        for (const auto& val : values) {
          argLine += " ";
          argLine += val;
        }
        sysLog_->info(label(), argLine);
      }
    }
  }
  sysLog_->info(label(), "=====================================");

  // ===== Parse Verbosity =====
  if (parsedArgs_.count(VERBOSITY)) {
    const std::uint8_t verbosity =
        static_cast<std::uint8_t>(std::stoul(std::string(parsedArgs_[VERBOSITY][0])));
    sysLog_->setVerbosity(verbosity);
  }

  // ===== Parse Profiling Configuration =====
  if (parsedArgs_.count(ENABLE_PROFILING)) {
    if (parsedArgs_.count(PROFILE_INTERVAL)) {
      profilingState_.sampleEveryN = std::stoul(std::string(parsedArgs_[PROFILE_INTERVAL][0]));
    } else {
      profilingState_.sampleEveryN = 1;
    }
  } else {
    profilingState_.sampleEveryN = 0;
  }

  // ===== Parse Startup Configuration =====
  if (parsedArgs_.count(STARTUP_MODE)) {
    std::string_view mode = parsedArgs_[STARTUP_MODE][0];
    if (mode == "auto") {
      startupConfig_.mode = StartupConfig::AUTO;
    } else if (mode == "interactive") {
      startupConfig_.mode = StartupConfig::INTERACTIVE;
    } else if (mode == "scheduled") {
      startupConfig_.mode = StartupConfig::SCHEDULED;
    } else {
      setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
      sysLog_->error(label(), status(), fmt::format("Invalid startup mode: {}", mode));
      return status();
    }
  }

  if (parsedArgs_.count(STARTUP_DELAY)) {
    startupConfig_.delaySeconds = std::stoul(std::string(parsedArgs_[STARTUP_DELAY][0]));
  }

  if (parsedArgs_.count(START_AT)) {
    startupConfig_.startAtEpochNs = std::stoll(std::string(parsedArgs_[START_AT][0]));
  }

  // ===== Parse Shutdown Configuration =====
  if (parsedArgs_.count(SHUTDOWN_MODE)) {
    std::string_view mode = parsedArgs_[SHUTDOWN_MODE][0];
    if (mode == "signal") {
      shutdownConfig_.mode = ShutdownConfig::SIGNAL_ONLY;
    } else if (mode == "scheduled") {
      shutdownConfig_.mode = ShutdownConfig::SCHEDULED;
    } else if (mode == "relative") {
      shutdownConfig_.mode = ShutdownConfig::RELATIVE_TIME;
    } else if (mode == "cycle") {
      shutdownConfig_.mode = ShutdownConfig::CLOCK_CYCLE;
    } else if (mode == "combined") {
      shutdownConfig_.mode = ShutdownConfig::COMBINED;
    } else {
      setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
      sysLog_->error(label(), status(), fmt::format("Invalid shutdown mode: {}", mode));
      return status();
    }
  }

  if (parsedArgs_.count(SHUTDOWN_AT)) {
    shutdownConfig_.shutdownAtEpochNs = std::stoll(std::string(parsedArgs_[SHUTDOWN_AT][0]));
  }

  if (parsedArgs_.count(SHUTDOWN_AFTER)) {
    shutdownConfig_.relativeSeconds = std::stoul(std::string(parsedArgs_[SHUTDOWN_AFTER][0]));
  }

  if (parsedArgs_.count(SHUTDOWN_CYCLE)) {
    shutdownConfig_.targetClockCycle = std::stoull(std::string(parsedArgs_[SHUTDOWN_CYCLE][0]));
  }

  // ===== Parse Archive Configuration =====
  if (parsedArgs_.count(SKIP_CLEANUP)) {
    shutdownConfig_.skipCleanup = true;
  }

  if (parsedArgs_.count(ARCHIVE_PATH)) {
    archivePath_ = parsedArgs_[ARCHIVE_PATH][0];
  }

  // ===== Parse Tunable Parameters Configuration =====
  if (parsedArgs_.count(CONFIG_FILE)) {
    configPath_ = parsedArgs_[CONFIG_FILE][0];
  }

  // ===== Parse Watchdog Configuration =====
  if (parsedArgs_.count(WATCHDOG_INTERVAL)) {
    watchdogState_.intervalMs = std::stoul(std::string(parsedArgs_[WATCHDOG_INTERVAL][0]));
    if (watchdogState_.intervalMs < 100) {
      setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
      sysLog_->error(label(), status(), "Watchdog interval must be >= 100 ms");
      return status();
    }
  }

  // ===== Parse RT Mode Configuration =====
  if (parsedArgs_.count(RT_MODE)) {
    std::string_view modeStr = parsedArgs_[RT_MODE][0];
    if (!parseRTMode(modeStr, rtConfig_.mode)) {
      setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
      sysLog_->error(
          label(), status(),
          fmt::format("Invalid RT mode: {}. Valid values: tick-complete, period-complete, "
                      "skip-on-busy, lag-tolerant, log-only",
                      modeStr));
      return status();
    }
  }

  if (parsedArgs_.count(RT_MAX_LAG)) {
    rtConfig_.maxLagTicks = std::stoul(std::string(parsedArgs_[RT_MAX_LAG][0]));
  }

  // ===== Validate Shutdown Configuration =====
  if (shutdownConfig_.mode == ShutdownConfig::SCHEDULED && shutdownConfig_.shutdownAtEpochNs == 0) {
    setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
    sysLog_->error(label(), status(), "SCHEDULED shutdown mode requires --shutdown-at argument");
    return status();
  }

  if (shutdownConfig_.mode == ShutdownConfig::RELATIVE_TIME &&
      shutdownConfig_.relativeSeconds == 0) {
    setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
    sysLog_->error(label(), status(),
                   "RELATIVE_TIME shutdown mode requires --shutdown-after argument");
    return status();
  }

  if (shutdownConfig_.mode == ShutdownConfig::CLOCK_CYCLE &&
      shutdownConfig_.targetClockCycle == 0) {
    setStatus(static_cast<std::uint8_t>(ERROR_ARG_PARSE_FAIL));
    sysLog_->error(label(), status(),
                   "CLOCK_CYCLE shutdown mode requires --shutdown-cycle argument");
    return status();
  }

  setStatus(static_cast<std::uint8_t>(SUCCESS));
  return status();
}

} // namespace executive
