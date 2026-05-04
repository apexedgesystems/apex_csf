#ifndef APEX_HAL_STM32_PPS_HPP
#define APEX_HAL_STM32_PPS_HPP
/**
 * @file Stm32Pps.hpp
 * @brief STM32 PPS edge capture using EXTI + DWT->CYCCNT.
 *
 * Captures the local cycle counter at the moment of an external 1PPS
 * edge using EXTI on a configurable GPIO pin. The DWT (Data Watchpoint
 * and Trace) cycle counter is read inside the ISR — it ticks at the
 * core clock frequency, so the latched value can be converted to
 * nanoseconds with a single multiply: `ns = cycles * (1e9 / coreFreqHz)`.
 *
 * Supported families: any Cortex-M3/M4/M7/M33 with DWT (essentially all
 * STM32 except Cortex-M0). DWT must be enabled before init().
 *
 * Wrap behaviour:
 *  DWT->CYCCNT is 32 bits. At 80 MHz it wraps every ~54 seconds; at
 *  200 MHz every ~21 seconds. Consumers MUST poll readCapture() at a
 *  cadence faster than the wrap interval to avoid losing precision. At
 *  1 Hz PPS and a typical 100 Hz scheduler frame, this is comfortable.
 *  The implementation does NOT extend the counter to 64 bits in
 *  software — that is left to the consumer if greater dynamic range is
 *  ever needed.
 *
 * Timestamp domain:
 *  The returned timestampNs is "DWT cycles since DWT was enabled,
 *  converted to nanoseconds." On STM32 this is the conventional
 *  steady-clock-equivalent monotonic source (it is what the McuExecutive
 *  tick source aligns to). TimeServer's correlation math depends on the
 *  IPps timestamp being in the same domain as whatever the system's
 *  steady clock returns.
 *
 * Usage:
 * @code
 *   // Board pins (in a board-specific header)
 *   constexpr GPIO_TypeDef* PPS_PORT = GPIOA;
 *   constexpr uint16_t      PPS_PIN  = GPIO_PIN_0;
 *   constexpr IRQn_Type     PPS_IRQ  = EXTI0_IRQn;
 *
 *   static apex::hal::stm32::Stm32Pps pps(PPS_PORT, PPS_PIN, PPS_IRQ,
 *       apex::hal::stm32::Stm32PpsOptions{.coreFreqHz = 80'000'000});
 *
 *   // Vector table linkage
 *   extern "C" void EXTI0_IRQHandler() { pps.irqHandler(); }
 *
 *   // In init
 *   apex::hal::PpsConfig cfg;
 *   cfg.edge = apex::hal::PpsEdge::RISING;
 *   pps.init(cfg);
 * @endcode
 *
 * Hardware verification only:
 *  This header has no host-side unit tests. The IPps contract is
 *  exercised by MockPps for logical correctness; the STM32-specific
 *  hardware path (EXTI, NVIC, DWT) requires real silicon.
 */

#include "src/system/core/hal/base/IPps.hpp"

#include <atomic>
#include <stdint.h>

#if defined(STM32L476xx) || defined(STM32L4xx)
#include "stm32l4xx_hal.h"
#elif defined(STM32G4xx) || defined(STM32G474xx)
#include "stm32g4xx_hal.h"
#elif defined(STM32H7xx) || defined(STM32H743xx)
#include "stm32h7xx_hal.h"
#elif defined(STM32F4xx) || defined(STM32F446xx) || defined(STM32F401xC) ||                       \
    defined(STM32F411xE)
#include "stm32f4xx_hal.h"
#else
// Allow compilation on host for portability checks (the code is gated
// behind APEX_HAL_STM32_MOCK on platforms without the STM32 HAL).
#ifndef APEX_HAL_STM32_MOCK
#error "STM32 family not defined. Define STM32L476xx, STM32G4xx, etc."
#endif
#endif

namespace apex {
namespace hal {
namespace stm32 {

/* ----------------------------- Stm32PpsOptions ----------------------------- */

/**
 * @brief Platform-specific options for Stm32Pps initialization.
 */
struct Stm32PpsOptions {
  /// Core clock frequency in Hz. Used to convert DWT cycles to nanoseconds.
  uint32_t coreFreqHz = 80'000'000;
  /// NVIC preemption priority (0 = highest).
  uint8_t nvicPreemptPriority = 0;
  /// NVIC sub-priority.
  uint8_t nvicSubPriority = 0;
};

/* ----------------------------- Stm32Pps ----------------------------- */

/**
 * @class Stm32Pps
 * @brief IPps backed by EXTI + DWT->CYCCNT on STM32.
 *
 * Single-instance per EXTI line. The vector table dispatches the IRQ to
 * irqHandler(); irqHandler() latches DWT->CYCCNT and bumps the pulse
 * count atomically (single 32-bit register read, single increment, on a
 * single ISR — no concurrency hazard).
 *
 * Thread / ISR safety:
 *  - irqHandler() runs in interrupt context. Touches only volatile
 *    members, no heap, no HAL calls beyond clearing the EXTI pending
 *    bit.
 *  - readCapture() runs in thread context. Atomically consumes the
 *    latched value via a plain volatile load (single-word, naturally
 *    aligned, no torn reads on Cortex-M).
 */
class Stm32Pps : public IPps {
public:
  /**
   * @brief Construct binding to a specific EXTI line.
   * @param port GPIO port (e.g. GPIOA).
   * @param pin GPIO pin mask (e.g. GPIO_PIN_0).
   * @param irqn EXTI IRQ number for the chosen line (e.g. EXTI0_IRQn).
   * @param opts Platform options (core freq, NVIC priority).
   */
#ifndef APEX_HAL_STM32_MOCK
  Stm32Pps(GPIO_TypeDef* port, uint16_t pin, IRQn_Type irqn,
           const Stm32PpsOptions& opts = {}) noexcept
      : port_(port), pin_(pin), irqn_(irqn), opts_(opts) {}
#else
  Stm32Pps(void* /*port*/, uint16_t /*pin*/, int /*irqn*/,
           const Stm32PpsOptions& opts = {}) noexcept
      : opts_(opts) {}
#endif

