#ifndef APEX_HAL_PICO_UART_HPP
#define APEX_HAL_PICO_UART_HPP
/**
 * @file PicoUart.hpp
 * @brief RP2040 UART implementation using Pico SDK hardware_uart.
 *
 * Provides interrupt-driven UART with static circular buffers.
 * Follows the same pattern as Stm32Uart and AvrUart.
 *
 * Usage:
 *  1. Create instance with uart_inst_t* and GPIO pin numbers
 *  2. Call init() with UartConfig
 *  3. Wire IRQ handler from UART0_IRQ or UART1_IRQ
 *  4. Use read()/write() for I/O
 *
 * @code
 * static apex::hal::pico::PicoUart<512, 512> dataUart(uart0, 0, 1);
 *
 * // In IRQ setup
 * extern "C" void UART0_IRQ_Handler() { dataUart.irqHandler(); }
 *
 * // In main
 * apex::hal::UartConfig cfg;
 * cfg.baudRate = 115200;
 * dataUart.init(cfg);
 * @endcode
 *
 * @note RT-safe after init: read()/write() are bounded-time.
 */

#include "src/system/core/hal/base/IUart.hpp"

#ifndef APEX_HAL_PICO_MOCK
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#endif

namespace apex {
namespace hal {
namespace pico {

/* ----------------------------- PicoUart ----------------------------- */

/**
 * @class PicoUart
 * @brief RP2040 UART implementation with static circular buffers.
 *
 * @tparam RxBufSize Size of RX circular buffer (bytes).
 * @tparam TxBufSize Size of TX circular buffer (bytes).
 *
 * Memory usage: RxBufSize + TxBufSize + ~32 bytes overhead.
 */
template <size_t RxBufSize = 256, size_t TxBufSize = 256> class PicoUart final : public IUart {
public:
  static_assert(RxBufSize > 0, "RX buffer size must be > 0");
  static_assert(TxBufSize > 0, "TX buffer size must be > 0");
  static_assert(RxBufSize <= 4096, "RX buffer size too large");
  static_assert(TxBufSize <= 4096, "TX buffer size too large");

#ifndef APEX_HAL_PICO_MOCK
  /**
   * @brief Construct UART wrapper.
   * @param inst UART instance (uart0 or uart1).
   * @param txPin GPIO number for TX.
   * @param rxPin GPIO number for RX.
   * @note NOT RT-safe: Construction only.
   */
  PicoUart(uart_inst_t* inst, uint txPin, uint rxPin) noexcept
      : inst_(inst), txPin_(txPin), rxPin_(rxPin) {}
#else
  /**
   * @brief Mock constructor for host-side testing.
   */
  PicoUart() noexcept = default;
#endif

  ~PicoUart() override { deinit(); }

  PicoUart(const PicoUart&) = delete;
  PicoUart& operator=(const PicoUart&) = delete;
  PicoUart(PicoUart&&) = delete;
  PicoUart& operator=(PicoUart&&) = delete;

  /* ----------------------------- IUart Interface ----------------------------- */

  [[nodiscard]] UartStatus init(const UartConfig& config) noexcept override {
    if (initialized_) {
      deinit();
    }

#ifndef APEX_HAL_PICO_MOCK
    // Initialize UART hardware
    uart_init(inst_, config.baudRate);

    // Configure GPIO pins for UART function
    gpio_set_function(txPin_, GPIO_FUNC_UART);
    gpio_set_function(rxPin_, GPIO_FUNC_UART);

    // Configure data format
    uint dataBits = config.dataBits;
    uint stopBits = (config.stopBits == UartStopBits::TWO) ? 2 : 1;
    uart_parity_t parity = UART_PARITY_NONE;
    if (config.parity == UartParity::ODD) {
      parity = UART_PARITY_ODD;
    } else if (config.parity == UartParity::EVEN) {
      parity = UART_PARITY_EVEN;
    }
    uart_set_format(inst_, dataBits, stopBits, parity);

    // Enable hardware flow control if requested
    uart_set_hw_flow(inst_, config.hwFlowControl, config.hwFlowControl);

    // Disable FIFO (character-by-character interrupt)
    uart_set_fifo_enabled(inst_, false);

    // Enable RX interrupt
    const uint UART_IDX = uart_get_index(inst_);
    const uint IRQ_NUM = (UART_IDX == 0) ? UART0_IRQ : UART1_IRQ;
    irq_set_enabled(IRQ_NUM, true);
    uart_set_irq_enables(inst_, true, false);
#else
    (void)config;
#endif

    initialized_ = true;
    return UartStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_PICO_MOCK
    if (initialized_) {
      const uint UART_IDX = uart_get_index(inst_);
      const uint IRQ_NUM = (UART_IDX == 0) ? UART0_IRQ : UART1_IRQ;
      uart_set_irq_enables(inst_, false, false);
      irq_set_enabled(IRQ_NUM, false);
      uart_deinit(inst_);
    }
#endif
    initialized_ = false;
    rxHead_ = 0;
    rxTail_ = 0;
    txHead_ = 0;
    txTail_ = 0;
    txActive_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  size_t write(const uint8_t* data, size_t len) noexcept override {
    if (!initialized_ || data == nullptr || len == 0) {
      return 0;
    }

    size_t written = 0;

    // Copy data to TX buffer
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

#ifndef APEX_HAL_PICO_MOCK
    // Start transmission if not already active
    if (written > 0 && !txActive_) {
      txActive_ = true;
      uart_set_irq_enables(inst_, true, true);
    }
#endif

    return written;
  }

  size_t read(uint8_t* buffer, size_t maxLen) noexcept override {
    if (!initialized_ || buffer == nullptr || maxLen == 0) {
      return 0;
    }

    size_t bytesRead = 0;

    while (bytesRead < maxLen && rxTail_ != rxHead_) {
      buffer[bytesRead] = rxBuf_[rxTail_];
      rxTail_ = (rxTail_ + 1) % RxBufSize;
      ++bytesRead;
    }

    return bytesRead;
  }

  [[nodiscard]] size_t available() const noexcept override {
    if (rxHead_ >= rxTail_) {
      return rxHead_ - rxTail_;
    }
    return RxBufSize - rxTail_ + rxHead_;
  }

  [[nodiscard]] bool txReady() const noexcept override {
    size_t nextHead = (txHead_ + 1) % TxBufSize;
    return nextHead != txTail_;
  }

  [[nodiscard]] bool txComplete() const noexcept override {
#ifndef APEX_HAL_PICO_MOCK
    return !txActive_ && (txHead_ == txTail_) && uart_is_writable(inst_);
#else
    return !txActive_ && (txHead_ == txTail_);
#endif
  }

  void flushRx() noexcept override {
    rxHead_ = 0;
    rxTail_ = 0;
  }

  void flushTx() noexcept override {
#ifndef APEX_HAL_PICO_MOCK
    while (!txComplete()) {
      tight_loop_contents();
    }
#endif
  }

  [[nodiscard]] const UartStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

  /* ----------------------------- IRQ Handler ----------------------------- */

  /**
   * @brief Handle UART interrupt.
   *
   * Call this from the UART0_IRQ or UART1_IRQ handler.
   *
   * @code
   * extern "C" void UART0_IRQ_Handler() { dataUart.irqHandler(); }
   * @endcode
   *
   * @note Must be called from interrupt context.
   */
  void irqHandler() noexcept {
#ifndef APEX_HAL_PICO_MOCK
    // RX: read all available characters
    while (uart_is_readable(inst_)) {
      uint8_t data = static_cast<uint8_t>(uart_getc(inst_));
      size_t nextHead = (rxHead_ + 1) % RxBufSize;

      if (nextHead != rxTail_) {
        rxBuf_[rxHead_] = data;
        rxHead_ = nextHead;
        ++stats_.bytesRx;
      } else {
        ++stats_.overrunErrors;
      }
    }

    // TX: send next byte if UART is writable
    if (uart_is_writable(inst_)) {
      if (txTail_ != txHead_) {
        uart_putc_raw(inst_, txBuf_[txTail_]);
        txTail_ = (txTail_ + 1) % TxBufSize;
      } else {
        // TX buffer empty, disable TX interrupt
        uart_set_irq_enables(inst_, true, false);
        txActive_ = false;
      }
    }
#endif
  }

  /* ----------------------------- Buffer Info ----------------------------- */

  [[nodiscard]] static constexpr size_t rxCapacity() noexcept { return RxBufSize - 1; }
  [[nodiscard]] static constexpr size_t txCapacity() noexcept { return TxBufSize - 1; }

private:
#ifndef APEX_HAL_PICO_MOCK
  uart_inst_t* inst_;
  uint txPin_;
  uint rxPin_;
#endif

  bool initialized_ = false;
  volatile bool txActive_ = false;

  uint8_t rxBuf_[RxBufSize] = {};
  volatile size_t rxHead_ = 0;
  volatile size_t rxTail_ = 0;

  uint8_t txBuf_[TxBufSize] = {};
  volatile size_t txHead_ = 0;
  volatile size_t txTail_ = 0;

  UartStats stats_ = {};
};

} // namespace pico
} // namespace hal
} // namespace apex

#endif // APEX_HAL_PICO_UART_HPP
