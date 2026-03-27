#ifndef APEX_HAL_ESP32_UART_HPP
#define APEX_HAL_ESP32_UART_HPP
/**
 * @file Esp32Uart.hpp
 * @brief ESP32-S3 UART implementation using ESP-IDF driver.
 *
 * Wraps the ESP-IDF UART driver for hardware UART peripherals.
 * The ESP-IDF driver manages its own ring buffers internally
 * (via uart_driver_install), so we delegate buffering to it
 * rather than maintaining our own circular buffers.
 *
 * In mock mode (APEX_HAL_ESP32_MOCK), uses internal circular buffers
 * for TX and returns 0 from read(). No ESP-IDF dependencies required.
 *
 * Usage:
 *  1. Create instance with UART port number and GPIO pins
 *  2. Call init() with UartConfig
 *  3. Use read()/write() for I/O
 *
 * @code
 * static apex::hal::esp32::Esp32Uart dataUart(UART_NUM_0, GPIO_NUM_43, GPIO_NUM_44);
 *
 * apex::hal::UartConfig cfg;
 * cfg.baudRate = 115200;
 * dataUart.init(cfg);
 * @endcode
 *
 * @note RT-safe after init: read()/write() are bounded-time.
 */

#include "src/system/core/hal/base/IUart.hpp"

#ifndef APEX_HAL_ESP32_MOCK
#include "driver/uart.h"
#include "driver/gpio.h"
#endif

