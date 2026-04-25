/**
 * @file ApexExecutive_TaskExecution.cpp
 * @brief Implementation of task execution coordination and profiling.
 *
 * Execution Flow:
 *  1. Wait for startup signal
 *  2. Wait for clock synchronization
 *  3. Main loop: wait for tick, execute scheduler, profile, handle pause
 *  4. Exit on shutdown signal or clock stop
 *
 * Profiling Optimization:
 *  Timing points are only captured when profiling is enabled, achieving
 *  zero overhead when disabled (just a branch check per frame).
 *  When enabled, overhead is ~1-2us per sampled frame (0.01-0.02% at 100Hz).
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <vector>

#include <fmt/core.h>

namespace executive {

/* ----------------------------- Timing Helpers ----------------------------- */

/**
 * @struct TimingPoints
 * @brief Simplified timing breakdowns for actionable profiling.
 *
 * Captures timestamps at key execution points:
 *  - t0: Loop start (after wait)
 *  - t1: Before scheduler execution
 *  - t2: After scheduler execution
 *  - t3: After atomic operations (loop end)
 *  - tickNs: Clock tick timestamp (for latency measurement)
 *
 * All times in nanoseconds (high resolution).
 */
struct TimingPoints {
  std::int64_t t0Ns{0};   ///< Loop start (after wake from wait).
  std::int64_t t1Ns{0};   ///< Before scheduler execution.
  std::int64_t t2Ns{0};   ///< After scheduler execution.
  std::int64_t t3Ns{0};   ///< After atomic wrap-up (loop end).
  std::int64_t tickNs{0}; ///< Clock tick timestamp (for latency).
};

/**
 * @brief Convert nanoseconds to milliseconds.
 * @param ns Time in nanoseconds.
 * @return Time in milliseconds (fractional).
 */
inline constexpr double nsToMs(std::int64_t ns) noexcept {
  return static_cast<double>(ns) / 1'000'000.0;
}

/**
 * @brief Compute profiling metrics from timing points.
 * @param tp Timing points captured during loop.
 * @param tick Current tick being executed.
 * @param outBuf Output buffer for formatted string (must be >=256 bytes).
 * @return Pointer to outBuf (for convenience).
 */
inline char* computeProfilingMetrics(const TimingPoints& tp, std::uint16_t tick,
                                     char* outBuf) noexcept {
  const double tickToExecMs = (tp.tickNs > 0) ? nsToMs(tp.t1Ns - tp.tickNs) : -1.0;
  const double execMs = nsToMs(tp.t2Ns - tp.t1Ns);
  const double wrapMs = nsToMs(tp.t3Ns - tp.t2Ns);
  const double loopMs = nsToMs(tp.t3Ns - tp.t0Ns);

  std::snprintf(outBuf, 256, "tick=%u tickToExec=%.3f exec=%.3f wrap=%.3f loop=%.3f ms", tick,
                tickToExecMs, execMs, wrapMs, loopMs);
  return outBuf;
}

/* ----------------------------- Task Execution ----------------------------- */

