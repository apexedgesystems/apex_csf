#ifndef APEX_HAL_STM32_TIMER_TICK_SOURCE_HPP
#define APEX_HAL_STM32_TIMER_TICK_SOURCE_HPP
/**
 * @file Stm32TimerTickSource.hpp
 * @brief Hardware timer-based tick source for McuExecutive on STM32.
 *
 * Uses a general-purpose or basic timer (TIM6, TIM7, TIM2-TIM5) to generate
 * periodic interrupts at the exact desired executive frequency. Unlike
 * SysTickSource, this does NOT share the timer with HAL_IncTick -- it uses a
 * dedicated timer peripheral.
 *
 * Supported families: STM32L4, G4 (direct register access, no HAL_TIM needed)
 *
 * Timer selection:
 *  - TIM6, TIM7 (basic timers) -- recommended, minimal peripheral
 *  - TIM2-TIM5 (general purpose) -- work but overkill for tick generation
 *
 * Uses direct register access (PSC, ARR, DIER, CR1, SR) rather than HAL_TIM
 * functions. Timer registers are identical across STM32 families, and this
 * avoids requiring HAL_TIM_MODULE_ENABLED.
 *
 * Usage:
 *  1. Create instance with timer peripheral and frequency
 *  2. Wire isrCallback() into the timer's IRQ handler
 *  3. Pass to McuExecutive constructor
 *  4. McuExecutive::run() calls start()/waitForNextTick()/stop()
 *
 * @code
 * static apex::hal::stm32::Stm32TimerTickSource tickSource(TIM6, 100);
 *
 * // In vector table linkage
 * extern "C" void TIM6_DAC_IRQHandler() {
 *   apex::hal::stm32::Stm32TimerTickSource::isrCallback();
 * }
 *
 * // In main
 * executive::mcu::McuExecutive exec(&tickSource, &scheduler);
 * exec.run();
 * @endcode
 *
 * Frequency range: 1 Hz to 10000 Hz (limited by prescaler/ARR resolution).
 * Actual frequency depends on SystemCoreClock -- typical accuracy is within
 * 0.01% for standard clock configurations.
 */

#include "src/system/core/executive/mcu/inc/ITickSource.hpp"

#include <stdint.h>

// STM32 HAL includes (for clock enable macros, NVIC, register definitions)
#if defined(STM32L476xx) || defined(STM32L4xx)
#include "stm32l4xx_hal.h"
#elif defined(STM32G4xx) || defined(STM32G474xx)
#include "stm32g4xx_hal.h"
#elif defined(STM32H7xx) || defined(STM32H743xx)
#include "stm32h7xx_hal.h"
#else
#ifndef APEX_HAL_STM32_MOCK
#error "STM32 family not defined. Define STM32L476xx, STM32G4xx, etc."
#endif
#endif

namespace apex {
namespace hal {
namespace stm32 {

/* ----------------------------- Stm32TimerTickSource ----------------------------- */

/**
 * @class Stm32TimerTickSource
 * @brief Tick source driven by a dedicated STM32 hardware timer.
 *
 * Configures a timer peripheral to fire periodic update interrupts at the
 * desired frequency. On real hardware, waitForNextTick() enters WFI between
 * ticks for power savings.
 *
 * Memory usage: ~20 bytes.
 */
class Stm32TimerTickSource final : public executive::mcu::ITickSource {
public:
  /**
   * @brief Construct with timer peripheral and frequency.
   * @param tim Timer peripheral pointer (TIM6, TIM7, etc.). nullptr in mock.
   * @param freqHz Desired tick frequency in Hz (default 100).
   *
   * Does NOT configure hardware -- deferred to start().
   *
   * @note NOT RT-safe: Construction only.
   */
#ifndef APEX_HAL_STM32_MOCK
  explicit Stm32TimerTickSource(TIM_TypeDef* tim, uint32_t freqHz = 100) noexcept
      : tim_(tim), frequency_(freqHz > 0 ? freqHz : 1) {}
#else
  explicit Stm32TimerTickSource(void* /* tim */ = nullptr, uint32_t freqHz = 100) noexcept
      : frequency_(freqHz > 0 ? freqHz : 1) {}
#endif

  ~Stm32TimerTickSource() override {
    if (running_) {
      stop();
    }
  }

