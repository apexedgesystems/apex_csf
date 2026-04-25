#ifndef APEX_HAL_STM32_SYSTICK_SOURCE_HPP
#define APEX_HAL_STM32_SYSTICK_SOURCE_HPP
/**
 * @file Stm32SysTickSource.hpp
 * @brief SysTick-based tick source for McuExecutive on Cortex-M.
 *
 * Shares the ARM SysTick timer with the STM32 HAL (HAL_IncTick runs at 1kHz).
 * Prescales the 1kHz SysTick down to the desired executive frequency (e.g.,
 * 100 Hz = every 10th SysTick). No additional hardware required -- SysTick is
 * a core Cortex-M peripheral present on every ARM MCU.
 *
 * Supported families: All Cortex-M (STM32L4, G4, H7, F4, etc.)
 *
 * Usage:
 *  1. Create instance with desired frequency
 *  2. Wire isrCallback() into SysTick_Handler (alongside HAL_IncTick)
 *  3. Pass to McuExecutive constructor
 *  4. McuExecutive::run() calls start()/waitForNextTick()/stop()
 *
 * @code
 * static apex::hal::stm32::Stm32SysTickSource tickSource(100);  // 100 Hz
 *
 * // In vector table linkage
 * extern "C" void SysTick_Handler() {
 *   HAL_IncTick();
 *   apex::hal::stm32::Stm32SysTickSource::isrCallback();
 * }
 *
 * // In main
 * executive::mcu::McuExecutive exec(&tickSource, &scheduler);
 * exec.run();
 * @endcode
 *
 * Frequency constraints:
 *  - Must divide evenly into 1000 (SysTick rate)
 *  - Valid: 1, 2, 4, 5, 8, 10, 20, 25, 40, 50, 100, 125, 200, 250, 500, 1000
 *  - Invalid frequencies are clamped to 1000 Hz
 */

#include "src/system/core/executive/mcu/inc/ITickSource.hpp"

#include <stdint.h>

// STM32 HAL includes (for __WFI on real hardware)
#if defined(STM32L476xx) || defined(STM32L4xx)
#include "stm32l4xx_hal.h"
#elif defined(STM32G4xx) || defined(STM32G474xx)
#include "stm32g4xx_hal.h"
#elif defined(STM32H7xx) || defined(STM32H743xx)
#include "stm32h7xx_hal.h"
#elif defined(STM32F4xx) || defined(STM32F446xx) || defined(STM32F401xC) || defined(STM32F411xE)
#include "stm32f4xx_hal.h"
#else
#ifndef APEX_HAL_STM32_MOCK
#error "STM32 family not defined. Define STM32L476xx, STM32G4xx, etc."
#endif
#endif

namespace apex {
namespace hal {
namespace stm32 {

/* ----------------------------- Stm32SysTickSource ----------------------------- */

/**
 * @class Stm32SysTickSource
 * @brief Tick source driven by ARM SysTick timer (shared with HAL).
 *
 * Prescales the 1kHz HAL SysTick to the desired executive frequency.
 * On real hardware, waitForNextTick() enters WFI (wait-for-interrupt) for
 * power savings between ticks.
 *
 * Memory usage: ~24 bytes.
 */
class Stm32SysTickSource final : public executive::mcu::ITickSource {
public:
  /**
   * @brief Construct with desired tick frequency.
   * @param freqHz Executive tick frequency in Hz (default 100).
   *
   * Frequency must divide evenly into 1000. If it does not, the prescaler
   * is computed via integer division (truncated), resulting in a slightly
   * higher actual frequency.
   *
   * @note NOT RT-safe: Construction only.
   */
  explicit Stm32SysTickSource(uint32_t freqHz = 100) noexcept
      : frequency_(freqHz > 0 ? freqHz : 1),
        prescaler_(frequency_ <= 1000 ? (1000 / frequency_) : 1) {}

  ~Stm32SysTickSource() override = default;

  Stm32SysTickSource(const Stm32SysTickSource&) = delete;
  Stm32SysTickSource& operator=(const Stm32SysTickSource&) = delete;
  Stm32SysTickSource(Stm32SysTickSource&&) = delete;
  Stm32SysTickSource& operator=(Stm32SysTickSource&&) = delete;

  /* ----------------------------- ISR Integration ----------------------------- */

