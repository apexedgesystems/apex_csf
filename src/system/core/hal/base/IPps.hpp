#ifndef APEX_HAL_IPPS_HPP
#define APEX_HAL_IPPS_HPP
/**
 * @file IPps.hpp
 * @brief Abstract pulse-per-second (PPS) edge capture interface.
 *
 * Captures the local timestamp of an external 1PPS pulse (typically from a
 * GPS receiver or other time reference). The captured timestamp is the
 * primitive that TimeServer uses to correlate the local steady clock with
 * an external time reference (UTC).
 *
 * Design principles:
 *  - No heap allocation
 *  - No POSIX dependencies in the base interface (suitable for bare-metal)
 *  - Edge timestamping happens in the implementation's interrupt or kernel
 *    context; readCapture() is a non-blocking poll from the calling thread.
 *  - The captured timestamp is in the same monotonic domain as the local
 *    steady clock used by the rest of the system, so TimeServer can
 *    interpolate without knowing which platform produced it.
 *
 * Implementations:
 *  - LinuxPps  (/dev/pps[N] via PPS_FETCH ioctl, kernel-latched timestamp)
 *  - Stm32Pps  (EXTI ISR + DWT->CYCCNT or TIM input capture)
 *  - PicoPps   (GPIO IRQ + time_us_64() latch)
 *  - Esp32Pps  (GPIO ISR + esp_timer_get_time() latch)
 *  - AvrPps    (ICP1 input capture + ICR1 latch)
 *  - MockPps   (software-injected edges for deterministic tests)
 */

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace hal {

/* ----------------------------- PpsEdge ----------------------------- */

/**
 * @brief Which edge of the PPS signal triggers a capture.
 *
 * GPS receivers conventionally emit a rising edge at the top of the
 * second; FALLING is rare but supported for inverted-polarity hardware.
 */
enum class PpsEdge : uint8_t {
  RISING = 0,  ///< Capture on rising edge (the conventional default).
  FALLING = 1, ///< Capture on falling edge.
};

/* ----------------------------- PpsStatus ----------------------------- */

/**
 * @brief Status codes for PPS operations.
 */
enum class PpsStatus : uint8_t {
  OK = 0,            ///< Operation succeeded.
  NO_NEW_EDGE,       ///< No new edge captured since last readCapture.
  ERROR_NOT_INIT,    ///< Interface not initialized.
  ERROR_DEVICE,      ///< Underlying device or peripheral error.
  ERROR_INVALID_ARG, ///< Invalid configuration argument.
};

/**
 * @brief Convert PpsStatus to string.
 * @param s Status value.
 * @return Human-readable string literal.
 * @note RT-safe: returns static string literal.
 */
inline const char* toString(PpsStatus s) noexcept {
  switch (s) {
  case PpsStatus::OK:
    return "OK";
  case PpsStatus::NO_NEW_EDGE:
    return "NO_NEW_EDGE";
  case PpsStatus::ERROR_NOT_INIT:
    return "ERROR_NOT_INIT";
  case PpsStatus::ERROR_DEVICE:
    return "ERROR_DEVICE";
  case PpsStatus::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- PpsConfig ----------------------------- */

/**
 * @brief PPS configuration parameters.
 *
 * Universal parameters live here; platform-specific options (Linux device
 * path, STM32 EXTI line, etc.) are passed through implementation
 * constructors to keep this struct portable to bare-metal targets.
 */
struct PpsConfig {
  PpsEdge edge = PpsEdge::RISING; ///< Edge polarity to capture.
};

/* ----------------------------- PpsStats ----------------------------- */

/**
 * @brief PPS statistics for monitoring.
 *
 * `captureCount` is the running count of successful captures; `errorCount`
 * counts implementation-level failures (ioctl errors, ISR overruns, etc.).
 * Glitch / out-of-band-interval rejection is the consumer's job
 * (TimeServer), not the HAL's, so it is not counted here.
 */
struct PpsStats {
  uint64_t captureCount = 0; ///< Total successful captures since init.
  uint64_t errorCount = 0;   ///< Total runtime errors since init.

  /** @brief Reset all counters to zero. */
  void reset() noexcept {
    captureCount = 0;
    errorCount = 0;
  }
};

/* ----------------------------- IPps ----------------------------- */

/**
 * @class IPps
 * @brief Abstract PPS edge capture interface.
 *
 * Lifecycle:
 *  1. Construct implementation (platform-specific args -- e.g. /dev/pps0
 *     path for Linux, EXTI/GPIO ids for STM32).
 *  2. Call init() with a PpsConfig (edge polarity, etc.).
 *  3. Poll readCapture() each frame from the consumer (TimeServer).
 *  4. Call deinit() to release the device.
 *
 * Thread Safety:
 *  - Implementations latch the timestamp in interrupt or kernel context
 *    and publish it to the polling thread atomically (single int64_t
 *    write or equivalent). Callers must not invoke readCapture()
 *    concurrently from multiple threads.
 *
 * RT-Safety:
 *  - init() / deinit():  NOT RT-safe (opens/closes hardware or kernel device).
 *  - readCapture():      RT-safe. Non-blocking read of the latched value.
 *  - pulseCount() / stats() / resetStats() / isInitialized(): RT-safe.
 *
 * Timestamp domain:
 *  - The `timestampNs` returned by readCapture() is in the same monotonic
 *    time domain as the local steady clock used by the rest of the
 *    system (Linux: CLOCK_MONOTONIC nanoseconds; MCU: an
 *    implementation-converted hardware counter). TimeServer interpolates
 *    by subtracting steady_clock::now() from it, so the two values must
 *    be commensurable.
 */
class IPps {
public:
  virtual ~IPps() = default;

  /**
   * @brief Initialize the PPS source.
   * @param config Edge polarity and other universal options.
   * @return OK on success, ERROR_* on failure.
   * @note NOT RT-safe: opens device, configures interrupts.
   */
  [[nodiscard]] virtual PpsStatus init(const PpsConfig& config) noexcept = 0;

  /**
   * @brief Release the PPS source.
   * @note NOT RT-safe: closes device, disables interrupts.
   */
  virtual void deinit() noexcept = 0;

  /**
   * @brief Check whether the PPS source is initialized.
   * @return true if init() has succeeded and deinit() has not been called.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

  /**
   * @brief Read the most recent edge capture, if a new one is available.
   * @param[out] timestampNs On OK, set to the captured local timestamp
   *                         (monotonic nanoseconds; see class doc).
   * @return OK if a new edge was captured since the previous OK return;
   *         NO_NEW_EDGE if no new edge has occurred;
   *         ERROR_NOT_INIT if init() has not succeeded;
   *         ERROR_DEVICE on underlying device failure.
   * @note RT-safe: non-blocking, single-shot consume of the latched value.
   * @note Each successful return consumes the latch; back-to-back calls
   *       without a new edge return NO_NEW_EDGE.
   */
  [[nodiscard]] virtual PpsStatus readCapture(int64_t& timestampNs) noexcept = 0;

  /**
   * @brief Get the running pulse count.
   * @return Number of edges seen since init() (monotonic, may wrap at
   *         uint32_t max -- 136 years at 1 Hz, practically non-wrapping).
   * @note RT-safe.
   * @note Distinct from `stats().captureCount`: pulseCount counts edges
   *       observed by the implementation (including any that were not
   *       consumed via readCapture()), while captureCount counts those
   *       successfully delivered to the caller.
   */
  [[nodiscard]] virtual uint32_t pulseCount() const noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe.
   */
  [[nodiscard]] virtual const PpsStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics counters to zero.
   * @note RT-safe.
   * @note Does not affect pulseCount() (that counts physical edges).
   */
  virtual void resetStats() noexcept = 0;

protected:
  IPps() = default;
  IPps(const IPps&) = delete;
  IPps& operator=(const IPps&) = delete;
  IPps(IPps&&) = default;
  IPps& operator=(IPps&&) = default;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_IPPS_HPP
