#ifndef APEX_EXECUTIVE_LITE_LITE_EXECUTIVE_HPP
#define APEX_EXECUTIVE_LITE_LITE_EXECUTIVE_HPP
/**
 * @file LiteExecutive.hpp
 * @brief Minimal executive for resource-constrained systems.
 *
 * Design:
 *   - Single-threaded, cooperative scheduling
 *   - Uses ITickSource abstraction for timing
 *   - Owns SchedulerLite by value
 *   - No heap allocation in hot path
 *   - No std::filesystem, std::thread, std::vector
 *   - Suitable for bare-metal MCU targets
 *
 * Template Parameters:
 *   - MaxTasks: Forwarded to SchedulerLite. Controls static task table size.
 *     Each LiteTaskEntry is 12 bytes. Default 32 (384 bytes).
 *   - Counter: Forwarded to SchedulerLite and used for cycleCount/maxCycles.
 *     Default uint64_t. Use uint32_t on 8/16-bit MCUs.
 *
 * Architecture:
 *   - LiteExecutive owns scheduler, borrows tick source
 *   - Main loop: wait for tick, execute scheduler, repeat
 *   - Shutdown via flag (can be set from ISR)
 *
 * Trade-offs vs ApexExecutive:
 *   - No multi-threading (single-core execution)
 *   - No queue-based async messaging
 *   - No filesystem logging
 *   - No TPRM file loading (compile-time or binary blob config)
 *   - No runtime component registration
 *
 * RT Constraints:
 *   - run() inner loop is RT-safe (depends on tick source and scheduler)
 *   - shutdown() is ISR-safe (just sets flag)
 *   - Query methods are O(1)
 *
 * Usage:
 * @code
 * FreeRunningSource tickSource(100);
 * LiteExecutive<> exec(&tickSource, 100);  // 32-task, uint64_t (default)
 * LiteExecutive<8, uint32_t> exec(&tickSource, 100);  // 8-task, uint32_t (AVR)
 * exec.addTask({myTask, &ctx, 1, 1, 0, 0, 1});
 * exec.init();
 * exec.run();  // Blocks until shutdown
 * @endcode
 *
 * @note Use ApexExecutive for full-featured Linux/RTOS deployments.
 */

#include "src/system/core/components/scheduler/lite/inc/SchedulerLite.hpp"
#include "src/system/core/executive/base/inc/IExecutive.hpp"
#include "src/system/core/executive/lite/inc/ITickSource.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/lite/inc/LiteComponentBase.hpp"

#include <stdint.h>

