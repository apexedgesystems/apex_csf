#ifndef APEX_HAL_C2000_TIMER_TICK_SOURCE_HPP
#define APEX_HAL_C2000_TIMER_TICK_SOURCE_HPP
/**
 * @file C2000TimerTickSource.hpp
 * @brief CPU Timer 0 tick source for TI C2000 F28004x (C++03 compatible).
 *
 * Standalone tick source using driverlib API. Does not inherit from
 * ITickSource (C2000 CGT only supports C++03). Same API shape as
 * other platforms.
 */

#include "src/utilities/compatibility/inc/compat_legacy.hpp"

#include <stdint.h>

#ifndef APEX_HAL_C2000_MOCK
#include "driverlib.h"
#include "device.h"
#endif

namespace apex {
namespace hal {
namespace c2000 {

/* ----------------------------- C2000TimerTickSource ----------------------------- */

class C2000TimerTickSource {
public:
  C2000TimerTickSource(uint32_t freqHz, uint32_t sysclkHz)
      : frequency_(freqHz > 0 ? freqHz : 1), sysclkHz_(sysclkHz), tickCount_(0),
        tickPending_(false), running_(false) {}

  explicit C2000TimerTickSource(uint32_t freqHz)
      : frequency_(freqHz > 0 ? freqHz : 1), sysclkHz_(100000000U), tickCount_(0),
        tickPending_(false), running_(false) {}

  void timerIsr() {
    tickPending_ = true;
    tickCount_ = tickCount_ + 1;
  }

  void waitForNextTick() {
#ifndef APEX_HAL_C2000_MOCK
    if (!running_) {
      return;
    }
    while (!tickPending_) { /* spin */
    }
    tickPending_ = false;
#else
    if (running_) {
      tickCount_ = tickCount_ + 1;
    }
#endif
  }

  uint32_t currentTick() const { return tickCount_; }
  uint32_t tickFrequency() const { return frequency_; }
  bool isRunning() const { return running_; }

  void start() {
    tickCount_ = 0;
    tickPending_ = false;

#ifndef APEX_HAL_C2000_MOCK
    uint32_t period = (sysclkHz_ / frequency_) - 1U;
    CPUTimer_setPeriod(CPUTIMER0_BASE, period);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
    Interrupt_register(INT_TIMER0, NULL);
    Interrupt_enable(INT_TIMER0);
#else
    (void)sysclkHz_;
#endif

    running_ = true;
  }

  void stop() {
    running_ = false;
#ifndef APEX_HAL_C2000_MOCK
    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_disableInterrupt(CPUTIMER0_BASE);
#endif
  }

private:
  uint32_t frequency_;
  uint32_t sysclkHz_;
  volatile uint32_t tickCount_;
  volatile bool tickPending_;
  bool running_;
};

} /* namespace c2000 */
} /* namespace hal */
} /* namespace apex */

#endif /* APEX_HAL_C2000_TIMER_TICK_SOURCE_HPP */
