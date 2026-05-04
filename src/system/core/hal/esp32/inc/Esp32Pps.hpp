#ifndef APEX_HAL_ESP32_PPS_HPP
#define APEX_HAL_ESP32_PPS_HPP
/**
 * @file Esp32Pps.hpp
 * @brief ESP32 PPS edge capture using GPIO ISR + esp_timer_get_time().
 *
 * ESP-IDF's gpio_install_isr_service + gpio_isr_handler_add gives us a
 * per-instance context pointer, so multiple Esp32Pps instances on
 * different pins coexist cleanly. The ISR latches
 * esp_timer_get_time() (microseconds since boot, monotonic) and the
 * polling thread consumes the latched value through readCapture().
 *
 * Mock-mode builds (APEX_HAL_ESP32_MOCK) drop the ESP-IDF includes and
 * expose mockEdge() so host-side tests verify the IPps contract.
 */

#include "src/system/core/hal/base/IPps.hpp"

#include <atomic>
#include <stdint.h>

#ifndef APEX_HAL_ESP32_MOCK
#include "driver/gpio.h"
#include "esp_timer.h"
#endif

namespace apex {
namespace hal {
namespace esp32 {

/* ----------------------------- Esp32Pps ----------------------------- */

class Esp32Pps : public IPps {
public:
#ifndef APEX_HAL_ESP32_MOCK
  explicit Esp32Pps(gpio_num_t gpio) noexcept : gpio_(gpio) {}
#else
  explicit Esp32Pps(int /*gpio*/) noexcept {}
#endif

  ~Esp32Pps() override = default;
  Esp32Pps(const Esp32Pps&) = delete;
  Esp32Pps& operator=(const Esp32Pps&) = delete;

  /* ----------------------------- IPps overrides ----------------------------- */

  [[nodiscard]] PpsStatus init(const PpsConfig& config) noexcept override {
    if (initialized_) {
      return PpsStatus::OK;
    }
    config_ = config;

#ifndef APEX_HAL_ESP32_MOCK
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << static_cast<uint64_t>(gpio_);
    io.mode = GPIO_MODE_INPUT;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.intr_type =
        (config.edge == PpsEdge::RISING) ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE;
    if (gpio_config(&io) != ESP_OK) {
      return PpsStatus::ERROR_DEVICE;
    }
    // ESP-IDF's gpio_install_isr_service is idempotent and returns
    // ESP_ERR_INVALID_STATE if already installed. Either is fine.
    (void)gpio_install_isr_service(0);
    if (gpio_isr_handler_add(gpio_, &Esp32Pps::isrTrampoline, this) != ESP_OK) {
      return PpsStatus::ERROR_DEVICE;
    }
#endif

    latchedNs_.store(0, std::memory_order_relaxed);
    pulseCount_.store(0, std::memory_order_relaxed);
    newEdge_.store(false, std::memory_order_relaxed);
    stats_.reset();
    initialized_ = true;
    return PpsStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    if (initialized_) {
      (void)gpio_isr_handler_remove(gpio_);
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

#ifdef APEX_HAL_ESP32_MOCK
  /// Mock-build seam: simulate an ISR firing with a chosen us-domain
  /// timestamp.
  void mockEdge(uint64_t edgeUs) noexcept {
    latchedNs_.store(edgeUs * 1000ULL, std::memory_order_relaxed);
    pulseCount_.fetch_add(1, std::memory_order_relaxed);
    newEdge_.store(true, std::memory_order_release);
  }
#endif

private:
#ifndef APEX_HAL_ESP32_MOCK
  /// Per-instance ISR thunk. ESP-IDF passes the registered context
  /// pointer (this) through, so multi-instance is safe out of the box.
  static void IRAM_ATTR isrTrampoline(void* arg) noexcept {
    auto* self = static_cast<Esp32Pps*>(arg);
    const int64_t NOW_US = esp_timer_get_time();
    self->latchedNs_.store(static_cast<uint64_t>(NOW_US) * 1000ULL,
                           std::memory_order_relaxed);
    self->pulseCount_.fetch_add(1, std::memory_order_relaxed);
    self->newEdge_.store(true, std::memory_order_release);
  }

  gpio_num_t gpio_{};
#endif

  PpsConfig config_{};
  PpsStats stats_{};

  std::atomic<uint64_t> latchedNs_{0};
  std::atomic<uint32_t> pulseCount_{0};
  std::atomic<bool> newEdge_{false};
  bool initialized_ = false;
};

} // namespace esp32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_ESP32_PPS_HPP
