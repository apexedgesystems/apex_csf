#ifndef APEX_EXECUTIVE_MCU_ITICK_SOURCE_HPP
#define APEX_EXECUTIVE_MCU_ITICK_SOURCE_HPP
/**
 * @file ITickSource.hpp
 * @brief Abstract tick source interface for McuExecutive.
 *
 * Provides a platform-agnostic way to drive the executive's main loop.
 * Implementations include:
 *   - FreeRunningSource: No wait, max speed (testing/benchmarking)
 *   - SysTickSource: ARM SysTick timer (bare-metal Cortex-M)
 *   - TimerTickSource: Hardware timer (STM32 TIMx)
 *   - FreeRtosTickSource: FreeRTOS tick wrapper
 *   - PpsTickSource: External PPS input (GPS/atomic clock sync)
 *
 * @note RT-safe: All methods are noexcept with bounded execution time.
 */

#include <stdint.h>

namespace executive {
namespace mcu {

/* ----------------------------- ITickSource ----------------------------- */

/**
 * @class ITickSource
 * @brief Abstract interface for tick generation.
 *
 * McuExecutive calls waitForNextTick() in its main loop, allowing the
 * tick source implementation to control timing. This enables:
 *   - Bare-metal: WFI sleep between ticks (power saving)
 *   - RTOS: xTaskDelayUntil for cooperative scheduling
 *   - Testing: Free-running for max-speed execution
 */
class ITickSource {
public:
  virtual ~ITickSource() = default;

  /* ----------------------------- Timing Control ----------------------------- */

  /**
   * @brief Block until the next tick is due.
   *
   * Implementation may:
   *   - Sleep (WFI on bare-metal)
   *   - Wait on semaphore (ISR-driven)
   *   - Spin-wait (polling)
   *   - Return immediately (free-running)
   *
   * @note RT-safe: Must complete within one tick period.
   */
  virtual void waitForNextTick() noexcept = 0;

  /**
   * @brief Acknowledge the current tick (optional).
   *
   * For interrupt-driven sources, this may clear pending flags or
   * re-arm the timer. Default implementation does nothing.
   *
   * @note RT-safe: No allocation, bounded execution.
   */
  virtual void ackTick() noexcept {}

  /* ----------------------------- Query ----------------------------- */

  /**
   * @brief Get current tick count.
   * @return Monotonic tick count (wraps at 32 bits).
   *
   * @note RT-safe: Simple register read or counter access.
   */
  [[nodiscard]] virtual uint32_t currentTick() const noexcept = 0;

  /**
   * @brief Get tick frequency in Hz.
   * @return Tick rate (e.g., 100 for 100Hz, 1000 for 1kHz).
   *
   * @note RT-safe: Returns compile-time or cached value.
   */
  [[nodiscard]] virtual uint32_t tickFrequency() const noexcept = 0;

  /**
   * @brief Get tick period in microseconds.
   * @return Microseconds per tick (e.g., 10000 for 100Hz).
   *
   * Default implementation computes from tickFrequency().
   *
   * @note RT-safe: Simple arithmetic.
   */
  [[nodiscard]] virtual uint32_t tickPeriodUs() const noexcept {
    const uint32_t FREQ = tickFrequency();
    return (FREQ > 0) ? (1000000U / FREQ) : 0U;
  }

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Start the tick source.
   *
   * Enable the timer/interrupt. After this call, waitForNextTick()
   * will block until ticks are generated.
   *
   * @note NOT RT-safe: May configure hardware.
   */
  virtual void start() noexcept = 0;

  /**
   * @brief Stop the tick source.
   *
   * Disable the timer/interrupt. After this call, waitForNextTick()
   * behavior is undefined (may block forever or return immediately).
   *
   * @note NOT RT-safe: May configure hardware.
   */
  virtual void stop() noexcept = 0;

  /**
   * @brief Check if tick source is running.
   * @return true if started, false if stopped.
   *
   * @note RT-safe: Simple flag check.
   */
  [[nodiscard]] virtual bool isRunning() const noexcept = 0;
};

} // namespace mcu
} // namespace executive

#endif // APEX_EXECUTIVE_MCU_ITICK_SOURCE_HPP
