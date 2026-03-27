#ifndef APEX_HAL_ESP32_TIMER_TICK_SOURCE_HPP
#define APEX_HAL_ESP32_TIMER_TICK_SOURCE_HPP
/**
 * @file Esp32TimerTickSource.hpp
 * @brief ESP-IDF esp_timer-based tick source for LiteExecutive on ESP32-S3.
 *
 * Uses esp_timer_create() with a periodic callback to generate ticks.
 * The LiteExecutive runs inside a FreeRTOS task; waitForNextTick() uses
 * ulTaskNotifyTake() to sleep between ticks (power-efficient).
 *
 * The ESP32-S3 runs at 240 MHz. The esp_timer runs at 1 MHz resolution
 * and is independent of the FreeRTOS tick.
 *
 * In mock mode (APEX_HAL_ESP32_MOCK), waitForNextTick() increments the
 * counter and returns immediately. No ESP-IDF or FreeRTOS dependencies
 * required.
 *
 * Usage:
 *  1. Create instance with desired frequency
 *  2. Pass to LiteExecutive constructor
 *  3. Call start() (sets up esp_timer periodic callback)
 *
 * @code
 * static apex::hal::esp32::Esp32TimerTickSource tickSource(100);
 *
 * executive::lite::LiteExecutive exec(&tickSource, 100);
 * tickSource.start();
 * exec.run();
 * @endcode
 *
 * @note RT-safe after start: waitForNextTick() blocks via FreeRTOS notification.
 */

#include "src/system/core/executive/lite/inc/ITickSource.hpp"

#ifndef APEX_HAL_ESP32_MOCK
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include <stdint.h>

namespace apex {
namespace hal {
namespace esp32 {

/* ----------------------------- Esp32TimerTickSource ----------------------------- */

/**
 * @class Esp32TimerTickSource
 * @brief Tick source driven by ESP-IDF esp_timer on ESP32-S3.
 *
 * Uses a periodic esp_timer callback to notify the executive task
 * via FreeRTOS task notification. waitForNextTick() uses
 * ulTaskNotifyTake() for efficient blocking.
 *
 * In mock mode, waitForNextTick() increments the counter and returns
 * immediately without blocking.
 *
 * Memory usage: ~48 bytes + esp_timer handle (real), ~16 bytes (mock).
 */
class Esp32TimerTickSource final : public executive::lite::ITickSource {
public:
  /**
   * @brief Construct with desired tick frequency.
   * @param freqHz Executive tick frequency in Hz (default 100).
   * @note NOT RT-safe: Construction only.
   */
  explicit Esp32TimerTickSource(uint32_t freqHz = 100) noexcept
      : frequency_(freqHz > 0 ? freqHz : 1)
#ifndef APEX_HAL_ESP32_MOCK
        ,
        periodUs_(1000000U / frequency_)
#endif
  {
  }

  ~Esp32TimerTickSource() override { stop(); }

  Esp32TimerTickSource(const Esp32TimerTickSource&) = delete;
  Esp32TimerTickSource& operator=(const Esp32TimerTickSource&) = delete;
  Esp32TimerTickSource(Esp32TimerTickSource&&) = delete;
  Esp32TimerTickSource& operator=(Esp32TimerTickSource&&) = delete;

  /* ----------------------------- ITickSource ----------------------------- */

  void waitForNextTick() noexcept override {
    if (!running_) {
      return;
    }
#ifndef APEX_HAL_ESP32_MOCK
    // Block until timer callback sends a task notification
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif
    tickCount_ = tickCount_ + 1;
  }

  [[nodiscard]] uint32_t currentTick() const noexcept override { return tickCount_; }

  [[nodiscard]] uint32_t tickFrequency() const noexcept override { return frequency_; }

  void start() noexcept override {
    if (running_) {
      stop();
    }

#ifndef APEX_HAL_ESP32_MOCK
    // Capture the calling task handle for notification
    taskHandle_ = xTaskGetCurrentTaskHandle();

    // Create periodic esp_timer
    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = timerCallback;
    timerArgs.arg = this;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "exec_tick";

    esp_timer_create(&timerArgs, &timerHandle_);
    esp_timer_start_periodic(timerHandle_, periodUs_);
#endif

    tickCount_ = 0;
    running_ = true;
  }

  void stop() noexcept override {
    if (!running_) {
      return;
    }
    running_ = false;

#ifndef APEX_HAL_ESP32_MOCK
    if (timerHandle_ != nullptr) {
      esp_timer_stop(timerHandle_);
      esp_timer_delete(timerHandle_);
      timerHandle_ = nullptr;
    }
    taskHandle_ = nullptr;
#endif
  }

  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

private:
#ifndef APEX_HAL_ESP32_MOCK
  /**
   * @brief esp_timer callback (runs in esp_timer task context).
   *
   * ESP_TIMER_TASK dispatch runs callbacks from a dedicated FreeRTOS task,
   * not from ISR context. Use xTaskNotifyGive() (task-context API).
   *
   * @param arg Pointer to Esp32TimerTickSource instance.
   */
  static void timerCallback(void* arg) noexcept {
    auto* self = static_cast<Esp32TimerTickSource*>(arg);
    if (self != nullptr && self->running_ && self->taskHandle_ != nullptr) {
      xTaskNotifyGive(self->taskHandle_);
    }
  }
#endif

  /* ----------------------------- Members ----------------------------- */

  uint32_t frequency_;

#ifndef APEX_HAL_ESP32_MOCK
  uint32_t periodUs_;
  volatile uint32_t tickCount_ = 0;
  bool running_ = false;
  esp_timer_handle_t timerHandle_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
#else
  uint32_t tickCount_ = 0;
  bool running_ = false;
#endif
};

} // namespace esp32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_ESP32_TIMER_TICK_SOURCE_HPP
