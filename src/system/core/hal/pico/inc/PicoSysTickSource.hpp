#ifndef APEX_HAL_PICO_SYSTICK_SOURCE_HPP
#define APEX_HAL_PICO_SYSTICK_SOURCE_HPP
/**
 * @file PicoSysTickSource.hpp
 * @brief SysTick-based tick source for McuExecutive on RP2040.
 *
 * Configures the ARM SysTick timer directly via CMSIS registers (no vendor
 * HAL dependency). Prescales a 1 kHz base tick to the desired executive
 * frequency (e.g., 100 Hz = every 10th SysTick).
 *
 * The RP2040 runs at 125 MHz by default (Pico SDK default, not the
 * theoretical 133 MHz maximum). SysTick reload = 125000000 / 1000 = 125000.
 *
 * Usage:
 *  1. Create instance with desired frequency
 *  2. Wire isrCallback() into isr_systick
 *  3. Pass to McuExecutive constructor
 *
 * @code
 * static apex::hal::pico::PicoSysTickSource tickSource(100);
 *
 * extern "C" void isr_systick() {
 *   apex::hal::pico::PicoSysTickSource::isrCallback();
 * }
 *
 * executive::mcu::McuExecutive exec(&tickSource, 100);
 * exec.run();
 * @endcode
 */

#include "src/system/core/executive/mcu/inc/ITickSource.hpp"

#ifndef APEX_HAL_PICO_MOCK
#include "hardware/clocks.h"
#endif

#include <stdint.h>

#ifndef APEX_HAL_PICO_MOCK
// CMSIS SysTick registers (Cortex-M0+ core peripheral)
// These are standard ARM addresses, available on all Cortex-M cores.
#ifndef SysTick
struct SysTickRegs {
  volatile uint32_t CTRL;
  volatile uint32_t LOAD;
  volatile uint32_t VAL;
  volatile uint32_t CALIB;
};
#define SysTick ((SysTickRegs*)0xE000E010UL)
#endif

#ifndef SCB
struct ScbRegs {
  volatile uint32_t CPUID;
  volatile uint32_t ICSR;
  volatile uint32_t reserved0;
  volatile uint32_t AIRCR;
  volatile uint32_t SCR;
};
#define SCB ((ScbRegs*)0xE000ED00UL)
#endif

/// SysTick CTRL register bits.
static constexpr uint32_t SYSTICK_CTRL_ENABLE = (1U << 0);
static constexpr uint32_t SYSTICK_CTRL_TICKINT = (1U << 1);
static constexpr uint32_t SYSTICK_CTRL_CLKSOURCE = (1U << 2);

/// SCR register bits.
static constexpr uint32_t SCR_SLEEPONEXIT = (1U << 1);
#endif // APEX_HAL_PICO_MOCK

namespace apex {
namespace hal {
namespace pico {

/* ----------------------------- PicoSysTickSource ----------------------------- */

/**
 * @class PicoSysTickSource
 * @brief Tick source driven by ARM SysTick timer on RP2040.
 *
 * Prescales a 1 kHz SysTick to the desired executive frequency.
 * Uses WFI (wait-for-interrupt) between ticks for power savings.
 *
 * Mock mode: waitForNextTick() increments tick count and returns
 * immediately. No hardware access.
 *
 * Memory usage: ~32 bytes.
 */
class PicoSysTickSource final : public executive::mcu::ITickSource {
public:
  /**
   * @brief Construct with desired tick frequency.
   * @param freqHz Executive tick frequency in Hz (default 100).
   * @note NOT RT-safe: Construction only.
   */
  explicit PicoSysTickSource(uint32_t freqHz = 100) noexcept
      : frequency_(freqHz > 0 ? freqHz : 1),
        prescaler_(frequency_ <= 1000 ? (1000 / frequency_) : 1) {}

  ~PicoSysTickSource() override = default;

  PicoSysTickSource(const PicoSysTickSource&) = delete;
  PicoSysTickSource& operator=(const PicoSysTickSource&) = delete;
  PicoSysTickSource(PicoSysTickSource&&) = delete;
  PicoSysTickSource& operator=(PicoSysTickSource&&) = delete;

  /* ----------------------------- ISR Integration ----------------------------- */

  /**
   * @brief SysTick ISR callback.
   *
   * Must be called at 1 kHz from the SysTick handler. Increments the
   * prescaler counter and sets tickPending when the executive tick is due.
   *
   * @code
   * extern "C" void isr_systick() {
   *   apex::hal::pico::PicoSysTickSource::isrCallback();
   * }
   * @endcode
   *
   * @note RT-safe: O(1), no allocation. Safe to call from ISR.
   */
  static void isrCallback() noexcept {
#ifndef APEX_HAL_PICO_MOCK
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
#ifndef APEX_HAL_PICO_MOCK
    if (!running_) {
      return;
    }
    while (!tickPending_) {
      __asm volatile("wfi");
    }
    tickPending_ = false;
    tickCount_ = tickCount_ + 1;
#else
    if (running_) {
      ++tickCount_;
    }
#endif
  }

  [[nodiscard]] uint32_t currentTick() const noexcept override { return tickCount_; }

  [[nodiscard]] uint32_t tickFrequency() const noexcept override { return frequency_; }

  void start() noexcept override {
#ifndef APEX_HAL_PICO_MOCK
    instance_ = this;
    prescalerCount_ = 0;
    tickPending_ = false;
#endif
    tickCount_ = 0;
    running_ = true;

#ifndef APEX_HAL_PICO_MOCK
    // Configure SysTick for 1 kHz (system clock / 1000)
    const uint32_t SYS_FREQ = clock_get_hz(clk_sys);
    const uint32_t RELOAD = (SYS_FREQ / 1000) - 1;

    SysTick->CTRL = 0;
    SysTick->LOAD = RELOAD;
    SysTick->VAL = 0;
    SysTick->CTRL = SYSTICK_CTRL_ENABLE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_CLKSOURCE;
#endif
  }

  void stop() noexcept override {
    running_ = false;
#ifndef APEX_HAL_PICO_MOCK
    SysTick->CTRL = 0;
    instance_ = nullptr;
#endif
  }

  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

  /* ----------------------------- Query ----------------------------- */

  [[nodiscard]] uint32_t prescaler() const noexcept { return prescaler_; }

private:
  uint32_t frequency_;
  uint32_t prescaler_;

#ifndef APEX_HAL_PICO_MOCK
  volatile uint32_t tickCount_ = 0;
  volatile bool tickPending_ = false;
  volatile uint32_t prescalerCount_ = 0;
  bool running_ = false;

  static PicoSysTickSource* volatile instance_;
#else
  uint32_t tickCount_ = 0;
  bool running_ = false;
#endif
};

#ifndef APEX_HAL_PICO_MOCK
inline PicoSysTickSource* volatile PicoSysTickSource::instance_ = nullptr;
#endif

} // namespace pico
} // namespace hal
} // namespace apex

#endif // APEX_HAL_PICO_SYSTICK_SOURCE_HPP
