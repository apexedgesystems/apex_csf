#ifndef APEX_HAL_C2000_PPS_HPP
#define APEX_HAL_C2000_PPS_HPP
/**
 * @file C2000Pps.hpp
 * @brief PPS edge capture for TI C2000 F28004x using eCAP (C++03 compatible).
 *
 * Mirrors the API shape of `apex::hal::IPps` (init/deinit/readCapture/
 * pulseCount/stats/resetStats) but does NOT inherit from it -- the TI
 * C2000 CGT compiler is C++03 and IPps uses C++11 features (`enum
 * class`, `noexcept`, default-deleted ops). Same pattern as C2000Uart
 * relative to IUart.
 *
 * Mechanism: the F28004x eCAP (Enhanced Capture) module captures a
 * free-running 32-bit counter on a configurable GPIO edge. The capture
 * register CAP1 holds the count; an ECCTL2 flag signals new data.
 * Conversion to nanoseconds uses the configured CPU clock:
 *   ns = ticks * 1e9 / cpuFreqHz
 *
 * Single-instance per eCAP module (the F28004x has eCAP1..eCAP3 but a
 * mission typically uses one for PPS).
 *
 * Mock-mode (APEX_HAL_C2000_MOCK) drops the SDK includes and exposes
 * mockEdge() for host-side tests.
 */

#include "src/utilities/compatibility/inc/compat_legacy.hpp"

#include <stddef.h>
#include <stdint.h>

#ifndef APEX_HAL_C2000_MOCK
#include "device.h"
#include "driverlib.h"
#endif

namespace apex {
namespace hal {
namespace c2000 {

/* ----------------------------- C2000PpsStatus ----------------------------- */

/**
 * @brief Status codes for C2000Pps operations.
 * @note Plain enum (not enum class) for C++03 compatibility. Values
 *       match apex::hal::PpsStatus by ordinal so a thin adapter on the
 *       consumer side can translate cleanly.
 */
enum C2000PpsStatus {
  C2000_PPS_OK = 0,
  C2000_PPS_NO_NEW_EDGE = 1,
  C2000_PPS_ERROR_NOT_INIT = 2,
  C2000_PPS_ERROR_DEVICE = 3,
  C2000_PPS_ERROR_INVALID_ARG = 4
};

/* ----------------------------- C2000Pps ----------------------------- */

class C2000Pps {
public:
#ifndef APEX_HAL_C2000_MOCK
  explicit C2000Pps(uint32_t ecapBase, uint32_t cpuFreqHz)
      : ecapBase_(ecapBase), cpuFreqHz_(cpuFreqHz), latchedTicks_(0), pulseCount_(0),
        captureCount_(0), errorCount_(0), newEdge_(false), initialized_(false) {}

  explicit C2000Pps(uint32_t ecapBase)
      : ecapBase_(ecapBase), cpuFreqHz_(DEVICE_SYSCLK_FREQ), latchedTicks_(0), pulseCount_(0),
        captureCount_(0), errorCount_(0), newEdge_(false), initialized_(false) {}
#else
  C2000Pps()
      : cpuFreqHz_(100000000U), latchedTicks_(0), pulseCount_(0), captureCount_(0), errorCount_(0),
        newEdge_(false), initialized_(false) {}

  explicit C2000Pps(uint32_t /*ecapBase*/, uint32_t cpuFreqHz)
      : cpuFreqHz_(cpuFreqHz), latchedTicks_(0), pulseCount_(0), captureCount_(0), errorCount_(0),
        newEdge_(false), initialized_(false) {}
#endif

  /** @brief Initialize the eCAP module for input capture. */
  C2000PpsStatus init(uint8_t risingEdge) {
    if (initialized_) {
      return C2000_PPS_OK;
    }
#ifndef APEX_HAL_C2000_MOCK
    // Application-side eCAP setup is expected to use ECAP_setEventPolarity
    // and friends. We just enable the capture-loaded interrupt here so
    // the user's ISR can call inputCaptureIsr().
    ECAP_enableInterrupt(ecapBase_, ECAP_ISR_SOURCE_CAPTURE_EVENT_1);
    ECAP_setEventPolarity(ecapBase_, ECAP_EVENT_1,
                          risingEdge ? ECAP_EVNT_RISING_EDGE : ECAP_EVNT_FALLING_EDGE);
#else
    (void)risingEdge;
#endif
    latchedTicks_ = 0;
    pulseCount_ = 0;
    captureCount_ = 0;
    errorCount_ = 0;
    newEdge_ = false;
    initialized_ = true;
    return C2000_PPS_OK;
  }

  void deinit() {
#ifndef APEX_HAL_C2000_MOCK
    if (initialized_) {
      ECAP_disableInterrupt(ecapBase_, ECAP_ISR_SOURCE_CAPTURE_EVENT_1);
    }
#endif
    initialized_ = false;
  }

  bool isInitialized() const { return initialized_; }

  /** @brief Consume the latest edge timestamp, in nanoseconds. */
  C2000PpsStatus readCapture(int64_t& timestampNs) {
    if (!initialized_) {
      return C2000_PPS_ERROR_NOT_INIT;
    }
    if (!newEdge_) {
      return C2000_PPS_NO_NEW_EDGE;
    }
    const uint32_t TICKS = latchedTicks_;
    newEdge_ = false;
    // ticks * 1e9 fits in uint64 (4.29e9 max ticks * 1e9 = 4.29e18, under 1.8e19).
    const uint64_t NUMER = (uint64_t)TICKS * (uint64_t)1000000000U;
    timestampNs = (int64_t)(NUMER / (uint64_t)cpuFreqHz_);
    captureCount_ = captureCount_ + 1U;
    return C2000_PPS_OK;
  }

  uint32_t pulseCount() const { return pulseCount_; }
  uint64_t captureCount() const { return captureCount_; }
  uint64_t errorCount() const { return errorCount_; }
  void resetStats() {
    captureCount_ = 0;
    errorCount_ = 0;
  }

  /**
   * @brief Call from the eCAP1_INT (or chosen vector) ISR in user code.
   */
  void inputCaptureIsr() {
#ifndef APEX_HAL_C2000_MOCK
    latchedTicks_ = ECAP_getEventTimeStamp(ecapBase_, ECAP_EVENT_1);
    ECAP_clearInterrupt(ecapBase_, ECAP_ISR_SOURCE_CAPTURE_EVENT_1);
    ECAP_clearGlobalInterrupt(ecapBase_);
    pulseCount_ = pulseCount_ + 1U;
    newEdge_ = true;
#endif
  }

#ifdef APEX_HAL_C2000_MOCK
  /// Mock-build seam: simulate an ISR firing with a chosen tick value.
  void mockEdge(uint32_t ticks) {
    latchedTicks_ = ticks;
    pulseCount_ = pulseCount_ + 1U;
    newEdge_ = true;
  }
#endif

private:
#ifndef APEX_HAL_C2000_MOCK
  uint32_t ecapBase_;
#endif
  uint32_t cpuFreqHz_;
  // Volatiles for ISR/thread state. C2000 is single-core; volatile +
  // disabling the eCAP interrupt around readCapture is sufficient if
  // the consumer needs hard atomicity.
  volatile uint32_t latchedTicks_;
  volatile uint32_t pulseCount_;
  volatile uint64_t captureCount_;
  volatile uint64_t errorCount_;
  volatile bool newEdge_;
  bool initialized_;
};

} // namespace c2000
} // namespace hal
} // namespace apex

#endif // APEX_HAL_C2000_PPS_HPP