void ApexExecutive::executeTasks(std::promise<std::uint8_t>&& p) noexcept {
  // Wait for startup signal
  {
    std::unique_lock lock(cvMutex_);
    cvClockTick_.wait(lock, [this]() {
      return controlState_.startupRequested.load(std::memory_order_acquire) ||
             controlState_.shutdownRequested.load(std::memory_order_acquire);
    });

    if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
      sysLog_->info(label(), "Task execution not started (shutdown before startup)");
      p.set_value(static_cast<uint8_t>(Status::SUCCESS));
      return;
    }
  }

  sysLog_->debug(label(), "Task execution waiting for clock sync", 1);

  // Wait for clock synchronization
  while (!clockState_.isRunning.load(std::memory_order_acquire)) {
    std::unique_lock lock(cvMutex_);
    cvClockTick_.wait(lock, [this]() {
      return clockState_.isRunning.load(std::memory_order_acquire) ||
             controlState_.shutdownRequested.load(std::memory_order_acquire);
    });

    if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
      sysLog_->info(label(), "Task execution not started (shutdown before clock sync)");
      p.set_value(static_cast<uint8_t>(Status::SUCCESS));
      return;
    }
  }

  sysLog_->info(label(), "Task execution started");

  // Main execution loop
  while (clockState_.isRunning.load(std::memory_order_acquire)) {
    // Check shutdown before waiting (early exit)
    if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
      break;
    }

    // Wait for clock tick signal
    {
      std::unique_lock lock(cvMutex_);
      cvClockTick_.wait(lock, [this]() {
        return taskState_.stepPending.load(std::memory_order_acquire) ||
               controlState_.shutdownRequested.load(std::memory_order_acquire);
      });
    }

    // Check shutdown after wake (covers both emergency and normal shutdown)
    if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
      break;
    }

    // Load current task cycle count
    const std::uint64_t taskCycles = taskState_.cycles.load(std::memory_order_acquire);

    // Profiling: Conditional timing capture (zero overhead when disabled)
    TimingPoints tp;
    const bool shouldProfile =
        (profilingState_.sampleEveryN > 0) && ((taskCycles % profilingState_.sampleEveryN) == 0);

    if (shouldProfile) {
      // t0: Loop start (just after wake)
      auto now = std::chrono::steady_clock::now();
      tp.t0Ns =
          std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

      // Load clock tick timestamp for latency measurement
      tp.tickNs = clockState_.lastTickNs.load(std::memory_order_acquire);
    }

    // Calculate tick to execute (wraps at clock frequency)
    const std::uint16_t tick = taskCycles % clockFrequency_;

    if (shouldProfile) {
      // t1: Just before scheduler execution
      tp.t1Ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
    }

    sysLog_->debug(label(), fmt::format("Executing tasks for tick {}", tick), 3);

    // Pre-tick I/O drain: telemetry out, then commands in.
    // Draining telemetry first ensures components start with empty outboxes.
    // Telemetry from tick N-1 goes out at start of tick N (1-tick latency).
    // This ordering is multithread-safe: no race between task completion and drain.
    if (interface_ && interface_->isInitialized()) {
      interface_->drainTelemetryOutboxes();
      interface_->drainCommandsToComponents();

      // Deferred RELOAD_EXECUTIVE: the ACK is now queued in the TX buffer.
      // Flush it to the wire, then execv. The client receives the ACK before
      // the connection drops.
      if (controlState_.restartPending.load(std::memory_order_acquire)) {
        // One more TX drain to push the ACK out.
        interface_->drainTelemetryOutboxes();
        interface_->pollSockets(0);

        sysLog_->flush();
        if (auto* sl = fileSystem_.swapLog()) {
          sl->flush();
        }

        // Build argv and exec.
        const std::string EXEC_STR = restartExecTarget_.string();
        std::vector<const char*> argv;
        argv.push_back(EXEC_STR.c_str());
        for (const auto& arg : args_) {
          argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);
        execv(EXEC_STR.c_str(), const_cast<char* const*>(argv.data()));

        // execv failed -- roll back bank swap and continue.
        const int EXEC_ERRNO = errno;
        sysLog_->warning(
            label(), static_cast<std::uint8_t>(0x01),
            fmt::format("execv failed (errno={}): {} -- system continues with old binary",
                        EXEC_ERRNO, std::strerror(EXEC_ERRNO)));
        if (restartDidSwapBinary_) {
          const std::string FN = restartExecTarget_.filename().string();
          if (fileSystem_.swapBankFile(system_core::filesystem::BIN_DIR, FN)) {
            sysLog_->info(label(), "RELOAD_EXECUTIVE: bank swap rolled back after execv failure");
          } else {
            sysLog_->warning(
                label(), static_cast<std::uint8_t>(0x01),
                "RELOAD_EXECUTIVE: bank swap rollback failed, on-disk state inconsistent");
          }
        }
        controlState_.restartPending.store(false, std::memory_order_release);
      }
    }

    // Execute scheduled tasks (core work)
    [[maybe_unused]] auto taskStatus = scheduler_.executeTasksOnTickMulti(tick);

    // Run action engine: evaluate watchpoints, tick sequences, apply data-writes.
    // Runs after scheduler so it observes freshly-written task outputs.
    actionComp_.tick(static_cast<std::uint32_t>(taskCycles));

    if (shouldProfile) {
      // t2: Just after scheduler returns
      tp.t2Ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
    }

    // Clear step flag and increment cycle counter
    taskState_.stepPending.store(false, std::memory_order_release);
    taskState_.cycles.fetch_add(1, std::memory_order_acq_rel);

    if (shouldProfile) {
      // t3: After atomic operations (loop end)
      tp.t3Ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();

      // Compute and log profiling metrics
      const double loopMs = nsToMs(tp.t3Ns - tp.t0Ns);

      // Update min/max/total statistics
      if (loopMs < profilingState_.minLoopTimeMs) {
        profilingState_.minLoopTimeMs = loopMs;
      }
      if (loopMs > profilingState_.maxLoopTimeMs) {
        profilingState_.maxLoopTimeMs = loopMs;
      }
      profilingState_.totalLoopTimeMs += loopMs;
      profilingState_.sampleCount++;

      // Format and log detailed breakdown (stack-allocated buffer)
      char metricsBuf[256];
      computeProfilingMetrics(tp, tick, metricsBuf);
      profLog_->info(label(), metricsBuf);
    }

    // Handle pause (check after completing tick)
    if (controlState_.isPaused.load(std::memory_order_acquire)) {
      sysLog_->info(label(), fmt::format("Task execution paused at cycle {}",
                                         taskState_.cycles.load(std::memory_order_acquire)));

      std::unique_lock lock(cvMutex_);
      cvPause_.wait(lock, [this]() {
        return !controlState_.isPaused.load(std::memory_order_acquire) ||
               controlState_.shutdownRequested.load(std::memory_order_acquire);
      });

      if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
        break;
      }

      sysLog_->info(label(), "Task execution resumed");
    }
  }

  sysLog_->info(label(), fmt::format("Task execution stopped ({} cycles)",
                                     taskState_.cycles.load(std::memory_order_acquire)));
  p.set_value(static_cast<uint8_t>(Status::SUCCESS));
}

} // namespace executive