namespace executive {
namespace lite {

/* ----------------------------- Constants ----------------------------- */

/// Executive component ID (matches apex executive).
constexpr uint16_t LITE_EXECUTIVE_COMPONENT_ID = 0;

/* ----------------------------- LiteExecutive ----------------------------- */

/**
 * @class LiteExecutive
 * @brief Minimal IExecutive implementation for MCU targets.
 *
 * Provides a simple main loop that coordinates a tick source and scheduler.
 * The executive waits for each tick, then executes the scheduler.
 *
 * Inherits IComponent implementation from LiteComponentBase. Implements
 * IExecutive for executive-specific methods (run, shutdown, cycleCount).
 *
 * Owns SchedulerLite by value. Tick source is injected via constructor
 * (dependency injection) to allow testing with FreeRunningSource on
 * desktop while using SysTickSource on MCU.
 *
 * @tparam MaxTasks Maximum tasks in the owned scheduler. Default: 32.
 * @tparam Counter Tick/cycle counter type. Default: uint64_t.
 *
 * @note RT-safe: Main loop is RT-safe if tick source and scheduler are.
 */
template <size_t MaxTasks = system_core::scheduler::lite::DEFAULT_LITE_MAX_TASKS,
          typename Counter = uint64_t>
class LiteExecutive : public system_core::system_component::lite::LiteComponentBase,
                      public IExecutive {
public:
  /// Owned scheduler type.
  using Scheduler = system_core::scheduler::lite::SchedulerLite<MaxTasks, Counter>;

  /**
   * @brief Construct executive with tick source and scheduler frequency.
   * @param tickSource Tick source for timing (borrowed, not owned).
   * @param freqHz Fundamental scheduling frequency in Hz.
   * @param maxCycles Maximum cycles before auto-shutdown (0 = unlimited).
   *
   * Tick source is borrowed (not owned). Caller must ensure it outlives executive.
   * Scheduler is owned by value.
   */
  LiteExecutive(ITickSource* tickSource,
                uint16_t freqHz = system_core::scheduler::lite::DEFAULT_LITE_FREQ_HZ,
                Counter maxCycles = 0) noexcept
      : tickSource_(tickSource), scheduler_(freqHz), maxCycles_(maxCycles) {
    // Executive is self-registered (always instance 0)
    setInstanceIndex(0);
  }

  ~LiteExecutive() override = default;

  // Non-copyable, non-movable
  LiteExecutive(const LiteExecutive&) = delete;
  LiteExecutive& operator=(const LiteExecutive&) = delete;
  LiteExecutive(LiteExecutive&&) = delete;
  LiteExecutive& operator=(LiteExecutive&&) = delete;

  /* ----------------------------- IComponent: Identity ----------------------------- */

  /**
   * @brief Get component type identifier.
   * @return Executive component ID (0).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint16_t componentId() const noexcept override {
    return LITE_EXECUTIVE_COMPONENT_ID;
  }

  /**
   * @brief Get component name.
   * @return "LiteExecutive".
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const char* componentName() const noexcept override { return "LiteExecutive"; }

  /**
   * @brief Get component type classification.
   * @return ComponentType::EXECUTIVE.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] system_core::system_component::ComponentType
  componentType() const noexcept override {
    return system_core::system_component::ComponentType::EXECUTIVE;
  }

  /**
   * @brief Get diagnostic label.
   * @return "EXEC_LITE".
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const char* label() const noexcept override { return "EXEC_LITE"; }

  /* ----------------------------- IExecutive ----------------------------- */

  /**
   * @brief Main executive loop.
   *
   * Runs the main control loop:
   * 1. Start tick source
   * 2. Wait for tick
   * 3. Execute scheduler
   * 4. Repeat until shutdown or max cycles
   *
   * @return RunResult indicating outcome.
   * @note Inner loop is RT-safe if tick source and scheduler are RT-safe.
   */
  [[nodiscard]] RunResult run() noexcept override {
    if (!isInitialized()) {
      return RunResult::ERROR_INIT;
    }

    // Start tick source
    tickSource_->start();
    shutdownRequested_ = false;
    cycleCount_ = 0;

    // Main loop
    while (!shutdownRequested_) {
      // Wait for next tick (skipped in fast-forward mode)
      if (!fastForward_) {
        tickSource_->waitForNextTick();
      }

      // Execute scheduler
      scheduler_.tick();

      // Acknowledge tick (for ISR-driven sources)
      tickSource_->ackTick();

      ++cycleCount_;

      // Check max cycles limit
      if (maxCycles_ > 0 && cycleCount_ >= maxCycles_) {
        break;
      }
    }

    // Stop tick source
    tickSource_->stop();

    return RunResult::SUCCESS;
  }

  /**
   * @brief Request graceful shutdown.
   * @note ISR-safe: Just sets volatile flag.
   */
  void shutdown() noexcept override { shutdownRequested_ = true; }

  /**
   * @brief Check if shutdown requested.
   * @return true if shutdown() called.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isShutdownRequested() const noexcept override { return shutdownRequested_; }

  /**
   * @brief Get completed cycle count.
   * @return Cycles since run() started.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint64_t cycleCount() const noexcept override {
    return static_cast<uint64_t>(cycleCount_);
  }

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get tick source.
   * @return Pointer to tick source.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] ITickSource* tickSource() const noexcept { return tickSource_; }

  /**
   * @brief Get scheduler reference.
   * @return Reference to owned scheduler.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] Scheduler& scheduler() noexcept { return scheduler_; }

  /**
   * @brief Get scheduler reference (const).
   * @return Const reference to owned scheduler.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const Scheduler& scheduler() const noexcept { return scheduler_; }

  /* ----------------------------- Task Registration ----------------------------- */

  /**
   * @brief Add a task to the scheduler.
   * @param entry Task configuration.
   * @return true on success, false if table is full.
   * @note NOT RT-safe: Must be called before init().
   */
  bool addTask(const system_core::scheduler::lite::LiteTaskEntry& entry) noexcept {
    return scheduler_.addTask(entry);
  }

  /**
   * @brief Set maximum cycles (0 = unlimited).
   * @param maxCycles Maximum cycles before auto-shutdown.
   * @note NOT RT-safe: Should be called before run().
   */
  void setMaxCycles(Counter maxCycles) noexcept { maxCycles_ = maxCycles; }

  /* ----------------------------- Fast-Forward ----------------------------- */

  /**
   * @brief Enable or disable fast-forward mode.
   *
   * When enabled, the main loop skips waitForNextTick() and executes
   * scheduler ticks at maximum CPU speed. Useful for benchmarking
   * per-tick overhead without tick source latency.
   *
   * @param enabled true to enable fast-forward, false to resume normal timing.
   * @note ISR-safe: Just sets volatile flag.
   */
  void setFastForward(bool enabled) noexcept { fastForward_ = enabled; }

  /**
   * @brief Check if fast-forward mode is active.
   * @return true if fast-forward is enabled.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isFastForward() const noexcept { return fastForward_; }

protected:
  /* ----------------------------- Lifecycle Hooks ----------------------------- */

  /**
   * @brief Initialize executive and scheduler.
   * @return 0 on success, non-zero on failure.
   * @note NOT RT-safe: Initializes scheduler.
   */
  [[nodiscard]] uint8_t doInit() noexcept override {
    if (tickSource_ == nullptr) {
      setLastError("null tick source");
      return 1;
    }

    // Initialize scheduler
    const uint8_t SCHED_RESULT = scheduler_.init();
    if (SCHED_RESULT != 0) {
      setLastError("scheduler init failed");
      return SCHED_RESULT;
    }

    // Register scheduler with this executive
    scheduler_.setInstanceIndex(0);

    return 0;
  }

  /**
   * @brief Reset executive state.
   * @note Resets cycle count, shutdown flag, and scheduler.
   */
  void doReset() noexcept override {
    cycleCount_ = 0;
    shutdownRequested_ = false;
    fastForward_ = false;
    scheduler_.reset();
  }

private:
  ITickSource* tickSource_;
  Scheduler scheduler_;
  Counter cycleCount_{0};
  Counter maxCycles_;
  volatile bool shutdownRequested_{false};
  volatile bool fastForward_{false};
};

} // namespace lite
} // namespace executive

#endif // APEX_EXECUTIVE_LITE_LITE_EXECUTIVE_HPP