  Stm32TimerTickSource(const Stm32TimerTickSource&) = delete;
  Stm32TimerTickSource& operator=(const Stm32TimerTickSource&) = delete;
  Stm32TimerTickSource(Stm32TimerTickSource&&) = delete;
  Stm32TimerTickSource& operator=(Stm32TimerTickSource&&) = delete;

  /* ----------------------------- ISR Integration ----------------------------- */

  /**
   * @brief Timer update ISR callback. Call from TIMx_IRQHandler.
   *
   * Clears the timer update interrupt flag and signals the next tick.
   *
   * Typical usage:
   * @code
   * extern "C" void TIM6_DAC_IRQHandler() {
   *   apex::hal::stm32::Stm32TimerTickSource::isrCallback();
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

    // Clear update interrupt flag
    self->tim_->SR = self->tim_->SR & ~TIM_SR_UIF;
    self->tickPending_ = true;
#endif
  }

  /* ----------------------------- ITickSource ----------------------------- */

  /**
   * @brief Block until the next tick.
   *
   * Real mode: Enters WFI until the timer fires.
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
   * Configures and starts the hardware timer:
   *  1. Enable timer peripheral clock
   *  2. Set prescaler and auto-reload for desired frequency
   *  3. Enable update interrupt
   *  4. Configure and enable NVIC
   *  5. Start timer
   *
   * @note NOT RT-safe: Configures hardware, enables interrupts.
   */
  void start() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    instance_ = this;
    tickPending_ = false;

    enableClock();

    // Compute prescaler and period
    // Strategy: set PSC for a 10 kHz base, then ARR for desired frequency.
    // For frequencies above 10 kHz, use PSC=0 and compute ARR directly.
    const uint32_t TIMER_CLK = timerClockHz();

    uint32_t psc;
    uint32_t arr;

    if (frequency_ <= 10000) {
      psc = (TIMER_CLK / 10000) - 1;
      arr = (10000 / frequency_) - 1;
    } else {
      psc = 0;
      arr = (TIMER_CLK / frequency_) - 1;
    }

    // Reset timer
    tim_->CR1 = 0;
    tim_->SR = 0;
    tim_->CNT = 0;
    tim_->PSC = static_cast<uint16_t>(psc);
    tim_->ARR = static_cast<uint16_t>(arr);

    // Generate update event to load prescaler immediately
    tim_->EGR = TIM_EGR_UG;
    tim_->SR = 0; // Clear UIF set by EGR

    // Enable update interrupt
    tim_->DIER = tim_->DIER | TIM_DIER_UIE;

    // Configure NVIC
    const IRQn_Type IRQ = irqn();
    HAL_NVIC_SetPriority(IRQ, 0, 0);
    HAL_NVIC_EnableIRQ(IRQ);

    // Start timer
    tim_->CR1 = tim_->CR1 | TIM_CR1_CEN;
#endif

    tickCount_ = 0;
    running_ = true;
  }

  /**
   * @brief Stop the tick source.
   *
   * Disables the timer, NVIC, and peripheral clock.
   *
   * @note NOT RT-safe: Modifies hardware configuration.
   */
  void stop() noexcept override {
    running_ = false;

#ifndef APEX_HAL_STM32_MOCK
    if (tim_ != nullptr) {
      // Stop timer
      tim_->CR1 = tim_->CR1 & ~TIM_CR1_CEN;
      tim_->DIER = tim_->DIER & ~TIM_DIER_UIE;

      // Disable NVIC
      HAL_NVIC_DisableIRQ(irqn());

      // Disable clock
      disableClock();
    }

    instance_ = nullptr;
#endif
  }

