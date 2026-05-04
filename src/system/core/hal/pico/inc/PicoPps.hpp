#ifndef APEX_HAL_PICO_PPS_HPP
#define APEX_HAL_PICO_PPS_HPP
/**
 * @file PicoPps.hpp
 * @brief RP2040 PPS edge capture using GPIO IRQ + time_us_64().
 *
 * The Pico SDK's hardware GPIO subsystem dispatches edge interrupts to a
 * registered callback. We install a static-method callback that latches
 * the current time in microseconds (time_us_64()) and converts to
 * nanoseconds by multiplying by 1000. The RP2040 timer driving
 * time_us_64 is the system 1 MHz watchdog tick, monotonic since boot --
 * the same domain used by other Pico HAL implementations.
 *
 * Single-instance per GPIO pin: the SDK callback dispatch routes by
 * pin, so multiple PicoPps instances bound to different pins coexist.
 * Mock-mode builds (APEX_HAL_PICO_MOCK) drop the SDK includes and
 * expose mockEdge() so host-side tests can verify the IPps contract.
 */

#include "src/system/core/hal/base/IPps.hpp"

#include <atomic>
#include <stdint.h>

#ifndef APEX_HAL_PICO_MOCK
#include "hardware/gpio.h"
#include "pico/time.h"
#endif

namespace apex {
namespace hal {
namespace pico {

/* ----------------------------- PicoPps ----------------------------- */

/**
 * @class PicoPps
 * @brief IPps backed by RP2040 GPIO IRQ and time_us_64().
 */
class PicoPps : public IPps {
public:
#ifndef APEX_HAL_PICO_MOCK
  explicit PicoPps(uint8_t gpioPin) noexcept : gpioPin_(gpioPin) {}
#else
  explicit PicoPps(uint8_t /*gpioPin*/) noexcept {}
#endif

  ~PicoPps() override = default;
  PicoPps(const PicoPps&) = delete;
  PicoPps& operator=(const PicoPps&) = delete;

  /* ----------------------------- IPps overrides ----------------------------- */

  [[nodiscard]] PpsStatus init(const PpsConfig& config) noexcept override {
    if (initialized_) {
      return PpsStatus::OK;
    }
    config_ = config;

#ifndef APEX_HAL_PICO_MOCK
    instance_ = this;
    gpio_init(gpioPin_);
    gpio_set_dir(gpioPin_, GPIO_IN);
    gpio_pull_down(gpioPin_);
    const uint32_t EDGE_MASK =
        (config.edge == PpsEdge::RISING) ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL;
    gpio_set_irq_enabled_with_callback(gpioPin_, EDGE_MASK, true, &PicoPps::gpioCallback);
#endif

    latchedNs_.store(0, std::memory_order_relaxed);
    pulseCount_.store(0, std::memory_order_relaxed);
    newEdge_.store(false, std::memory_order_relaxed);
    stats_.reset();
    initialized_ = true;
    return PpsStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_PICO_MOCK
    if (initialized_) {
      gpio_set_irq_enabled(gpioPin_, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    }
    if (instance_ == this) {
      instance_ = nullptr;
    }
#endif
    initialized_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  [[nodiscard]] PpsStatus readCapture(int64_t& timestampNs) noexcept override {
    if (!initialized_) {
      return PpsStatus::ERROR_NOT_INIT;
    }
    if (!newEdge_.exchange(false, std::memory_order_acquire)) {
      return PpsStatus::NO_NEW_EDGE;
    }
    timestampNs = static_cast<int64_t>(latchedNs_.load(std::memory_order_relaxed));
    ++stats_.captureCount;
    return PpsStatus::OK;
  }

  [[nodiscard]] uint32_t pulseCount() const noexcept override {
    return pulseCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] const PpsStats& stats() const noexcept override { return stats_; }
  void resetStats() noexcept override { stats_.reset(); }

#ifdef APEX_HAL_PICO_MOCK
  /// Mock-build seam: simulate an ISR firing with a chosen us-domain
  /// timestamp. The ISR-side latch normally happens via gpioCallback.
  void mockEdge(uint64_t edgeUs) noexcept {
    latchedNs_.store(edgeUs * 1000ULL, std::memory_order_relaxed);
    pulseCount_.fetch_add(1, std::memory_order_relaxed);
    newEdge_.store(true, std::memory_order_release);
  }
#endif

private:
#ifndef APEX_HAL_PICO_MOCK
  /// Static dispatcher: the SDK's per-callback API delivers (pin, events)
  /// to a single function, so we route to the live instance.
  static void gpioCallback(uint gpio, uint32_t /*events*/) noexcept {
    if (instance_ == nullptr || gpio != instance_->gpioPin_) {
      return;
    }
    const uint64_t NOW_US = time_us_64();
    instance_->latchedNs_.store(NOW_US * 1000ULL, std::memory_order_relaxed);
    instance_->pulseCount_.fetch_add(1, std::memory_order_relaxed);
    instance_->newEdge_.store(true, std::memory_order_release);
  }

  uint8_t gpioPin_ = 0;
  inline static PicoPps* instance_ = nullptr;
#endif

  PpsConfig config_{};
  PpsStats stats_{};

  std::atomic<uint64_t> latchedNs_{0};
  std::atomic<uint32_t> pulseCount_{0};
  std::atomic<bool> newEdge_{false};
  bool initialized_ = false;
};

} // namespace pico
} // namespace hal
} // namespace apex

#endif // APEX_HAL_PICO_PPS_HPP
