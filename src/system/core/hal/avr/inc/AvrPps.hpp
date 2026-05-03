#ifndef APEX_HAL_AVR_PPS_HPP
#define APEX_HAL_AVR_PPS_HPP
/**
 * @file AvrPps.hpp
 * @brief ATmega328P PPS edge capture using Timer1 ICP1 input capture.
 *
 * The ATmega328P's Timer1 has an Input Capture function: when ICP1
 * (PB0) sees its configured edge, the hardware copies TCNT1 into ICR1
 * within 1 cycle, and the ICF1 flag in TIFR1 is raised to signal the
 * ISR. We read ICR1 in the ISR(TIMER1_CAPT_vect) handler and combine
 * it with a software-maintained Timer1-overflow count to form a
 * 32-bit tick value, which converts to nanoseconds via F_CPU and the
 * Timer1 prescaler.
 *
 * Resource conflicts:
 *   Timer1 is shared with `AvrTimerTickSource`. An application that
 *   uses both must either:
 *     - Configure Timer1 once, with a prescaler and TOP that allow
 *       both functions (CTC + ICP capture); or
 *     - Run AvrPps with ICP-only (no scheduler tick on Timer1) and
 *       drive the executive's tick from a different source.
 *   The header does NOT configure Timer1 itself -- the application
 *   sets prescaler / TOP / interrupt enables before calling init().
 *
 * Single-instance: ATmega328P has one ICP1 pin, so one AvrPps
 * instance per system.
 *
 * Mock-mode (APEX_HAL_AVR_MOCK) drops the AVR includes and exposes
 * mockEdge() so host-side tests verify the IPps contract.
 */

#include "src/system/core/hal/base/IPps.hpp"

#include <stdint.h>

#ifndef APEX_HAL_AVR_MOCK
#include <avr/interrupt.h>
#include <avr/io.h>
#else
#ifndef F_CPU
#define F_CPU 16000000UL
#endif
#endif

namespace apex {
namespace hal {
namespace avr {

/* ----------------------------- AvrPpsOptions ----------------------------- */

struct AvrPpsOptions {
  /// Timer1 prescaler the application configured (1, 8, 64, 256, 1024).
  /// Used purely for cycles->ns conversion; AvrPps does not write TCCR1B.
  uint16_t prescaler = 1;
};

/* ----------------------------- AvrPps ----------------------------- */

class AvrPps : public IPps {
public:
  explicit AvrPps(const AvrPpsOptions& opts = {}) noexcept : opts_(opts) {
#ifndef APEX_HAL_AVR_MOCK
    instance_ = this;
#endif
  }

  ~AvrPps() override = default;
  AvrPps(const AvrPps&) = delete;
  AvrPps& operator=(const AvrPps&) = delete;

  /* ----------------------------- IPps overrides ----------------------------- */

  PpsStatus init(const PpsConfig& config) noexcept override {
    if (initialized_) {
      return PpsStatus::OK;
    }
    config_ = config;
#ifndef APEX_HAL_AVR_MOCK
    // Configure ICP1 edge polarity in TCCR1B (ICES1 bit).
    if (config.edge == PpsEdge::RISING) {
      TCCR1B |= (1U << ICES1);
    } else {
      TCCR1B &= static_cast<uint8_t>(~(1U << ICES1));
    }
    // Enable input-capture interrupt and Timer1 overflow interrupt
    // (the latter feeds our software extension counter).
    TIMSK1 |= (1U << ICIE1) | (1U << TOIE1);
#endif
    overflowCount_ = 0;
    latchedTicks_ = 0;
    pulseCount_ = 0;
    newEdge_ = false;
    stats_.reset();
    initialized_ = true;
    return PpsStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_AVR_MOCK
    if (initialized_) {
      TIMSK1 &= static_cast<uint8_t>(~((1U << ICIE1) | (1U << TOIE1)));
    }
#endif
    initialized_ = false;
  }

  bool isInitialized() const noexcept override { return initialized_; }

  PpsStatus readCapture(int64_t& timestampNs) noexcept override {
    if (!initialized_) {
      return PpsStatus::ERROR_NOT_INIT;
    }
    bool hadEdge = false;
    uint32_t ticks = 0;
#ifndef APEX_HAL_AVR_MOCK
    cli();
#endif
    if (newEdge_) {
      hadEdge = true;
      ticks = latchedTicks_;
      newEdge_ = false;
    }
#ifndef APEX_HAL_AVR_MOCK
    sei();
#endif
    if (!hadEdge) {
      return PpsStatus::NO_NEW_EDGE;
    }
    // ticks * (1e9 / (F_CPU / prescaler)) = ticks * prescaler * 1e9 / F_CPU.
    // For 16 MHz / prescaler=1: 62.5 ns/tick. uint64 math to avoid overflow.
    const uint64_t numer = static_cast<uint64_t>(ticks) * opts_.prescaler * 1'000'000'000ULL;
    timestampNs = static_cast<int64_t>(numer / static_cast<uint64_t>(F_CPU));
    ++stats_.captureCount;
    return PpsStatus::OK;
  }

  uint32_t pulseCount() const noexcept override { return pulseCount_; }
  const PpsStats& stats() const noexcept override { return stats_; }
  void resetStats() noexcept override { stats_.reset(); }

  /* ----------------------------- ISR entry points ----------------------------- */

  /**
   * @brief Call from ISR(TIMER1_CAPT_vect) in user code.
   * @note RT-safe: latches ICR1 + overflow count atomically (ISR context).
   */
  void inputCaptureIsr() noexcept {
#ifndef APEX_HAL_AVR_MOCK
    const uint16_t icr = ICR1;
    latchedTicks_ = (static_cast<uint32_t>(overflowCount_) << 16) | icr;
    pulseCount_ = static_cast<uint32_t>(pulseCount_ + 1U);
    newEdge_ = true;
#endif
  }

  /**
   * @brief Call from ISR(TIMER1_OVF_vect) in user code.
   * @note Maintains the 32-bit software extension of the 16-bit Timer1.
   */
  void overflowIsr() noexcept {
    overflowCount_ = static_cast<uint16_t>(overflowCount_ + 1U);
  }

#ifdef APEX_HAL_AVR_MOCK
  /// Mock-build seam: simulate an ISR firing with a chosen tick value.
  void mockEdge(uint32_t ticks) noexcept {
    latchedTicks_ = ticks;
    pulseCount_ = static_cast<uint32_t>(pulseCount_ + 1U);
    newEdge_ = true;
  }
#endif

private:
#ifndef APEX_HAL_AVR_MOCK
  inline static AvrPps* instance_ = nullptr;
#endif

  AvrPpsOptions opts_;
  PpsConfig config_{};
  PpsStats stats_{};

  // Volatile because the ISR mutates these and the polling thread reads.
  // AVR is single-core; volatile + cli/sei in readCapture is sufficient.
  volatile uint16_t overflowCount_ = 0;
  volatile uint32_t latchedTicks_ = 0;
  volatile uint32_t pulseCount_ = 0;
  volatile bool newEdge_ = false;
  bool initialized_ = false;
};

} // namespace avr
} // namespace hal
} // namespace apex

#endif // APEX_HAL_AVR_PPS_HPP
