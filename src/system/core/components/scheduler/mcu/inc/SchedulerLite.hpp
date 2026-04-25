#ifndef APEX_SCHEDULER_MCU_SCHEDULER_LITE_HPP
#define APEX_SCHEDULER_MCU_SCHEDULER_LITE_HPP
/**
 * @file SchedulerLite.hpp
 * @brief Minimal scheduler for resource-constrained systems.
 *
 * Design:
 *   - Static task table (compile-time sized via MaxTasks template parameter)
 *   - Simple rate-group execution (no thread pools)
 *   - No heap allocation in hot path
 *   - Suitable for bare-metal MCU targets with McuExecutive
 *
 * Template Parameters:
 *   - MaxTasks: Maximum number of tasks in the static table (default 32).
 *     Set to the actual number of tasks + small margin for the target.
 *     Each LiteTaskEntry is 12 bytes, so this directly controls SRAM usage.
 *   - Counter: Tick counter type (default uint64_t). Use uint32_t on
 *     8/16-bit MCUs to avoid expensive software 64-bit arithmetic.
 *     uint32_t at 100 Hz overflows after ~497 days.
 *
 * Task Scheduling Model:
 *   - Tasks are function pointers with optional context
 *   - Frequency expressed as (freqN/freqD) * fundamentalFreq
 *   - Offset allows phase spreading within a rate group
 *   - Execution order: by rate group, then by priority within group
 *
 * Trade-offs vs SchedulerBase:
 *   - No dynamic task registration (compile-time table)
 *   - No thread pools (sequential execution)
 *   - No TPRM file loading (use compile-time config or binary blob)
 *   - No logging to filesystem (optional console/UART output)
 *   - No sequence groups (simple priority ordering only)
 *
 * RT Constraints:
 *   - tick() is RT-safe (O(n) where n = tasks for this tick)
 *   - No heap allocation in tick()
 *   - All query methods are O(1)
 *
 * @note Use SchedulerBase for full-featured Linux/RTOS deployments.
 */

#include "src/system/core/components/scheduler/base/inc/IScheduler.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/mcu/inc/McuComponentBase.hpp"

#include <stdint.h>
#include <stddef.h>

namespace system_core {
namespace scheduler {
namespace mcu {

/* ----------------------------- Constants ----------------------------- */

/// Default maximum number of tasks in the static task table.
constexpr size_t DEFAULT_LITE_MAX_TASKS = 32;

/// Default fundamental frequency (Hz).
constexpr uint16_t DEFAULT_LITE_FREQ_HZ = 100;

/// Scheduler component ID (matches apex scheduler).
constexpr uint16_t SCHEDULER_LITE_COMPONENT_ID = 1;

/* ----------------------------- LiteTaskEntry ----------------------------- */

/**
 * @struct LiteTaskEntry
 * @brief Static task configuration for SchedulerLite.
 *
 * Describes a task to be executed at a specific rate. All fields are
 * compile-time or boot-time configured. 12 bytes per entry on all platforms.
 */
struct LiteTaskEntry {
  using TaskFn = void (*)(void* ctx) noexcept;

