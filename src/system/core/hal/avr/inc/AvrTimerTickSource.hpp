#ifndef APEX_HAL_AVR_TIMER_TICK_SOURCE_HPP
#define APEX_HAL_AVR_TIMER_TICK_SOURCE_HPP
/**
 * @file AvrTimerTickSource.hpp
 * @brief ATmega328P Timer1 tick source implementing ITickSource.
 *
 * Uses Timer1 (16-bit) in CTC mode to generate a precise tick at the
 * requested frequency. At 16 MHz with prescaler=64:
 *   OCR1A = (F_CPU / prescaler / freqHz) - 1
 *   100 Hz: OCR1A = (16000000 / 64 / 100) - 1 = 2499
 *
 * The ISR(TIMER1_COMPA_vect) sets a volatile flag that waitForNextTick()
 * polls. Since AVR has no WFI instruction, the wait is a spin loop.
 *
 * @note One instance only (Timer1 is a singleton resource).
 */

#include "src/system/core/executive/lite/inc/ITickSource.hpp"

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

/* ----------------------------- AvrTimerTickSource ----------------------------- */

/**
 * @class AvrTimerTickSource
 * @brief Timer1 CTC-based tick source for LiteExecutive.
 */
class AvrTimerTickSource final : public executive::lite::ITickSource {
public:
  /**
   * @brief Construct tick source with desired frequency.
   * @param freqHz Tick frequency in Hz (e.g., 100 for 100 Hz).
   */
  explicit AvrTimerTickSource(uint16_t freqHz) noexcept
      : freqHz_(freqHz), tickCount_(0), running_(false) {
#ifndef APEX_HAL_AVR_MOCK
    instance_ = this;
#endif
  }

#ifdef APEX_HAL_AVR_MOCK
  /**
   * @brief Mock default constructor (100 Hz).
   */
  AvrTimerTickSource() noexcept : freqHz_(100), tickCount_(0), running_(false) {}
#endif

  ~AvrTimerTickSource() override {
    stop();
#ifndef APEX_HAL_AVR_MOCK
    instance_ = nullptr;
#endif
  }

  AvrTimerTickSource(const AvrTimerTickSource&) = delete;
  AvrTimerTickSource& operator=(const AvrTimerTickSource&) = delete;
  AvrTimerTickSource(AvrTimerTickSource&&) = delete;
  AvrTimerTickSource& operator=(AvrTimerTickSource&&) = delete;

  /* ----------------------------- Timing Control ----------------------------- */

  void waitForNextTick() noexcept override {
#ifndef APEX_HAL_AVR_MOCK
    // Spin-wait until ISR sets the pending flag
    while (!tickPending_) {
      // No WFI on AVR; could use sleep_mode() for power saving
    }
    tickPending_ = false;
#else
    // Mock: increment counter and return immediately
    tickCount_ = tickCount_ + 1;
#endif
  }

  void ackTick() noexcept override {
    // Nothing to acknowledge -- flag was cleared in waitForNextTick
  }

  /* ----------------------------- Query ----------------------------- */

  [[nodiscard]] uint32_t currentTick() const noexcept override { return tickCount_; }

  [[nodiscard]] uint32_t tickFrequency() const noexcept override { return freqHz_; }

  /* ----------------------------- Lifecycle ----------------------------- */

  void start() noexcept override {
    tickPending_ = false;
    tickCount_ = 0;

#ifndef APEX_HAL_AVR_MOCK
    // Configure Timer1 in CTC mode (WGM12)
    // Prescaler = 64 (CS11 + CS10)
    // OCR1A = (F_CPU / 64 / freqHz) - 1
    const uint16_t OCR_VAL = static_cast<uint16_t>((F_CPU / 64UL / freqHz_) - 1);

    cli();
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10); // CTC, prescaler=64
    OCR1A = OCR_VAL;
    TCNT1 = 0;
    TIMSK1 |= (1 << OCIE1A); // Enable compare match A interrupt
    sei();
#endif

    running_ = true;
  }

  void stop() noexcept override {
#ifndef APEX_HAL_AVR_MOCK
    TIMSK1 &= ~(1 << OCIE1A); // Disable compare match interrupt
    TCCR1B = 0;               // Stop timer
#endif
    running_ = false;
  }

  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

  /* ----------------------------- ISR Callback ----------------------------- */

#ifndef APEX_HAL_AVR_MOCK
  /**
   * @brief Timer1 compare match ISR callback.
   * @note Called from ISR(TIMER1_COMPA_vect). Do not call directly.
   */
  void timerIsr() noexcept {
    tickPending_ = true;
    tickCount_ = tickCount_ + 1;
  }

  /// Static instance pointer for ISR dispatch.
  static AvrTimerTickSource* instance_;
#endif

private:
  uint16_t freqHz_;
  volatile uint32_t tickCount_;
  volatile bool tickPending_ = false;
  bool running_;
};

} // namespace avr
} // namespace hal
} // namespace apex

#endif // APEX_HAL_AVR_TIMER_TICK_SOURCE_HPP
