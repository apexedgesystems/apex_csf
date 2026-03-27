#ifndef APEX_HAL_PICO_USB_CDC_HPP
#define APEX_HAL_PICO_USB_CDC_HPP
/**
 * @file PicoUsbCdc.hpp
 * @brief RP2040 USB CDC implementation using TinyUSB.
 *
 * Provides a USB CDC (virtual COM port) implementation of the IUart
 * interface. Allows USB to be used as a serial channel with the same
 * API as hardware UART, enabling drop-in replacement.
 *
 * Relies on stdio_init_all() having been called first, which initializes
 * TinyUSB and starts the background tud_task() timer. This class is a
 * thin wrapper around tud_cdc_*() for raw binary I/O.
 *
 * Usage:
 *  1. Enable stdio_usb in CMakeLists (provides USB descriptors + TinyUSB)
 *  2. Call stdio_init_all() in main (initializes USB stack)
 *  3. Create instance and call init()
 *  4. Use read()/write() for binary I/O (do not mix with printf/getchar)
 *
 * @code
 * // In main, BEFORE cmdUart.init():
 * stdio_init_all();
 *
 * static apex::hal::pico::PicoUsbCdc<128, 128> cmdUart;
 * apex::hal::UartConfig cfg;
 * cmdUart.init(cfg);  // baud rate ignored for USB
 * @endcode
 *
 * @note RT-safe after init: read()/write() are bounded-time.
 * @note Requires pico_enable_stdio_usb(target 1) in CMakeLists.
 */

#include "src/system/core/hal/base/IUart.hpp"

#ifndef APEX_HAL_PICO_MOCK
#include "tusb.h"
#endif

namespace apex {
namespace hal {
namespace pico {

/* ----------------------------- PicoUsbCdc ----------------------------- */

/**
 * @class PicoUsbCdc
 * @brief RP2040 USB CDC implementation of IUart.
 *
 * Thin wrapper around TinyUSB CDC device functions. USB stack
 * initialization and background processing are handled by the Pico
 * SDK's stdio_usb layer (via stdio_init_all).
 *
 * Mock mode: write() queues to an internal TX circular buffer,
 * read() returns 0 (no data). Allows host-side compilation and testing.
 *
 * @tparam RxBufSize Unused on real hardware (TinyUSB manages its own
 *         buffers). Used for mock TX buffer. Kept for template
 *         compatibility with PicoUart.
 * @tparam TxBufSize Unused on real hardware (TinyUSB manages its own
 *         buffers). Used for mock TX buffer.
 */
template <size_t RxBufSize = 256, size_t TxBufSize = 256> class PicoUsbCdc final : public IUart {
public:
  PicoUsbCdc() noexcept = default;
  ~PicoUsbCdc() override { deinit(); }

  PicoUsbCdc(const PicoUsbCdc&) = delete;
  PicoUsbCdc& operator=(const PicoUsbCdc&) = delete;
  PicoUsbCdc(PicoUsbCdc&&) = delete;
  PicoUsbCdc& operator=(PicoUsbCdc&&) = delete;

  /* ----------------------------- IUart Interface ----------------------------- */

  /**
   * @brief Mark USB CDC as ready for I/O.
   * @param config Ignored (USB CDC does not use baud/parity/stop settings).
   * @return OK on success.
   * @note Requires stdio_init_all() to have been called first.
   */
  [[nodiscard]] UartStatus init(const UartConfig& /*config*/) noexcept override {
    initialized_ = true;
    return UartStatus::OK;
  }

  void deinit() noexcept override {
    initialized_ = false;
#ifdef APEX_HAL_PICO_MOCK
    txHead_ = 0;
    txTail_ = 0;
#endif
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /**
   * @brief Write bytes to USB CDC TX endpoint.
   * @param data Data to transmit.
   * @param len Number of bytes to transmit.
   * @return Number of bytes actually queued for transmission.
   * @note RT-safe: Copies to TinyUSB TX FIFO, returns immediately.
   */
  size_t write(const uint8_t* data, size_t len) noexcept override {
    if (!initialized_ || data == nullptr || len == 0) {
      return 0;
    }

#ifndef APEX_HAL_PICO_MOCK
    if (!tud_cdc_connected()) {
      return 0;
    }

    uint32_t written = tud_cdc_write(data, static_cast<uint32_t>(len));
    tud_cdc_write_flush();

    stats_.bytesTx += written;
    return written;
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
    stats_.bytesTx += written;
    return written;
#endif
  }

  /**
   * @brief Read bytes from USB CDC RX endpoint.
   * @param buffer Destination buffer.
   * @param maxLen Maximum bytes to read.
   * @return Number of bytes actually read.
   * @note RT-safe: Copies from TinyUSB RX FIFO, returns immediately.
   */
  size_t read(uint8_t* buffer, size_t maxLen) noexcept override {
    if (!initialized_ || buffer == nullptr || maxLen == 0) {
      return 0;
    }

#ifndef APEX_HAL_PICO_MOCK
    uint32_t bytesRead = tud_cdc_read(buffer, static_cast<uint32_t>(maxLen));
    stats_.bytesRx += bytesRead;
    return bytesRead;
#else
    (void)buffer;
    (void)maxLen;
    return 0;
#endif
  }

  [[nodiscard]] size_t available() const noexcept override {
    if (!initialized_) {
      return 0;
    }
#ifndef APEX_HAL_PICO_MOCK
    return tud_cdc_available();
#else
    return 0;
#endif
  }

  [[nodiscard]] bool txReady() const noexcept override {
    if (!initialized_) {
      return false;
    }
#ifndef APEX_HAL_PICO_MOCK
    return tud_cdc_write_available() > 0;
#else
    size_t nextHead = (txHead_ + 1) % TxBufSize;
    return nextHead != txTail_;
#endif
  }

  [[nodiscard]] bool txComplete() const noexcept override {
    if (!initialized_) {
      return true;
    }
#ifndef APEX_HAL_PICO_MOCK
    return !tud_cdc_connected() || (tud_cdc_write_available() == CFG_TUD_CDC_TX_BUFSIZE);
#else
    return txHead_ == txTail_;
#endif
  }

  void flushRx() noexcept override {
    if (!initialized_) {
      return;
    }
#ifndef APEX_HAL_PICO_MOCK
    tud_cdc_read_flush();
#endif
  }

  void flushTx() noexcept override {
    if (!initialized_) {
      return;
    }
#ifndef APEX_HAL_PICO_MOCK
    tud_cdc_write_flush();
#endif
  }

  [[nodiscard]] const UartStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
  bool initialized_ = false;
  UartStats stats_ = {};

#ifdef APEX_HAL_PICO_MOCK
  /* ----------------------------- Mock Buffers ----------------------------- */

  uint8_t txBuf_[TxBufSize] = {};
  size_t txHead_ = 0;
  size_t txTail_ = 0;
#endif
};

} // namespace pico
} // namespace hal
} // namespace apex

#endif // APEX_HAL_PICO_USB_CDC_HPP