  TaskFn fn{nullptr};     ///< Task function pointer (nullptr = unused slot).
  void* context{nullptr}; ///< User context passed to task function.
  uint16_t freqN{1};      ///< Frequency numerator (freqN/freqD * fundamental).
  uint16_t freqD{1};      ///< Frequency denominator.
  uint16_t offset{0};     ///< Phase offset in ticks.
  int8_t priority{0};     ///< Priority (-128 to 127, higher = earlier).
  uint8_t taskId{0};      ///< Task identifier for diagnostics.
};

/* ----------------------------- SchedulerLite ----------------------------- */

/**
 * @class SchedulerLite
 * @brief Minimal IScheduler implementation for MCU targets.
 *
 * Provides simple rate-group scheduling without heap allocation.
 * Tasks are registered at boot time and executed sequentially
 * based on their rate and priority configuration.
 *
 * Inherits IComponent implementation from McuComponentBase. Implements
 * IScheduler for scheduler-specific methods (tick, taskCount, etc.).
 *
 * @tparam MaxTasks Maximum number of tasks in the static table. Each
 *         LiteTaskEntry is 12 bytes, so sizeof(tasks_) = MaxTasks * 12.
 *         Default: 32 (384 bytes). For constrained MCUs, use the actual
 *         number of registered tasks + small margin.
 * @tparam Counter Tick counter type. Default: uint64_t. Use uint32_t on
 *         8/16-bit targets where 64-bit arithmetic is expensive. At 100 Hz,
 *         uint32_t overflows after ~497 days.
 *
 * Usage:
 * @code
 * SchedulerLite<> sched(100);  // 32-task, uint64_t (default)
 * SchedulerLite<8, uint32_t> sched(100);  // 8-task, uint32_t (AVR)
 * sched.addTask({myTask, &ctx, 1, 1, 0, 0, 1});  // 100 Hz task
 * sched.addTask({slowTask, &ctx, 1, 10, 0, 0, 2});  // 10 Hz task
 * sched.init();
 * while (running) {
 *   tickSource.waitForNextTick();
 *   sched.tick();
 * }
 * @endcode
 *
 * @note RT-safe: tick() has no allocation, O(n) execution.
 */
template <size_t MaxTasks = DEFAULT_LITE_MAX_TASKS, typename Counter = uint64_t>
class SchedulerLite : public system_core::system_component::mcu::McuComponentBase,
                      public IScheduler {
public:
  /// Compile-time maximum number of tasks.
  static constexpr size_t MAX_TASKS = MaxTasks;

  /**
   * @brief Construct scheduler with specified frequency.
   * @param freqHz Fundamental scheduling frequency in Hz.
   */
  explicit SchedulerLite(uint16_t freqHz = DEFAULT_LITE_FREQ_HZ) noexcept
      : fundamentalFreq_(freqHz) {}

  ~SchedulerLite() override = default;

  // Non-copyable, non-movable
  SchedulerLite(const SchedulerLite&) = delete;
  SchedulerLite& operator=(const SchedulerLite&) = delete;
  SchedulerLite(SchedulerLite&&) = delete;
  SchedulerLite& operator=(SchedulerLite&&) = delete;

  /* ----------------------------- IComponent: Identity ----------------------------- */

  /**
   * @brief Get component type identifier.
   * @return Scheduler component ID (1).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint16_t componentId() const noexcept override {
    return SCHEDULER_LITE_COMPONENT_ID;
  }

  /**
   * @brief Get component name.
   * @return "SchedulerLite".
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const char* componentName() const noexcept override { return "SchedulerLite"; }

  /**
   * @brief Get component type classification.
   * @return ComponentType::CORE.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] system_component::ComponentType componentType() const noexcept override {
    return system_component::ComponentType::CORE;
  }

  /**
   * @brief Get diagnostic label.
   * @return "SCHED_LITE".
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const char* label() const noexcept override { return "SCHED_LITE"; }

  /* ----------------------------- IScheduler ----------------------------- */

  /**
   * @brief Get fundamental frequency.
   * @return Scheduling frequency in Hz.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint16_t fundamentalFreq() const noexcept override { return fundamentalFreq_; }

  /**
   * @brief Execute one scheduler tick.
   *
   * Iterates through all registered tasks and executes those
   * scheduled for the current tick based on their frequency and offset.
   *
   * @note RT-safe: O(n) where n = number of tasks.
   * @note No heap allocation.
   */
  void tick() noexcept override {
    for (size_t i = 0; i < taskCount_; ++i) {
      const auto& task = tasks_[i];
      if (task.fn != nullptr && shouldExecute(task, tickCount_)) {
        task.fn(task.context);
      }
    }
    ++tickCount_;
  }

  /**
   * @brief Get current tick count.
   * @return Number of ticks since init/reset.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint64_t tickCount() const noexcept override {
    return static_cast<uint64_t>(tickCount_);
  }

  /**
   * @brief Get number of registered tasks.
   * @return Task count.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] size_t taskCount() const noexcept override { return taskCount_; }

  /**
   * @brief Get compile-time maximum task capacity.
   * @return MaxTasks template parameter value.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] static constexpr size_t maxTasks() noexcept { return MaxTasks; }

  /* ----------------------------- Task Registration ----------------------------- */

  /**
   * @brief Add a task to the scheduler.
   * @param entry Task configuration.
   * @return true on success, false if table is full.
   * @note NOT RT-safe: Must be called before init() or during reset.
   * @note Does not re-sort; call init() after all tasks are added.
   */
  bool addTask(const LiteTaskEntry& entry) noexcept {
    if (taskCount_ >= MaxTasks) {
      return false;
    }
    if (entry.fn == nullptr) {
      return false;
    }
    tasks_[taskCount_++] = entry;
    return true;
  }

  /**
   * @brief Clear all tasks.
   * @note NOT RT-safe: Must not be called during execution.
   */
  void clearTasks() noexcept {
    taskCount_ = 0;
    for (auto& task : tasks_) {
      task = LiteTaskEntry{};
    }
  }

  /**
   * @brief Get task entry by index.
   * @param idx Task index.
   * @return Pointer to task entry, or nullptr if out of range.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const LiteTaskEntry* task(size_t idx) const noexcept {
    if (idx < taskCount_) {
      return &tasks_[idx];
    }
    return nullptr;
  }

protected:
  /* ----------------------------- Lifecycle Hooks ----------------------------- */

  /**
   * @brief Initialize scheduler (sort tasks by priority).
   * @return 0 on success.
   * @note NOT RT-safe: Sorts task table.
   */
  [[nodiscard]] uint8_t doInit() noexcept override {
    sortTasksByPriority();
    return 0;
  }

  /**
   * @brief Reset scheduler state.
   * @note Clears tick count but preserves task table.
   */
  void doReset() noexcept override { tickCount_ = 0; }

private:
  /**
   * @brief Check if task should execute on this tick.
   * @param task Task entry.
   * @param tick Current tick number.
   * @return true if task should run.
   *
   * Task frequency = (freqN / freqD) * fundamentalFreq
   * Period in ticks = freqD / freqN
   *
   * Examples at 100 Hz fundamental:
   *   freqN=1, freqD=1: runs every tick (100 Hz)
   *   freqN=1, freqD=10: runs every 10 ticks (10 Hz)
   *   freqN=2, freqD=1: runs every tick (200 Hz - but scheduler is 100 Hz, so runs every tick)
   */
  [[nodiscard]] bool shouldExecute(const LiteTaskEntry& task, Counter tick) const noexcept {
    if (task.freqN == 0 || task.freqD == 0) {
      return false;
    }
    // Skip if tick hasn't reached offset yet
    if (tick < task.offset) {
      return false;
    }
    // Period in ticks = freqD / freqN
    // For freqN >= freqD: period <= 1, so run every tick
    const Counter PERIOD = static_cast<Counter>(task.freqD) / static_cast<Counter>(task.freqN);
    if (PERIOD == 0) {
      return true; // freqN > freqD: run every tick
    }
    // Adjust tick by offset
    const Counter ADJUSTED = tick - static_cast<Counter>(task.offset);
    return (ADJUSTED % PERIOD) == 0;
  }

  /**
   * @brief Sort tasks by priority (higher priority first).
   * @note Simple insertion sort - task count is small.
   */
  void sortTasksByPriority() noexcept {
    for (size_t i = 1; i < taskCount_; ++i) {
      auto key = tasks_[i];
      size_t j = i;
      while (j > 0 && tasks_[j - 1].priority < key.priority) {
        tasks_[j] = tasks_[j - 1];
        --j;
      }
      tasks_[j] = key;
    }
  }

  LiteTaskEntry tasks_[MaxTasks]{};
  Counter tickCount_{0};
  uint16_t fundamentalFreq_;
  size_t taskCount_{0};
};

} // namespace mcu
} // namespace scheduler
} // namespace system_core

#endif // APEX_SCHEDULER_MCU_SCHEDULER_LITE_HPP
