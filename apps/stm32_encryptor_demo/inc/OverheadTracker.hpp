#ifndef APEX_STM32_ENCRYPTOR_OVERHEAD_TRACKER_HPP
#define APEX_STM32_ENCRYPTOR_OVERHEAD_TRACKER_HPP
/**
 * @file OverheadTracker.hpp
 * @brief DWT cycle counter overhead measurement for the stm32_encryptor_demo.
 *
 * Uses the ARM Cortex-M4 DWT->CYCCNT register to measure per-tick
 * CPU busy cycles. Two profiler tasks (highest and lowest priority)
 * bracket the scheduler to capture the full tick execution cost.
 *
 * Fast-forward control is delegated to the McuExecutive.
 *
 * All buffers are statically allocated. No heap usage.
 *
 * @note RT-safe: markTickStart() and markTickEnd() are O(1) register reads.
 */

#include "McuExecutive.hpp"

#include <stdint.h>

// DWT registers are defined in CMSIS core headers via the HAL
#include "stm32l4xx_hal.h"

namespace encryptor {

/* ----------------------------- OverheadStats ----------------------------- */

/**
 * @struct OverheadStats
 * @brief Per-tick CPU cycle statistics.
 */
struct OverheadStats {
  uint32_t lastCycles{0};         ///< Most recent tick busy cycles.
  uint32_t minCycles{UINT32_MAX}; ///< Minimum since reset.
  uint32_t maxCycles{0};          ///< Maximum since reset.
  uint32_t sampleCount{0};        ///< Ticks measured since reset.

  void reset() noexcept {
    lastCycles = 0;
    minCycles = UINT32_MAX;
    maxCycles = 0;
    sampleCount = 0;
  }
};

/* ----------------------------- OverheadTracker ----------------------------- */

/**
 * @class OverheadTracker
 * @brief DWT-based per-tick overhead measurement.
 *
 * Lifecycle:
 *  1. Construct with McuExecutive reference
 *  2. Call enableDwt() once at startup
 *  3. Register profilerStartTask (priority 127) and profilerEndTask (priority -128)
 *  4. Query stats via command channel
 *
 * @note RT-safe: markTickStart() and markTickEnd() are O(1).
 */
class OverheadTracker {
public:
  /**
   * @brief Construct tracker bound to an executive.
   * @param exec McuExecutive for fast-forward delegation and frequency query.
   */
  explicit OverheadTracker(executive::mcu::McuExecutive<>& exec) noexcept
      : exec_(exec), stats_{}, startCycles_(0) {}

  /**
   * @brief Enable the DWT cycle counter.
   *
   * Writes to CoreDebug->DEMCR, DWT->CYCCNT, and DWT->CTRL to
   * enable the 32-bit free-running cycle counter at SystemCoreClock.
   *
   * @note NOT RT-safe: Call once during initialization.
   */
  void enableDwt() noexcept {
    CoreDebug->DEMCR = CoreDebug->DEMCR | CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL = DWT->CTRL | DWT_CTRL_CYCCNTENA_Msk;
  }

  /**
   * @brief Sample DWT->CYCCNT at the start of a scheduler tick.
   *
   * Called by the profiler start task (highest priority, runs first).
   *
   * @note RT-safe: Single register read.
   */
  void markTickStart() noexcept { startCycles_ = DWT->CYCCNT; }

  /**
   * @brief Sample DWT->CYCCNT at the end of a scheduler tick.
   *
   * Computes the delta from markTickStart() and updates statistics.
   * Called by the profiler end task (lowest priority, runs last).
   *
   * @note RT-safe: Register read + arithmetic + comparisons.
   */
  void markTickEnd() noexcept {
    const uint32_t END = DWT->CYCCNT;
    const uint32_t DELTA = END - startCycles_;

    stats_.lastCycles = DELTA;
    if (DELTA < stats_.minCycles) {
      stats_.minCycles = DELTA;
    }
    if (DELTA > stats_.maxCycles) {
      stats_.maxCycles = DELTA;
    }
    ++stats_.sampleCount;
  }

  /* ----------------------------- Fast-Forward ----------------------------- */

  /**
   * @brief Enable or disable fast-forward mode.
   * @param enabled true to enable, false to resume normal timing.
   * @note Delegates to McuExecutive::setFastForward().
   */
  void setFastForward(bool enabled) noexcept { exec_.setFastForward(enabled); }

  /**
   * @brief Check if fast-forward mode is active.
   * @return true if fast-forward is enabled.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isFastForward() const noexcept { return exec_.isFastForward(); }

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get overhead statistics.
   * @return Reference to stats structure.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const OverheadStats& stats() const noexcept { return stats_; }

  /**
   * @brief Reset overhead statistics.
   * @note RT-safe: O(1).
   */
  void resetStats() noexcept { stats_.reset(); }

  /**
   * @brief Get the cycle budget per tick at normal rate.
   * @return SystemCoreClock / scheduler fundamental frequency.
   *
   * At 80 MHz and 100 Hz: 800,000 cycles per tick.
   *
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uint32_t budgetCycles() const noexcept {
    const uint16_t FREQ = exec_.scheduler().fundamentalFreq();
    return (FREQ > 0) ? (SystemCoreClock / FREQ) : 0;
  }

private:
  executive::mcu::McuExecutive<>& exec_;
  OverheadStats stats_;
  uint32_t startCycles_;
};

} // namespace encryptor

#endif // APEX_STM32_ENCRYPTOR_OVERHEAD_TRACKER_HPP
