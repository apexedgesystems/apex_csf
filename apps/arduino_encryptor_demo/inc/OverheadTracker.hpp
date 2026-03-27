#ifndef APEX_ARDUINO_ENCRYPTOR_OVERHEAD_TRACKER_HPP
#define APEX_ARDUINO_ENCRYPTOR_OVERHEAD_TRACKER_HPP
/**
 * @file OverheadTracker.hpp
 * @brief Timer-based per-tick overhead measurement for the arduino_encryptor_demo.
 *
 * Uses Timer0's TCNT0 register (8-bit, prescaler=64 from Arduino bootloader)
 * to estimate per-tick CPU busy time. Less precise than STM32 DWT (8-bit
 * overflow at 256 ticks = 1.024 ms at 16 MHz/64) but provides the same
 * API surface for the command protocol.
 *
 * For longer measurements, combines TCNT0 reads with a software overflow
 * counter incremented by TIMER0_OVF_vect (which the Arduino bootloader
 * normally hooks for millis()). Since we run bare-metal without Arduino
 * libs, we provide our own overflow counter.
 *
 * Fast-forward control is delegated to the LiteExecutive.
 *
 * @note RT-safe: markTickStart() and markTickEnd() are O(1) register reads.
 */

#include "LiteExecutive.hpp"

#include <avr/io.h>
#include <stdint.h>

namespace encryptor {

/* ----------------------------- OverheadStats ----------------------------- */

/**
 * @struct OverheadStats
 * @brief Per-tick CPU cycle statistics.
 */
struct OverheadStats {
  uint32_t lastCycles{0};
  uint32_t minCycles{UINT32_MAX};
  uint32_t maxCycles{0};
  uint32_t sampleCount{0};

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
 * @brief Timer0-based per-tick overhead measurement.
 *
 * Timer0 runs at F_CPU/64 = 250 kHz (4 us per tick). TCNT0 is 8-bit so
 * overflows every 256 ticks = 1.024 ms. We track overflows to extend range.
 *
 * Cycle values reported are in Timer0 ticks (4 us each at 16 MHz).
 * To convert to CPU cycles: multiply by 64.
 *
 * Budget is reported in Timer0 ticks: F_CPU / 64 / scheduler_freq_hz.
 * At 100 Hz: budget = 250000 / 100 = 2500 Timer0 ticks per scheduler tick.
 */
class OverheadTracker {
public:
  explicit OverheadTracker(executive::lite::LiteExecutive<8, uint32_t>& exec) noexcept
      : exec_(exec), stats_{}, startCount_(0), overflows_(0) {}

  /**
   * @brief Initialize Timer0 for overhead measurement.
   *
   * Timer0 in normal mode, prescaler=64 (CS0[2:0] = 011).
   * Enable overflow interrupt for extended range.
   *
   * @note NOT RT-safe: Call once during initialization.
   */
  void enableTimer() noexcept {
    // Timer0: normal mode, prescaler=64
    TCCR0A = 0;
    TCCR0B = (1 << CS01) | (1 << CS00); // clk/64
    TCNT0 = 0;
    TIMSK0 |= (1 << TOIE0); // Enable overflow interrupt
    overflows_ = 0;
  }

  /**
   * @brief Sample Timer0 at the start of a scheduler tick.
   * @note RT-safe: register read + volatile write.
   */
  void markTickStart() noexcept {
    overflows_ = 0;
    startCount_ = TCNT0;
  }

  /**
   * @brief Sample Timer0 at the end of a scheduler tick.
   * @note RT-safe: register read + arithmetic.
   */
  void markTickEnd() noexcept {
    const uint8_t END_COUNT = TCNT0;
    const uint32_t DELTA = (static_cast<uint32_t>(overflows_) * 256U) + END_COUNT - startCount_;

    stats_.lastCycles = DELTA;
    if (DELTA < stats_.minCycles) {
      stats_.minCycles = DELTA;
    }
    if (DELTA > stats_.maxCycles) {
      stats_.maxCycles = DELTA;
    }
    ++stats_.sampleCount;
  }

  /**
   * @brief Called from TIMER0_OVF_vect ISR to track overflows.
   * @note ISR context only.
   */
  void overflowIsr() noexcept { ++overflows_; }

  /* ----------------------------- Fast-Forward ----------------------------- */

  void setFastForward(bool enabled) noexcept { exec_.setFastForward(enabled); }

  [[nodiscard]] bool isFastForward() const noexcept { return exec_.isFastForward(); }

  /* ----------------------------- Accessors ----------------------------- */

  [[nodiscard]] const OverheadStats& stats() const noexcept { return stats_; }

  void resetStats() noexcept { stats_.reset(); }

  /**
   * @brief Get the Timer0 tick budget per scheduler tick.
   * @return F_CPU / 64 / scheduler_freq_hz.
   *
   * At 16 MHz, prescaler=64, 100 Hz: 250000/100 = 2500 ticks.
   */
  [[nodiscard]] uint32_t budgetCycles() const noexcept {
    const uint16_t FREQ = exec_.scheduler().fundamentalFreq();
    return (FREQ > 0) ? ((F_CPU / 64UL) / FREQ) : 0;
  }

  /* ----------------------------- Static ISR Linkage ----------------------------- */

  static OverheadTracker* instance_;

private:
  executive::lite::LiteExecutive<8, uint32_t>& exec_;
  OverheadStats stats_;
  volatile uint8_t startCount_;
  volatile uint16_t overflows_;
};

} // namespace encryptor

#endif // APEX_ARDUINO_ENCRYPTOR_OVERHEAD_TRACKER_HPP