  /**
   * @brief Check if tick source is running.
   * @return true if started, false if stopped.
   * @note RT-safe.
   */
  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

private:
  /* ----------------------------- Hardware Helpers ----------------------------- */

#ifndef APEX_HAL_STM32_MOCK
  /**
   * @brief Enable timer peripheral clock.
   * @note NOT RT-safe.
   */
  void enableClock() const noexcept {
#if defined(TIM6)
    if (tim_ == TIM6) {
      __HAL_RCC_TIM6_CLK_ENABLE();
      return;
    }
#endif
#if defined(TIM7)
    if (tim_ == TIM7) {
      __HAL_RCC_TIM7_CLK_ENABLE();
      return;
    }
#endif
#if defined(TIM2)
    if (tim_ == TIM2) {
      __HAL_RCC_TIM2_CLK_ENABLE();
      return;
    }
#endif
#if defined(TIM3)
    if (tim_ == TIM3) {
      __HAL_RCC_TIM3_CLK_ENABLE();
      return;
    }
#endif
#if defined(TIM4)
    if (tim_ == TIM4) {
      __HAL_RCC_TIM4_CLK_ENABLE();
      return;
    }
#endif
#if defined(TIM5)
    if (tim_ == TIM5) {
      __HAL_RCC_TIM5_CLK_ENABLE();
      return;
    }
#endif
  }

  /**
   * @brief Disable timer peripheral clock.
   * @note NOT RT-safe.
   */
  void disableClock() const noexcept {
#if defined(TIM6)
    if (tim_ == TIM6) {
      __HAL_RCC_TIM6_CLK_DISABLE();
      return;
    }
#endif
#if defined(TIM7)
    if (tim_ == TIM7) {
      __HAL_RCC_TIM7_CLK_DISABLE();
      return;
    }
#endif
#if defined(TIM2)
    if (tim_ == TIM2) {
      __HAL_RCC_TIM2_CLK_DISABLE();
      return;
    }
#endif
#if defined(TIM3)
    if (tim_ == TIM3) {
      __HAL_RCC_TIM3_CLK_DISABLE();
      return;
    }
#endif
#if defined(TIM4)
    if (tim_ == TIM4) {
      __HAL_RCC_TIM4_CLK_DISABLE();
      return;
    }
#endif
#if defined(TIM5)
    if (tim_ == TIM5) {
      __HAL_RCC_TIM5_CLK_DISABLE();
      return;
    }
#endif
  }

  /**
   * @brief Get NVIC IRQ number for this timer.
   * @return IRQn_Type for NVIC configuration.
   * @note RT-safe.
   */
  [[nodiscard]] IRQn_Type irqn() const noexcept {
#if defined(TIM6)
    if (tim_ == TIM6) {
      return TIM6_DAC_IRQn;
    }
#endif
#if defined(TIM7)
    if (tim_ == TIM7) {
      return TIM7_IRQn;
    }
#endif
#if defined(TIM2)
    if (tim_ == TIM2) {
      return TIM2_IRQn;
    }
#endif
#if defined(TIM3)
    if (tim_ == TIM3) {
      return TIM3_IRQn;
    }
#endif
#if defined(TIM4)
    if (tim_ == TIM4) {
      return TIM4_IRQn;
    }
#endif
#if defined(TIM5)
    if (tim_ == TIM5) {
      return TIM5_IRQn;
    }
#endif
    return UsageFault_IRQn;
  }

  /**
   * @brief Get timer input clock frequency.
   * @return Clock frequency in Hz.
   *
   * On STM32L4 with default clock configuration, all timers run at
   * SystemCoreClock (APB prescalers = 1). Override if APB prescalers differ.
   *
   * @note RT-safe.
   */
  [[nodiscard]] static uint32_t timerClockHz() noexcept { return SystemCoreClock; }
#endif // !APEX_HAL_STM32_MOCK

  /* ----------------------------- Members ----------------------------- */

#ifndef APEX_HAL_STM32_MOCK
  TIM_TypeDef* tim_;
#endif

  uint32_t frequency_; ///< Desired tick frequency (Hz).

#ifndef APEX_HAL_STM32_MOCK
  volatile uint32_t tickCount_ = 0;   ///< Monotonic tick counter.
  volatile bool tickPending_ = false; ///< ISR-to-main signaling flag.
  bool running_ = false;              ///< Running state.

  static Stm32TimerTickSource* volatile instance_; ///< Active instance.
#else
  uint32_t tickCount_ = 0; ///< Monotonic tick counter (mock).
  bool running_ = false;   ///< Running state (mock).
#endif
};

#ifndef APEX_HAL_STM32_MOCK
/// Static instance pointer definition.
inline Stm32TimerTickSource* volatile Stm32TimerTickSource::instance_ = nullptr;
#endif

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_TIMER_TICK_SOURCE_HPP
