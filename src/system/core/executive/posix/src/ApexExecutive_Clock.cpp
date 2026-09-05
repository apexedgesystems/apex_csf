/**
 * @file ApexExecutive_Clock.cpp
 * @brief Optimized clock implementation with frame overrun detection.
 *
 * Clock/Task Cycle Relationship:
 *  - clockState_.cycles tracks ticks SENT to task execution (incremented AFTER signal)
 *  - taskState_.cycles tracks ticks COMPLETED by task execution
 *  - Invariant: clockCycles == taskCycles + 1 (when no overruns)
 *  - Example: When executing tick N, clockCycles = N+1, taskCycles = N
 *
 * Frame Overrun Detection:
 *  Two conditions indicate overrun (both checked):
 *   1. stepPending still true: Task execution hasn't consumed previous tick
 *   2. threadsRunning() true: Thread pool still executing previous tick's tasks
 *
 *  In hard RT mode, first overrun triggers emergency shutdown.
 *  In soft RT mode, overruns are logged but execution continues.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/RTMode.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include <fmt/core.h>

using enum executive::Status;

namespace executive {

void ApexExecutive::clock(std::promise<std::uint8_t>&& p) noexcept {
  sysLog_->debug(label(), "Clock thread waiting for startup signal", 1);

  // Wait for startup signal or early shutdown
  {
    std::unique_lock lock(cvMutex_);
    cvStartup_.wait(lock, [this]() {
      return controlState_.startupRequested.load(std::memory_order_acquire) ||
             controlState_.shutdownRequested.load(std::memory_order_acquire);
    });

    if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
      sysLog_->info(label(), "Clock not started, shutdown signal received prior to startup");
      cvClockTick_.notify_all();
      p.set_value(static_cast<uint8_t>(Status::SUCCESS));
      return;
    }
  }

  // Mark clock as synchronized (allows task execution to proceed)
  clockState_.isRunning.store(true, std::memory_order_release);
  sysLog_->debug(label(), "Clock synchronization flag set to true", 2);

  // Configurable startup delay (allows other threads to stabilize)
  const auto STARTUP_DELAY = std::chrono::milliseconds(2000);
  std::this_thread::sleep_for(STARTUP_DELAY);
  sysLog_->info(label(), "Clock started");

  const auto FRAME_MS = 1000 / clockFrequency_;
  sysLog_->info(label(), fmt::format("RT mode: {} (hard={})", rtModeToString(rtConfig_.mode),
                                     rtConfig_.isHardMode() ? "yes" : "no"));
  sysLog_->debug(label(), fmt::format("Frame period: {} ms ({} Hz)", FRAME_MS, clockFrequency_), 2);

  // Main clock loop: sleep to the ABSOLUTE deadline grid, check
  // overrun, signal tick, repeat. The grid is anchored once and each
  // deadline advances by exactly one period: an oversleep (scheduler
  // stall, host contention) is repaid by immediately-due deadlines, so
  // clock cycles track wall time by construction and starved intervals
  // surface as task lag the RT machinery already handles. Re-anchoring
  // to now() each iteration would leak every oversleep into permanent
  // sim-vs-wall drift.
  const auto FRAME_PERIOD = std::chrono::milliseconds(FRAME_MS);
  auto nextTick = std::chrono::steady_clock::now() + FRAME_PERIOD;
  while (!controlState_.shutdownRequested.load(std::memory_order_acquire)) {
    std::this_thread::sleep_until(nextTick);
    nextTick += FRAME_PERIOD;

    // Pathological backlog (host suspend, debugger stop): firing an
    // unbounded catch-up burst would be worse than the gap. Beyond one
    // second of backlog, re-anchor the grid, count the dropped ticks,
    // and warn — bounded intervals keep true catch-up.
    {
      const auto NOW = std::chrono::steady_clock::now();
      if (NOW > nextTick + std::chrono::seconds(1)) {
        const auto BEHIND_NS =
            std::chrono::duration_cast<std::chrono::nanoseconds>(NOW - nextTick).count();
        const std::uint64_t DROPPED =
            static_cast<std::uint64_t>(BEHIND_NS / 1000000) / static_cast<std::uint64_t>(FRAME_MS);
        clockState_.wallResyncDroppedTicks.fetch_add(DROPPED, std::memory_order_acq_rel);
        sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_FRAME_OVERRUN),
                         fmt::format("clock resync: {} ticks dropped after a {} ms stall "
                                     "(host suspend/starvation beyond catch-up policy)",
                                     DROPPED, BEHIND_NS / 1000000));
        nextTick = NOW + FRAME_PERIOD;
      }
    }

    // Load current cycle counts
    const std::uint64_t clockCycles = clockState_.cycles.load(std::memory_order_acquire);
    const std::uint64_t taskCycles = taskState_.cycles.load(std::memory_order_acquire);

    // ========================================================================
    // CRITICAL: Frame Overrun Detection
    // ========================================================================
    // Detection logic depends on RT mode:
    //  - TICK_COMPLETE: Check poolActive || stepPending (any work pending at tick boundary)
    //  - PERIOD_COMPLETE: Check if scheduler detected period violations (task-specific)
    //  - SOFT modes: Log but don't fail
    const bool STEP_PENDING = taskState_.stepPending.load(std::memory_order_acquire);
    const bool POOL_ACTIVE = scheduler_.threadsRunning();
    bool isViolation = false;

    if (rtConfig_.mode == RTMode::HARD_TICK_COMPLETE) {
      // TICK_COMPLETE: Any pending work at tick boundary is a violation
      isViolation = STEP_PENDING || POOL_ACTIVE;
    } else if (rtConfig_.mode == RTMode::HARD_PERIOD_COMPLETE) {
      // PERIOD_COMPLETE: Only check period-specific violations from scheduler
      // The scheduler sets this flag when a task is still running at re-dispatch time
      isViolation = scheduler_.checkAndClearPeriodViolation();
    } else if (rtConfig_.mode == RTMode::SOFT_LAG_TOLERANT) {
      // LAG_TOLERANT: Allow clock to lead task execution by configurable amount
      // Compute current lag and check against threshold
      const std::uint64_t CURRENT_LAG = (clockCycles > taskCycles) ? (clockCycles - taskCycles) : 0;
      if (CURRENT_LAG > rtConfig_.maxLagTicks) {
        sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_FRAME_OVERRUN),
                         fmt::format("Lag threshold exceeded: {} ticks (max: {}). "
                                     "Clock cycle: {}, Task cycle: {}",
                                     CURRENT_LAG, rtConfig_.maxLagTicks, clockCycles, taskCycles));
        // LAG_TOLERANT is a SOFT mode, so isViolation stays false (no shutdown)
        // Track that the threshold was exceeded for diagnostics
        taskState_.lagThresholdExceeded.store(true, std::memory_order_release);
      }
    }
    // Other SOFT modes (LOG_ONLY, SKIP_ON_BUSY): isViolation stays false

    // Frame-loss event: task cycles fell further behind the clock than
    // ever before in this run. Rare by construction (a ratchet), so the
    // in-flight snapshot is affordable -- it names the tasks that were
    // running when the frame was lost, which is the fact post-incident
    // analysis needs.
    {
      const std::uint64_t LAG_NOW = (clockCycles > taskCycles) ? (clockCycles - taskCycles) : 0;
      static thread_local std::uint64_t maxLagSeen = 0;
      if (LAG_NOW > maxLagSeen && LAG_NOW > 1) {
        maxLagSeen = LAG_NOW;
        sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_FRAME_OVERRUN),
                         fmt::format("FRAME_LOSS: lag reached {} at cycle {} -- in-flight: {}",
                                     LAG_NOW, clockCycles, scheduler_.inFlightSummary(4)));
      }
    }

    // Track frame overruns (pool active or step pending at tick boundary)
    // Count is tracked for all modes; summary logged at shutdown.
    // No per-tick warnings here:
    //  - HARD modes: violation triggers immediate FATAL, warning would be redundant
    //  - SOFT modes: overruns are expected by design
    if (STEP_PENDING || POOL_ACTIVE) {
      clockState_.overrunCount.fetch_add(1, std::memory_order_acq_rel);
    }

    // Handle hard RT violations
    if (isViolation && rtConfig_.isHardMode()) {
      const std::uint64_t CYCLE_LAG = (clockCycles > taskCycles) ? (clockCycles - taskCycles) : 0;

      // HARD RT VIOLATION: Stop clock immediately, do NOT send another tick
      const char* violator = scheduler_.lastViolationComponent();
      const std::uint8_t violatorUid = scheduler_.lastViolationTaskUid();
      sysLog_->fatal(
          label(), static_cast<std::uint8_t>(ERROR_HARD_REALTIME_FAILURE),
          fmt::format("Hard real-time constraint violated at cycle {} (mode={}): "
                      "{}. Violator: {} taskUid={}. Clock cycles: {}, Task cycles: {}, Lag: {}. "
                      "Initiating emergency shutdown.",
                      clockCycles, rtModeToString(rtConfig_.mode),
                      rtConfig_.mode == RTMode::HARD_TICK_COMPLETE
                          ? fmt::format("task execution overran frame period ({} ms)", FRAME_MS)
                          : "task missed period deadline (still running at next invocation)",
                      violator != nullptr ? violator : "(unattributed)", violatorUid, clockCycles,
                      taskCycles, CYCLE_LAG));

      // Emergency shutdown sequence
      controlState_.rtFailureShutdown.store(true, std::memory_order_release);
      controlState_.shutdownRequested.store(true, std::memory_order_release);
      cvShutdown_.notify_all();
      cvClockTick_.notify_all();
      break;
    }

    // Early exit check (shutdown may have been requested externally)
    if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
      break;
    }

    // ========================================================================
    // Tick Generation
    // ========================================================================
    // Stamp tick time for profiling (ns since epoch, high resolution)
    const auto TICK_NS = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
    clockState_.lastTickNs.store(TICK_NS, std::memory_order_release);

    // Signal task execution thread to process this tick
    // NOTE: clockCycles incremented AFTER signal, maintaining invariant:
    //       clockCycles = taskCycles + 1 (when no overruns)
    taskState_.stepPending.store(true, std::memory_order_release);
    cvClockTick_.notify_all();
    const std::uint64_t newClockCycles =
        clockState_.cycles.fetch_add(1, std::memory_order_acq_rel) + 1;

    sysLog_->debug(label(), fmt::format("Clock tick {} sent to executors", newClockCycles), 3);

    // ========================================================================
    // Cycle-Based Shutdown Check
    // ========================================================================
    // Note: Primary shutdown detection happens in shutdown thread.
    // This is a fast-path check to avoid unnecessary tick processing.
    if (shutdownConfig_.mode == ShutdownConfig::CLOCK_CYCLE ||
        shutdownConfig_.mode == ShutdownConfig::COMBINED) {
      if (newClockCycles >= shutdownConfig_.targetClockCycle) {
        sysLog_->info(label(),
                      fmt::format("Clock cycle target reached ({} >= {}), triggering shutdown",
                                  newClockCycles, shutdownConfig_.targetClockCycle));
        controlState_.shutdownRequested.store(true, std::memory_order_release);
        cvShutdown_.notify_all();
        break;
      }
    }

    // ========================================================================
    // Pause Handling
    // ========================================================================
    if (controlState_.pauseRequested.load(std::memory_order_acquire) &&
        !controlState_.isPaused.load(std::memory_order_acquire)) {
      handleClockPause();

      // Exit if shutdown requested during pause
      if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
        break;
      }

      // Paused time is suspended, not owed: without a fresh anchor the
      // absolute grid repays the pause as immediately-due deadlines,
      // and back-to-back ticks read as period violations (fatal under
      // hard RT within cycles of resume). Only sub-second stalls repay
      // through the grid; a pause resumes on a clean period boundary.
      nextTick = std::chrono::steady_clock::now() + FRAME_PERIOD;
    }
  }

  // ========================================================================
  // Clock Shutdown Sequence
  // ========================================================================
  // Clear synchronization flag to signal task execution to exit
  clockState_.isRunning.store(false, std::memory_order_release);
  cvClockTick_.notify_all();

  sysLog_->info(label(), fmt::format("Clock stopped after {} cycles",
                                     clockState_.cycles.load(std::memory_order_acquire)));
  sysLog_->debug(label(), "Clock thread exiting cleanly", 1);

  p.set_value(static_cast<uint8_t>(Status::SUCCESS));
}

void ApexExecutive::handleClockPause() noexcept {
  // Mark system as paused
  controlState_.isPaused.store(true, std::memory_order_release);

  const std::uint64_t cycles = clockState_.cycles.load(std::memory_order_acquire);
  sysLog_->info(label(),
                fmt::format("Clock paused at cycle {}, waiting for resume signal", cycles));
  std::cout << "\n=== System PAUSED (cycle " << cycles << ") - Press R+Enter to resume ===\n"
            << std::endl;

  // Wait for resume, shutdown, or a deferred restart. RELOAD_EXECUTIVE
  // must work from a paused system -- the SAFE ingest hold repairs
  // through it -- so a pending restart wakes the clock and releases the
  // task loop, whose per-tick restart processing execs before any task
  // dispatches.
  std::unique_lock lock(cvMutex_);
  cvPause_.wait(lock, [this]() {
    return !controlState_.pauseRequested.load(std::memory_order_acquire) ||
           controlState_.shutdownRequested.load(std::memory_order_acquire) ||
           controlState_.restartPending.load(std::memory_order_acquire);
  });
  if (controlState_.restartPending.load(std::memory_order_acquire)) {
    sysLog_->info(label(), "Deferred restart requested while paused; releasing for exec");
    controlState_.pauseRequested.store(false, std::memory_order_release);
    controlState_.isPaused.store(false, std::memory_order_release);
  }

  // Reset frame overrun counter after pause (fresh start for RT checks)
  clockState_.overrunCount.store(0, std::memory_order_release);

  sysLog_->info(label(), "Clock resumed");
  std::cout << "\n=== System RESUMED ===\n" << std::endl;
}

} // namespace executive
