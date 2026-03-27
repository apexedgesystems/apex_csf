#ifndef APEX_HAL_ESP32_USB_CDC_HPP
#define APEX_HAL_ESP32_USB_CDC_HPP
/**
 * @file Esp32UsbCdc.hpp
 * @brief ESP32-S3 USB CDC implementation using USB Serial/JTAG controller.
 *
 * Provides a USB CDC (virtual COM port) implementation of the IUart
 * interface using the ESP32-S3's built-in USB Serial/JTAG controller.
 *
 * The USB Serial/JTAG controller shares the internal USB PHY with the
 * USB OTG controller. When CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y is set
 * in sdkconfig, ESP-IDF routes the PHY to the USB Serial/JTAG controller,
 * providing a CDC serial port over the USB-C connection.
 *
 * This approach avoids the TinyUSB managed component dependency and uses
 * only the built-in ESP-IDF driver component.
 *
 * In mock mode (APEX_HAL_ESP32_MOCK), uses internal circular buffers
 * for TX and returns 0 from read(). No ESP-IDF dependencies required.
 *
 * Usage:
 *  1. Set CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y in sdkconfig.defaults
 *  2. Create instance and call init()
 *  3. Use read()/write() for binary I/O
 *
 * @note RT-safe after init: read()/write() are bounded-time.
 */

#include "src/system/core/hal/base/IUart.hpp"

#ifndef APEX_HAL_ESP32_MOCK
#include "driver/usb_serial_jtag.h"
#endif

namespace apex {
namespace hal {
namespace esp32 {

/* ----------------------------- Esp32UsbCdc ----------------------------- */

/**
 * @class Esp32UsbCdc
 * @brief ESP32-S3 USB Serial/JTAG CDC implementation of IUart.
 *
 * Wraps the ESP-IDF usb_serial_jtag driver for byte-level I/O.
 * The driver provides internal ring buffers for RX and TX.
 *
 * @tparam RxBufSize Size of USB Serial/JTAG RX ring buffer (bytes).
 * @tparam TxBufSize Size of USB Serial/JTAG TX ring buffer (bytes).
 */
template <size_t RxBufSize = 256, size_t TxBufSize = 256> class Esp32UsbCdc final : public IUart {
public:
  static_assert(RxBufSize >= 64, "RX buffer size must be >= 64");
  static_assert(TxBufSize >= 64, "TX buffer size must be >= 64");

  Esp32UsbCdc() noexcept = default;
  ~Esp32UsbCdc() override { deinit(); }

  Esp32UsbCdc(const Esp32UsbCdc&) = delete;
  Esp32UsbCdc& operator=(const Esp32UsbCdc&) = delete;
  Esp32UsbCdc(Esp32UsbCdc&&) = delete;
  Esp32UsbCdc& operator=(Esp32UsbCdc&&) = delete;

  /* ----------------------------- IUart Interface ----------------------------- */

  /**
   * @brief Install USB Serial/JTAG driver with ring buffers.
   * @param config Ignored (USB CDC does not use baud/parity/stop settings).
   * @return OK on success.
   */
  [[nodiscard]] UartStatus init(const UartConfig& /*config*/) noexcept override {
    if (initialized_) {
      return UartStatus::OK;
    }

#ifndef APEX_HAL_ESP32_MOCK
    usb_serial_jtag_driver_config_t drvConfig = {};
    drvConfig.rx_buffer_size = RxBufSize;
    drvConfig.tx_buffer_size = TxBufSize;

    esp_err_t err = usb_serial_jtag_driver_install(&drvConfig);
    if (err != ESP_OK) {
      return UartStatus::ERROR_NOT_INIT;
    }
#else
    txHead_ = 0;
    txTail_ = 0;
#endif

    initialized_ = true;
    return UartStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    if (initialized_) {
      usb_serial_jtag_driver_uninstall();
    }
#endif
    initialized_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /**
   * @brief Write bytes to USB Serial/JTAG TX buffer.
   * @param data Data to transmit.
   * @param len Number of bytes to transmit.
   * @return Number of bytes actually written.
   * @note RT-safe: Non-blocking write (timeout = 0).
   */
  size_t write(const uint8_t* data, size_t len) noexcept override {
    if (!initialized_ || data == nullptr || len == 0) {
      return 0;
    }

#ifndef APEX_HAL_ESP32_MOCK
    int written = usb_serial_jtag_write_bytes(reinterpret_cast<const char*>(data), len, 0);
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

  /**
   * @brief Read bytes from USB Serial/JTAG RX buffer.
   * @param buffer Destination buffer.
   * @param maxLen Maximum bytes to read.
   * @return Number of bytes actually read.
   * @note RT-safe: Non-blocking read (timeout = 0).
   */
  size_t read(uint8_t* buffer, size_t maxLen) noexcept override {
    if (!initialized_ || buffer == nullptr || maxLen == 0) {
      return 0;
    }

#ifndef APEX_HAL_ESP32_MOCK
    int bytesRead = usb_serial_jtag_read_bytes(buffer, maxLen, 0);
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
    // USB Serial/JTAG driver does not expose a buffered count query.
    // Return 0 (callers use read() with timeout=0 for non-blocking check).
    return 0;
  }

  [[nodiscard]] bool txReady() const noexcept override { return initialized_; }

  [[nodiscard]] bool txComplete() const noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    return initialized_;
#else
    return txHead_ == txTail_;
#endif
  }

  void flushRx() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    if (!initialized_) {
      return;
    }
    // Drain RX buffer
    uint8_t discard[64];
    while (usb_serial_jtag_read_bytes(discard, sizeof(discard), 0) > 0) {
    }
#endif
  }

  void flushTx() noexcept override {
    // TX is flushed automatically by the driver (real and mock)
  }

  [[nodiscard]] const UartStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
  bool initialized_ = false;
  UartStats stats_ = {};

#ifdef APEX_HAL_ESP32_MOCK
  uint8_t txBuf_[TxBufSize] = {};
  volatile size_t txHead_ = 0;
  volatile size_t txTail_ = 0;
#endif
};

} // namespace esp32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_ESP32_USB_CDC_HPP
