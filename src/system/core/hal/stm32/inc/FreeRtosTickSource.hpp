#ifndef APEX_HAL_STM32_FREERTOS_TICK_SOURCE_HPP
#define APEX_HAL_STM32_FREERTOS_TICK_SOURCE_HPP
/**
 * @file FreeRtosTickSource.hpp
 * @brief FreeRTOS-based tick source for McuExecutive on Cortex-M.
 *
 * Uses vTaskDelayUntil() to generate periodic ticks at the desired executive
 * frequency. FreeRTOS runs at configTICK_RATE_HZ (typically 1 kHz); this class
 * prescales to the executive rate (e.g., 100 Hz = 10 ms period).
 *
 * Requirements:
 *   - FreeRTOS kernel running (vTaskStartScheduler called)
 *   - INCLUDE_vTaskDelayUntil = 1 in FreeRTOSConfig.h
 *   - Must be called from a FreeRTOS task context (not from main before scheduler)
 *
 * Usage:
 * @code
 * static apex::hal::stm32::FreeRtosTickSource tickSource(100);  // 100 Hz
 *
 * void executiveTask(void* param) {
 *   executive::mcu::McuExecutive exec(&tickSource, 100);
 *   exec.addTask({...});
 *   exec.init();
 *   exec.run();  // Blocks, uses vTaskDelayUntil internally
 * }
 * @endcode
 *
 * @note RT-safe: All methods are O(1) with bounded execution time.
 */

#include "src/system/core/executive/mcu/inc/ITickSource.hpp"

#include <stdint.h>

#ifndef APEX_HAL_STM32_MOCK
#include "FreeRTOS.h"
#include "task.h"
#else
// Mock FreeRTOS types for host-side testing.
using TickType_t = uint32_t;
#endif

namespace apex {
namespace hal {
namespace stm32 {

/* ----------------------------- FreeRtosTickSource ----------------------------- */

/**
 * @class FreeRtosTickSource
 * @brief Tick source driven by FreeRTOS vTaskDelayUntil().
 *
 * Generates executive ticks by sleeping the calling FreeRTOS task for the
 * appropriate number of FreeRTOS ticks. The executive thread yields to
 * lower-priority FreeRTOS tasks during the delay.
 *
 * Memory usage: ~20 bytes.
 */
class FreeRtosTickSource final : public executive::mcu::ITickSource {
public:
  /**
   * @brief Construct with desired tick frequency.
   * @param freqHz Executive tick frequency in Hz (default 100).
   *
   * The period is computed as configTICK_RATE_HZ / freqHz FreeRTOS ticks.
   * For 100 Hz with 1 kHz FreeRTOS tick: period = 10 ticks = 10 ms.
   *
   * @note NOT RT-safe: Construction only.
   */
  explicit FreeRtosTickSource(uint32_t freqHz = 100) noexcept
      : frequency_(freqHz > 0 ? freqHz : 1),
#ifndef APEX_HAL_STM32_MOCK
        periodTicks_(frequency_ <= configTICK_RATE_HZ ? (configTICK_RATE_HZ / frequency_) : 1) {
  }
#else
        periodTicks_(frequency_ <= 1000 ? (1000 / frequency_) : 1) {
  }
#endif

  ~FreeRtosTickSource() override = default;

  FreeRtosTickSource(const FreeRtosTickSource&) = delete;
  FreeRtosTickSource& operator=(const FreeRtosTickSource&) = delete;
  FreeRtosTickSource(FreeRtosTickSource&&) = delete;
  FreeRtosTickSource& operator=(FreeRtosTickSource&&) = delete;

  /* ----------------------------- ITickSource ----------------------------- */

  /**
   * @brief Block until the next executive tick is due.
   *
   * Calls vTaskDelayUntil() which yields the current FreeRTOS task until
   * the absolute wake time. This provides jitter-free periodic execution
   * and allows lower-priority tasks to run during idle time.
   *
   * @note RT-safe: Completes within one tick period.
   */
  void waitForNextTick() noexcept override {
    if (!running_) {
      return;
    }
#ifndef APEX_HAL_STM32_MOCK
    vTaskDelayUntil(&lastWakeTime_, periodTicks_);
#endif
    ++tickCount_;
  }

  /**
   * @brief Get current tick count.
   * @return Monotonic tick count since last start().
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t currentTick() const noexcept override { return tickCount_; }

  /**
   * @brief Get tick frequency in Hz.
   * @return Configured executive frequency.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t tickFrequency() const noexcept override { return frequency_; }

  /**
   * @brief Start the tick source.
   *
   * Records the current FreeRTOS tick as the baseline for vTaskDelayUntil().
   * Must be called from a FreeRTOS task context.
   *
   * @note NOT RT-safe: Initialization.
   */
  void start() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    lastWakeTime_ = xTaskGetTickCount();
#else
    lastWakeTime_ = 0;
#endif
    tickCount_ = 0;
    running_ = true;
  }

  /**
   * @brief Stop the tick source.
   * @note NOT RT-safe: Teardown.
   */
  void stop() noexcept override { running_ = false; }

  /**
   * @brief Check if tick source is running.
   * @return true if started, false if stopped.
   * @note RT-safe.
   */
  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

  /* ----------------------------- Query ----------------------------- */

  /**
   * @brief Get the period in FreeRTOS ticks.
   * @return FreeRTOS ticks per executive tick.
   *
   * For 100 Hz executive with 1 kHz FreeRTOS: period = 10.
   *
   * @note RT-safe.
   */
  [[nodiscard]] TickType_t periodTicks() const noexcept { return periodTicks_; }

private:
  uint32_t frequency_;         ///< Executive tick frequency (Hz).
  TickType_t periodTicks_;     ///< FreeRTOS ticks per executive tick.
  TickType_t lastWakeTime_{0}; ///< Absolute wake time for vTaskDelayUntil.
  uint32_t tickCount_{0};      ///< Monotonic tick counter.
  bool running_{false};        ///< Running state.
};

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_FREERTOS_TICK_SOURCE_HPP
