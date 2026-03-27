/**
 * @file ApexExecutive_Watchdog.cpp
 * @brief Implementation of watchdog monitoring with health checks and heartbeat emission.
 */

#include "src/system/core/executive/apex/inc/ApexExecutive.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <iostream>

#include <fmt/core.h>

using namespace executive;
using enum executive::Status;

void ApexExecutive::watchdogCheck() noexcept {
  const std::uint64_t CLOCK_NOW = clockState_.cycles.load(std::memory_order_acquire);
  const std::uint64_t TASK_NOW = taskState_.cycles.load(std::memory_order_acquire);

  // Only check clock progress if clock has started ticking
  if (clockState_.isRunning.load(std::memory_order_acquire) && CLOCK_NOW > 0) {
    if (CLOCK_NOW == watchdogState_.lastClockCycles) {
      watchdogState_.warningCount++;
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_CLOCK_FROZEN),
                       fmt::format("Clock appears frozen at cycle {}", CLOCK_NOW));

      // Console output gated by verbosity (level 1+)
      if (sysLog_->verbosity() >= 1) {
        std::cout << "[WATCHDOG] WARNING: Clock frozen at cycle " << CLOCK_NOW << std::endl;
      }
    }
    // Update last cycles only when clock is running
    watchdogState_.lastClockCycles = CLOCK_NOW;
  }

  // Check task execution lag (only if tasks are executing)
  if (clockState_.isRunning.load(std::memory_order_acquire) && CLOCK_NOW > 0) {
    if (CLOCK_NOW > TASK_NOW) {
      const std::uint64_t LAG = CLOCK_NOW - TASK_NOW;
      if (LAG > (clockFrequency_ / 10)) { // Lag > 100ms worth of cycles
        watchdogState_.warningCount++;
        sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_CLOCK_DRIFT),
                         fmt::format("Task execution lagging: clock={} tasks={} lag={} cycles",
                                     CLOCK_NOW, TASK_NOW, LAG));
      }
    }
  }
}

void ApexExecutive::emitHeartbeat() noexcept {
  const auto NOW = std::chrono::high_resolution_clock::now();
  const std::int64_t TIMESTAMP_NS =
      std::chrono::duration_cast<std::chrono::nanoseconds>(NOW.time_since_epoch()).count();

  const std::uint64_t CLOCK_NOW = clockState_.cycles.load(std::memory_order_acquire);
  const std::uint64_t TASK_NOW = taskState_.cycles.load(std::memory_order_acquire);
  const std::uint64_t OVERRUNS = clockState_.overrunCount.load(std::memory_order_acquire);

  // Determine status
  // The clock thread may tick between the two atomic loads above, creating a
  // transient (clock=N+k, tasks=N) that resolves before the next heartbeat.
  // At high clock rates multiple ticks can fire during the read window.
  // Tolerance = 1% of clock frequency (1 tick at 100Hz, 10 at 1kHz, 100 at 10kHz).
  // Real lag is caught by watchdogCheck() (>100ms threshold).
  const std::uint64_t SAMPLING_TOLERANCE = (clockFrequency_ >= 100) ? (clockFrequency_ / 100) : 1;
  const char* STATUS = "OK";
  const std::uint64_t LAG = (CLOCK_NOW > TASK_NOW) ? (CLOCK_NOW - TASK_NOW) : 0;
  if (OVERRUNS > 0 || LAG > SAMPLING_TOLERANCE) {
    STATUS = "WARNING";
  }

  // Write CSV line to heartbeat log (always)
  heartbeatLog_->info("HEARTBEAT", fmt::format("{},{},{},{},{}", TIMESTAMP_NS, CLOCK_NOW, TASK_NOW,
                                               OVERRUNS, STATUS));

  // Write heartbeat byte to supervisor pipe (if running under apex_watchdog)
  if (watchdogState_.supervisorFd >= 0) {
    const char BEAT = '.';
    if (::write(watchdogState_.supervisorFd, &BEAT, 1) < 0) { /* best-effort */
    }
  }

  // Console output gated by verbosity (level 2+)
  if (sysLog_->verbosity() >= 2) {
    if (CLOCK_NOW == TASK_NOW && OVERRUNS == 0) {
      std::cout << fmt::format("[WATCHDOG] {} | Clock: {} | Tasks: {} | Overruns: {}\n", STATUS,
                               CLOCK_NOW, TASK_NOW, OVERRUNS);
    } else {
      const std::uint64_t LAG = (CLOCK_NOW > TASK_NOW) ? (CLOCK_NOW - TASK_NOW) : 0;
      std::cout << fmt::format("[WATCHDOG] {} | Clock: {} | Tasks: {} | Lag: {} | Overruns: {}\n",
                               STATUS, CLOCK_NOW, TASK_NOW, LAG, OVERRUNS);
    }
  }
}

void ApexExecutive::watchdog(std::promise<std::uint8_t>&& p) noexcept {
  // Wait for startup to complete
  {
    std::unique_lock lock(cvMutex_);
    cvStartup_.wait(lock, [this]() {
      return controlState_.startupRequested.load(std::memory_order_acquire) ||
             controlState_.shutdownRequested.load(std::memory_order_acquire);
    });

    if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
      sysLog_->info(label(), "Watchdog not started, shutdown signal received prior to startup");
      p.set_value(static_cast<uint8_t>(Status::SUCCESS));
      return;
    }
  }

  sysLog_->info(label(),
                fmt::format("Watchdog started (interval: {} ms)", watchdogState_.intervalMs));

  // Console notification gated by verbosity (level 1+)
  if (sysLog_->verbosity() >= 1) {
    std::cout << "[WATCHDOG] Started with " << watchdogState_.intervalMs << " ms interval"
              << std::endl;
  }

  // Use interval from executive configuration
  const auto WATCHDOG_INTERVAL = std::chrono::milliseconds(watchdogState_.intervalMs);

  while (!controlState_.shutdownRequested.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(WATCHDOG_INTERVAL);

    // Perform health checks
    watchdogCheck();
    emitHeartbeat();
  }

  sysLog_->info(label(), "Watchdog stopped");
  p.set_value(static_cast<uint8_t>(Status::SUCCESS));
}