namespace apex {
namespace hal {
namespace esp32 {

/* ----------------------------- Esp32Uart ----------------------------- */

/**
 * @class Esp32Uart
 * @brief ESP32-S3 UART implementation using ESP-IDF driver.
 *
 * @tparam RxBufSize Size of ESP-IDF RX ring buffer (bytes).
 * @tparam TxBufSize Size of ESP-IDF TX ring buffer (bytes).
 *
 * Memory usage: Managed by ESP-IDF UART driver (heap-allocated ring buffers).
 * In mock mode, uses static TX circular buffer of TxBufSize bytes.
 */
template <size_t RxBufSize = 512, size_t TxBufSize = 512> class Esp32Uart final : public IUart {
public:
  static_assert(RxBufSize >= 128, "RX buffer size must be >= 128");
  static_assert(TxBufSize >= 128, "TX buffer size must be >= 128");

#ifndef APEX_HAL_ESP32_MOCK
  /**
   * @brief Construct UART wrapper.
   * @param port UART port number (UART_NUM_0, UART_NUM_1, UART_NUM_2).
   * @param txPin GPIO number for TX.
   * @param rxPin GPIO number for RX.
   * @note NOT RT-safe: Construction only.
   */
  Esp32Uart(uart_port_t port, gpio_num_t txPin, gpio_num_t rxPin) noexcept
      : port_(port), txPin_(txPin), rxPin_(rxPin) {}
#else
  /**
   * @brief Mock constructor for host-side testing.
   */
  Esp32Uart() noexcept = default;
#endif

  ~Esp32Uart() override { deinit(); }

  Esp32Uart(const Esp32Uart&) = delete;
  Esp32Uart& operator=(const Esp32Uart&) = delete;
  Esp32Uart(Esp32Uart&&) = delete;
  Esp32Uart& operator=(Esp32Uart&&) = delete;

  /* ----------------------------- IUart Interface ----------------------------- */

  [[nodiscard]] UartStatus init(const UartConfig& config) noexcept override {
    if (initialized_) {
      deinit();
    }

#ifndef APEX_HAL_ESP32_MOCK
    // Configure UART parameters
    uart_config_t uartConfig = {};
    uartConfig.baud_rate = static_cast<int>(config.baudRate);
    uartConfig.data_bits = (config.dataBits == 7) ? UART_DATA_7_BITS : UART_DATA_8_BITS;

    if (config.parity == UartParity::ODD) {
      uartConfig.parity = UART_PARITY_ODD;
    } else if (config.parity == UartParity::EVEN) {
      uartConfig.parity = UART_PARITY_EVEN;
    } else {
      uartConfig.parity = UART_PARITY_DISABLE;
    }

    uartConfig.stop_bits =
        (config.stopBits == UartStopBits::TWO) ? UART_STOP_BITS_2 : UART_STOP_BITS_1;

    uartConfig.flow_ctrl =
        config.hwFlowControl ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    uartConfig.source_clk = UART_SCLK_DEFAULT;

    // Remove any pre-existing driver (e.g. ESP-IDF boot console on UART0).
    // uart_driver_delete() returns ESP_ERR_INVALID_ARG if no driver is
    // installed, which is harmless.
    static_cast<void>(uart_driver_delete(port_));

    // Install driver with ring buffers
    esp_err_t err = uart_driver_install(port_, static_cast<int>(RxBufSize),
                                        static_cast<int>(TxBufSize), 0, nullptr, 0);
    if (err != ESP_OK) {
      return UartStatus::ERROR_NOT_INIT;
    }

    err = uart_param_config(port_, &uartConfig);
    if (err != ESP_OK) {
      uart_driver_delete(port_);
      return UartStatus::ERROR_INVALID_ARG;
    }

    // Set GPIO pins
    err = uart_set_pin(port_, static_cast<int>(txPin_), static_cast<int>(rxPin_),
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
      uart_driver_delete(port_);
      return UartStatus::ERROR_INVALID_ARG;
    }
#else
    (void)config;
    txHead_ = 0;
    txTail_ = 0;
#endif

    initialized_ = true;
    return UartStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    if (initialized_) {
      uart_driver_delete(port_);
    }
#endif
    initialized_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  size_t write(const uint8_t* data, size_t len) noexcept override {
    if (!initialized_ || data == nullptr || len == 0) {
      return 0;
    }

#ifndef APEX_HAL_ESP32_MOCK
    int written = uart_write_bytes(port_, data, len);
    if (written < 0) {
      return 0;
    }

    stats_.bytesTx += static_cast<uint32_t>(written);
    return static_cast<size_t>(written);
#else
    size_t written = 0;
    while (written < len) {
      size_t nextHead = (txHead_ + 1) % TxBufSize;
      if (nextHead == txTail_) {
        break;
      }
      txBuf_[txHead_] = data[written];
      txHead_ = nextHead;
      ++written;
    }
    stats_.bytesTx += static_cast<uint32_t>(written);
    return written;
#endif
  }

  size_t read(uint8_t* buffer, size_t maxLen) noexcept override {
    if (!initialized_ || buffer == nullptr || maxLen == 0) {
      return 0;
    }

#ifndef APEX_HAL_ESP32_MOCK
    // Non-blocking read (timeout = 0)
    int bytesRead = uart_read_bytes(port_, buffer, maxLen, 0);
    if (bytesRead < 0) {
      return 0;
    }

    stats_.bytesRx += static_cast<uint32_t>(bytesRead);
    return static_cast<size_t>(bytesRead);
#else
    return 0;
#endif
  }

  [[nodiscard]] size_t available() const noexcept override {
    if (!initialized_) {
      return 0;
    }

#ifndef APEX_HAL_ESP32_MOCK
    size_t buffered = 0;
    uart_get_buffered_data_len(port_, &buffered);
    return buffered;
#else
    return 0;
#endif
  }

  [[nodiscard]] bool txReady() const noexcept override { return initialized_; }

  [[nodiscard]] bool txComplete() const noexcept override {
    if (!initialized_) {
      return true;
    }
#ifndef APEX_HAL_ESP32_MOCK
    return uart_wait_tx_done(port_, 0) == ESP_OK;
#else
    return txHead_ == txTail_;
#endif
  }

  void flushRx() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    if (initialized_) {
      uart_flush_input(port_);
    }
#endif
  }

  void flushTx() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    if (initialized_) {
      uart_wait_tx_done(port_, portMAX_DELAY);
    }
#endif
  }

  [[nodiscard]] const UartStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
#ifndef APEX_HAL_ESP32_MOCK
  uart_port_t port_;
  gpio_num_t txPin_;
  gpio_num_t rxPin_;
#else
  uint8_t txBuf_[TxBufSize] = {};
  volatile size_t txHead_ = 0;
  volatile size_t txTail_ = 0;
#endif

  bool initialized_ = false;
  UartStats stats_ = {};
};

} // namespace esp32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_ESP32_UART_HPP
