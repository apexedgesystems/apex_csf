/**
 * @file ApexExecutive_Shutdown.cpp
 * @brief Implementation of ApexExecutive shutdown logic with comprehensive statistics.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive_Shutdown.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>

#include <fmt/core.h>

using enum executive::Status;

namespace executive {

// Helper: Get current time in nanoseconds (inline for performance)
static inline int64_t getCurrentTimeNs() noexcept {
  auto now = std::chrono::high_resolution_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

// Helper: Broadcast shutdown request to all threads
void ApexExecutive::broadcastShutdownRequest() noexcept {
  controlState_.shutdownRequested.store(true, std::memory_order_release);
  cvShutdown_.notify_all();
}

// Shutdown Logic - Staged approach for clean resource cleanup
void ApexExecutive::shutdownThread(std::promise<std::uint8_t>&& p) noexcept {
  int signum = 0;
  ShutdownStage stage = ShutdownStage::STAGE_SIGNAL_RECEIVED;
  bool shutdownTriggered = false;

  // Stack-based buffer for shutdown reason (no heap allocation)
  char reasonBuf[256] = "unknown";

  sysLog_->debug(label(), "Shutdown thread started, waiting for trigger", 1);

  // Wait for shutdown to be triggered (either by signal or programmed condition)
  while (!shutdownTriggered && !controlState_.shutdownRequested.load(std::memory_order_acquire)) {
    bool programmedShutdown = false;

    switch (shutdownConfig_.mode) {
    case ShutdownConfig::SCHEDULED: {
      const int64_t nowNs = getCurrentTimeNs();

      if (nowNs >= shutdownConfig_.shutdownAtEpochNs) {
        programmedShutdown = true;
        const int64_t nowMs = nowNs / 1'000'000;
        const int64_t targetMs = shutdownConfig_.shutdownAtEpochNs / 1'000'000;
        snprintf(reasonBuf, sizeof(reasonBuf), "SCHEDULED shutdown at %ld ms (now=%ld ms)",
                 targetMs, nowMs);
      }
      break;
    }

    case ShutdownConfig::RELATIVE_TIME: {
      const int64_t startNs = startupCompletedNs_.load(std::memory_order_acquire);
      if (startNs > 0) {
        const int64_t nowNs = getCurrentTimeNs();
        const int64_t elapsedNs = nowNs - startNs;
        const int64_t targetNs =
            static_cast<int64_t>(shutdownConfig_.relativeSeconds) * 1'000'000'000LL;

        if (elapsedNs >= targetNs) {
          programmedShutdown = true;
          const uint32_t elapsedSec = static_cast<uint32_t>(elapsedNs / 1'000'000'000LL);
          snprintf(reasonBuf, sizeof(reasonBuf),
                   "RELATIVE_TIME shutdown after %u seconds (%u target)", elapsedSec,
                   shutdownConfig_.relativeSeconds);
        }
      }
      break;
    }

    case ShutdownConfig::CLOCK_CYCLE: {
      // Cycle-based shutdown is handled by clock thread for exact accuracy
      break;
    }

    case ShutdownConfig::COMBINED: {
      // Check time-based conditions only; cycle check handled by clock thread
      bool scheduledReached = false;
      bool relativeReached = false;

      if (shutdownConfig_.shutdownAtEpochNs > 0) {
        const int64_t nowNs = getCurrentTimeNs();
        scheduledReached = (nowNs >= shutdownConfig_.shutdownAtEpochNs);
      }

      if (shutdownConfig_.relativeSeconds > 0) {
        const int64_t startNs = startupCompletedNs_.load(std::memory_order_acquire);
        if (startNs > 0) {
          const int64_t nowNs = getCurrentTimeNs();
          const int64_t elapsedNs = nowNs - startNs;
          const int64_t targetNs =
              static_cast<int64_t>(shutdownConfig_.relativeSeconds) * 1'000'000'000LL;
          relativeReached = (elapsedNs >= targetNs);
        }
      }

      if (scheduledReached || relativeReached) {
        programmedShutdown = true;
        snprintf(reasonBuf, sizeof(reasonBuf), "COMBINED shutdown (scheduled=%d relative=%d)",
                 scheduledReached, relativeReached);
      }
      break;
    }

    case ShutdownConfig::SIGNAL_ONLY:
    default:
      break;
    }

    if (programmedShutdown) {
      shutdownTriggered = true;
      signum = 0;
      sysLog_->info(label(), fmt::format("Programmed shutdown triggered: {}", reasonBuf));
      broadcastShutdownRequest();
      break;
    }

    // Wait for signal with timeout so we can periodically check conditions
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 100'000'000; // 100ms

    int waitResult = sigtimedwait(&signalSet_, nullptr, &timeout);

    if (waitResult > 0) {
      signum = waitResult;
      shutdownTriggered = true;
      snprintf(reasonBuf, sizeof(reasonBuf), "OS signal %d", signum);
      sysLog_->info(label(), fmt::format("Signal-triggered shutdown: {}", reasonBuf));
      broadcastShutdownRequest();
      break;
    } else if (waitResult == -1 && errno != EAGAIN && errno != EINTR) {
      sysLog_->error(label(), status(),
                     fmt::format("sigtimedwait failed: {} (errno: {})", strerror(errno), errno));
      signum = SIGTERM;
      shutdownTriggered = true;
      snprintf(reasonBuf, sizeof(reasonBuf), "error in signal handling");
      broadcastShutdownRequest();
      break;
    }
  }

  // If we exited loop because shutdownRequested was set externally
  if (!shutdownTriggered && controlState_.shutdownRequested.load(std::memory_order_acquire)) {
    shutdownTriggered = true;
    signum = 0;

    const std::uint64_t cycles = clockState_.cycles.load(std::memory_order_acquire);
    const std::uint64_t overruns = clockState_.overrunCount.load(std::memory_order_acquire);

    if (shutdownConfig_.mode == ShutdownConfig::CLOCK_CYCLE) {
      snprintf(reasonBuf, sizeof(reasonBuf), "CLOCK_CYCLE shutdown at cycle %lu", cycles);
    } else if (shutdownConfig_.mode == ShutdownConfig::COMBINED) {
      snprintf(reasonBuf, sizeof(reasonBuf), "COMBINED shutdown (cycle condition at cycle %lu)",
               cycles);
    } else if (overruns > 0 && rtConfig_.isHardMode()) {
      snprintf(reasonBuf, sizeof(reasonBuf), "HARD_RT_FAILURE after %lu overruns at cycle %lu",
               overruns, cycles);
    } else {
      snprintf(reasonBuf, sizeof(reasonBuf), "external shutdown request");
    }

    sysLog_->info(label(), fmt::format("Shutdown triggered: {}", reasonBuf));
  }

  try {
    // ============================================================
    // STAGE 1: Signal Received - Broadcast shutdown intent
    // ============================================================
    stage = ShutdownStage::STAGE_SIGNAL_RECEIVED;
    sysLog_->debug(label(), "Entering Stage 1: Signal Received", 2);
    shutdownStage1SignalReceived(signum);

    // ============================================================
    // STAGE 2: Stop Clock - Prevent new work from being scheduled
    // ============================================================
    stage = ShutdownStage::STAGE_STOP_CLOCK;
    sysLog_->debug(label(), "Entering Stage 2: Stop Clock", 2);
    shutdownStage2StopClock();

    // ============================================================
    // STAGE 3: Drain Tasks - Wait for in-flight work to complete
    // ============================================================
    stage = ShutdownStage::STAGE_DRAIN_TASKS;
    sysLog_->debug(label(), "Entering Stage 3: Drain Tasks", 2);
    shutdownStage3DrainTasks();

    // ============================================================
    // STAGE 4: Cleanup Resources - Orderly resource deallocation
    // ============================================================
    stage = ShutdownStage::STAGE_CLEANUP_RESOURCES;
    sysLog_->debug(label(), "Entering Stage 4: Cleanup Resources", 2);
    shutdownStage4CleanupResources();

    // ============================================================
    // STAGE 5: Final Statistics - Report system state
    // ============================================================
    stage = ShutdownStage::STAGE_FINAL_STATS;
    sysLog_->debug(label(), "Entering Stage 5: Final Statistics", 2);
    shutdownStage5FinalStats();

    stage = ShutdownStage::STAGE_COMPLETE;
    sysLog_->info(label(),
                  fmt::format("Shutdown sequence completed successfully (reason: {})", reasonBuf));

  } catch (const std::exception& e) {
    sysLog_->error(
        label(), static_cast<std::uint8_t>(ERROR_SHUTDOWN_FAILURE),
        fmt::format("Exception during shutdown stage {}: {}", static_cast<int>(stage), e.what()));
  } catch (...) {
    sysLog_->error(
        label(), static_cast<std::uint8_t>(ERROR_SHUTDOWN_FAILURE),
        fmt::format("Unknown exception during shutdown stage {}", static_cast<int>(stage)));
  }

  p.set_value(static_cast<uint8_t>(signum));
}

// Stage 1: Broadcast shutdown intent to all threads
void ApexExecutive::shutdownStage1SignalReceived(int signum) noexcept {
  const char* signalName = "UNKNOWN";
  switch (signum) {
  case SIGINT:
    signalName = "SIGINT (Ctrl+C)";
    break;
  case SIGTERM:
    signalName = "SIGTERM (kill)";
    break;
  case SIGQUIT:
    signalName = "SIGQUIT (Ctrl+\\)";
    break;
  case SIGHUP:
    signalName = "SIGHUP (hangup)";
    break;
  case 0:
    signalName = "PROGRAMMED";
    break;
  }

  sysLog_->info(label(), fmt::format("Shutdown signal received: {} ({})", signalName, signum));

  broadcastShutdownRequest();

  cvStartup_.notify_all();
  cvClockTick_.notify_all();
  cvPause_.notify_all();

  std::atomic_signal_fence(std::memory_order_seq_cst);

  shutdownState_.stage1Complete.store(true, std::memory_order_release);
  sysLog_->debug(label(), "Stage 1 complete", 2);
}

// Stage 2: Stop clock from generating new ticks
void ApexExecutive::shutdownStage2StopClock() noexcept {
  sysLog_->info(label(), "Stage 2: Stopping clock...");

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);

  while (clockState_.isRunning.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (clockState_.isRunning.load(std::memory_order_acquire)) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_CLOCK_STOP_TIMEOUT),
                     "Clock did not stop within timeout (2s), forcing shutdown");
  } else {
    sysLog_->info(label(), "Clock stopped successfully");
  }

  shutdownState_.stage2Complete.store(true, std::memory_order_release);
  sysLog_->debug(label(), "Stage 2 complete", 2);
}

// Stage 3: Wait for task execution to drain in-flight work
void ApexExecutive::shutdownStage3DrainTasks() noexcept {
  sysLog_->info(label(), "Stage 3: Draining in-flight tasks...");

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

  while (scheduler_.threadsRunning() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (scheduler_.threadsRunning()) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_TASK_DRAIN_TIMEOUT),
                     "Tasks still running after timeout (5s), proceeding with shutdown");
  } else {
    sysLog_->info(label(), "All tasks drained successfully");
  }

  shutdownState_.stage3Complete.store(true, std::memory_order_release);
  sysLog_->debug(label(), "Stage 3 complete", 2);
}

// Stage 4: Resource coordination and final preparations
void ApexExecutive::shutdownStage4CleanupResources() noexcept {
  sysLog_->info(label(), "Stage 4: Coordinating resource cleanup...");

  // Lightweight resource coordination only.
  // Filesystem archive/cleanup happens automatically in ApexFileSystem destructor,
  // ensuring complete logging before archive operation.

  sysLog_->info(label(), "Resource coordination complete");
  shutdownState_.stage4Complete.store(true, std::memory_order_release);
  sysLog_->debug(label(), "Stage 4 complete", 2);
}

// Stage 5: Report final statistics
void ApexExecutive::shutdownStage5FinalStats() noexcept {
  sysLog_->info(label(), "Stage 5: Reporting final statistics...");

  // Calculate runtime duration
  const int64_t startNs = startupCompletedNs_.load(std::memory_order_acquire);
  const int64_t nowNs = getCurrentTimeNs();
  const int64_t runtimeNs = (startNs > 0) ? (nowNs - startNs) : 0;
  const double runtimeSec = static_cast<double>(runtimeNs) / 1'000'000'000.0;

  // Load state values
  const std::uint64_t clockCycles = clockState_.cycles.load(std::memory_order_acquire);
  const std::uint64_t taskCycles = taskState_.cycles.load(std::memory_order_acquire);
  const std::uint64_t frameOverruns = clockState_.overrunCount.load(std::memory_order_acquire);
  const bool lagExceeded = taskState_.lagThresholdExceeded.load(std::memory_order_acquire);

  // Calculate cycle lag and completion rate
  const std::uint64_t cycleLag = (clockCycles > taskCycles) ? (clockCycles - taskCycles) : 0;
  const double completionRate = (clockCycles > 0) ? (100.0 * taskCycles / clockCycles) : 0.0;

  // Report comprehensive statistics
  sysLog_->info(label(), "=== Final Shutdown Statistics ===");
  sysLog_->info(label(), fmt::format("Runtime: {:.3f} seconds", runtimeSec));
  sysLog_->info(label(), fmt::format("Clock cycles completed: {}", clockCycles));
  sysLog_->info(label(), fmt::format("Task execution cycles: {}", taskCycles));
  sysLog_->info(label(),
                fmt::format("Cycle lag: {} ({:.2f}% completion)", cycleLag, completionRate));
  const double framePeriodMs = 1000.0 / clockFrequency_;
  sysLog_->info(label(), fmt::format("Frame overruns: {} (frame = {:.1f}ms at {}Hz)", frameOverruns,
                                     framePeriodMs, clockFrequency_));
  if (rtConfig_.mode == RTMode::SOFT_LAG_TOLERANT && lagExceeded) {
    sysLog_->info(label(), fmt::format("Lag threshold exceeded: yes (max allowed: {} ticks)",
                                       rtConfig_.maxLagTicks));
  }
  sysLog_->info(label(), fmt::format("Watchdog warnings: {}", watchdogState_.warningCount));

  // Profiling statistics if enabled
  if (profilingState_.isActive() && profilingState_.sampleCount > 0) {
    const double avgLoopTimeMs = profilingState_.averageLoopTimeMs();
    const double targetLoopMs = 1000.0 / clockFrequency_;
    const double utilizationPct = (avgLoopTimeMs / targetLoopMs) * 100.0;

    sysLog_->info(label(), "=== Profiling Summary ===");
    sysLog_->info(label(), fmt::format("Samples collected: {}", profilingState_.sampleCount));
    sysLog_->info(label(),
                  fmt::format("Loop time - min: {:.3f} ms", profilingState_.minLoopTimeMs));
    sysLog_->info(label(),
                  fmt::format("Loop time - max: {:.3f} ms", profilingState_.maxLoopTimeMs));
    sysLog_->info(label(), fmt::format("Loop time - avg: {:.3f} ms", avgLoopTimeMs));
    sysLog_->info(label(), fmt::format("Frame budget: {:.3f} ms", targetLoopMs));
    sysLog_->info(label(), fmt::format("Average utilization: {:.1f}%", utilizationPct));
  }

  sysLog_->info(label(), "=================================");

  shutdownState_.stage5Complete.store(true, std::memory_order_release);
  sysLog_->debug(label(), "Stage 5 complete", 2);

  // CRITICAL: Flush all logs to ensure shutdown stats reach disk before exit
  // For async mode: waits for I/O thread to drain queue and sync to disk
  // For sync mode: calls fsync() on file descriptor
  // Without this, final shutdown stats may be lost on rapid process termination
  sysLog_->flush();
}

} // namespace executive