  /**
   * @brief SysTick ISR callback. Call from SysTick_Handler.
   *
   * Must be called at 1kHz (the default HAL SysTick rate). Increments
   * the prescaler counter and sets tickPending when the executive tick
   * is due.
   *
   * Typical usage:
   * @code
   * extern "C" void SysTick_Handler() {
   *   HAL_IncTick();
   *   apex::hal::stm32::Stm32SysTickSource::isrCallback();
   * }
   * @endcode
   *
   * @note RT-safe: O(1), no allocation. Safe to call from ISR.
   */
  static void isrCallback() noexcept {
#ifndef APEX_HAL_STM32_MOCK
    auto* self = instance_;
    if (self == nullptr || !self->running_) {
      return;
    }

    self->prescalerCount_ = self->prescalerCount_ + 1;
    if (self->prescalerCount_ >= self->prescaler_) {
      self->prescalerCount_ = 0;
      self->tickPending_ = true;
    }
#endif
  }

  /* ----------------------------- ITickSource ----------------------------- */

  /**
   * @brief Block until the next executive tick is due.
   *
   * Real mode: Enters WFI (wait-for-interrupt) until the prescaled SysTick
   * fires. Power-efficient -- CPU sleeps between ticks.
   *
   * Mock mode: Increments tick count and returns immediately.
   *
   * @note RT-safe: Completes within one tick period.
   */
  void waitForNextTick() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (!running_) {
      return;
    }
    while (!tickPending_) {
      __WFI();
    }
    tickPending_ = false;
    tickCount_ = tickCount_ + 1;
#else
    if (running_) {
      tickCount_ = tickCount_ + 1;
    }
#endif
  }

  /**
   * @brief Get current tick count.
   * @return Monotonic tick count since last start().
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t currentTick() const noexcept override { return tickCount_; }

  /**
   * @brief Get tick frequency in Hz.
   * @return Configured frequency.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t tickFrequency() const noexcept override { return frequency_; }

  /**
   * @brief Start the tick source.
   *
   * Registers this instance for ISR callbacks and resets counters.
   * SysTick is already running (configured by HAL_Init), so no hardware
   * reconfiguration is needed.
   *
   * @note NOT RT-safe: Modifies static instance pointer.
   */
  void start() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    instance_ = this;
    prescalerCount_ = 0;
    tickPending_ = false;
#endif
    tickCount_ = 0;
    running_ = true;
  }

  /**
   * @brief Stop the tick source.
   *
   * Unregisters from ISR callbacks. SysTick continues running for
   * HAL_IncTick -- only the executive tick generation stops.
   *
   * @note NOT RT-safe: Modifies static instance pointer.
   */
  void stop() noexcept override {
    running_ = false;
#ifndef APEX_HAL_STM32_MOCK
    instance_ = nullptr;
#endif
  }

  /**
   * @brief Check if tick source is running.
   * @return true if started, false if stopped.
   * @note RT-safe.
   */
  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

  /* ----------------------------- Query ----------------------------- */

  /**
   * @brief Get the prescaler value.
   * @return Number of SysTick interrupts per executive tick.
   *
   * For 100 Hz executive with 1kHz SysTick, prescaler = 10.
   *
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t prescaler() const noexcept { return prescaler_; }

private:
  uint32_t frequency_; ///< Executive tick frequency (Hz).
  uint32_t prescaler_; ///< SysTick interrupts per tick.

#ifndef APEX_HAL_STM32_MOCK
  volatile uint32_t tickCount_ = 0;      ///< Monotonic tick counter.
  volatile bool tickPending_ = false;    ///< ISR-to-main signaling flag.
  volatile uint32_t prescalerCount_ = 0; ///< ISR prescaler counter.
  bool running_ = false;                 ///< Running state.

  static Stm32SysTickSource* volatile instance_; ///< Active instance (one per system).
#else
  uint32_t tickCount_ = 0; ///< Monotonic tick counter (mock).
  bool running_ = false;   ///< Running state (mock).
#endif
};

#ifndef APEX_HAL_STM32_MOCK
/// Static instance pointer definition (one per translation unit that includes this).
inline Stm32SysTickSource* volatile Stm32SysTickSource::instance_ = nullptr;
#endif

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_SYSTICK_SOURCE_HPP