  ~Stm32Pps() override = default;
  Stm32Pps(const Stm32Pps&) = delete;
  Stm32Pps& operator=(const Stm32Pps&) = delete;

  /* ----------------------------- IPps overrides ----------------------------- */

  [[nodiscard]] PpsStatus init(const PpsConfig& config) noexcept override {
    if (initialized_) {
      return PpsStatus::OK;
    }
    config_ = config;

#ifndef APEX_HAL_STM32_MOCK
    // Enable DWT cycle counter (idempotent if already enabled).
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // Configure GPIO as input with pull-down for clean PPS edges.
    GPIO_InitTypeDef g{};
    g.Pin = pin_;
    g.Mode = (config.edge == PpsEdge::RISING) ? GPIO_MODE_IT_RISING : GPIO_MODE_IT_FALLING;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(port_, &g);

    // Enable EXTI interrupt at the configured priority.
    HAL_NVIC_SetPriority(irqn_, opts_.nvicPreemptPriority, opts_.nvicSubPriority);
    HAL_NVIC_EnableIRQ(irqn_);
#endif

    latchedCycles_.store(0, std::memory_order_relaxed);
    pulseCount_.store(0, std::memory_order_relaxed);
    newEdge_.store(false, std::memory_order_relaxed);
    stats_.reset();
    initialized_ = true;
    return PpsStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (initialized_) {
      HAL_NVIC_DisableIRQ(irqn_);
      HAL_GPIO_DeInit(port_, pin_);
    }
#endif
    initialized_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  [[nodiscard]] PpsStatus readCapture(int64_t& timestampNs) noexcept override {
    if (!initialized_) {
      return PpsStatus::ERROR_NOT_INIT;
    }
    // Acquire-load synchronizes with the ISR's release-store of newEdge_,
    // ensuring that latchedCycles_ is visible by the time we read it.
    if (!newEdge_.exchange(false, std::memory_order_acquire)) {
      return PpsStatus::NO_NEW_EDGE;
    }
    const uint32_t CYCLES = latchedCycles_.load(std::memory_order_relaxed);

    // cycles * 1e9 fits in uint64_t (max ~4.29e18, well under 1.8e19).
    timestampNs = static_cast<int64_t>(
        (static_cast<uint64_t>(CYCLES) * 1'000'000'000ULL) / opts_.coreFreqHz);

    ++stats_.captureCount;
    return PpsStatus::OK;
  }

  [[nodiscard]] uint32_t pulseCount() const noexcept override {
    return pulseCount_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] const PpsStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

  /* ----------------------------- ISR entry point ----------------------------- */

  /**
   * @brief Vector-table entry. Wire into the appropriate EXTI handler.
   * @note RT-safe in ISR context: single register read, single
   *       increment, single flag set. No heap, no HAL calls beyond
   *       clearing the EXTI pending bit.
   */
  void irqHandler() noexcept {
#ifndef APEX_HAL_STM32_MOCK
    if (__HAL_GPIO_EXTI_GET_IT(pin_) == 0) {
      return; // Spurious -- EXTI pending bit not set for our pin.
    }
    __HAL_GPIO_EXTI_CLEAR_IT(pin_);
    latchedCycles_.store(DWT->CYCCNT, std::memory_order_relaxed);
    pulseCount_.fetch_add(1, std::memory_order_relaxed);
    // Release-store synchronizes with readCapture's acquire-load of newEdge_.
    newEdge_.store(true, std::memory_order_release);
#endif
  }

  /* ----------------------------- Test seam (mock builds only) ----------------------------- */

#ifdef APEX_HAL_STM32_MOCK
  /**
   * @brief Simulate an ISR firing with a chosen DWT value (mock builds).
   * @param dwtCycles Value to record as if DWT->CYCCNT had been read in ISR.
   */
  void mockEdge(uint32_t dwtCycles) noexcept {
    latchedCycles_.store(dwtCycles, std::memory_order_relaxed);
    pulseCount_.fetch_add(1, std::memory_order_relaxed);
    newEdge_.store(true, std::memory_order_release);
  }
#endif

private:
#ifndef APEX_HAL_STM32_MOCK
  GPIO_TypeDef* port_ = nullptr;
  uint16_t pin_ = 0;
  IRQn_Type irqn_{};
#endif
  Stm32PpsOptions opts_{};
  PpsConfig config_{};
  PpsStats stats_{};

  std::atomic<uint32_t> latchedCycles_{0};
  std::atomic<uint32_t> pulseCount_{0};
  std::atomic<bool> newEdge_{false};
  bool initialized_ = false;
};

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_PPS_HPP
