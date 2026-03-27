#ifndef APEX_HAL_AVR_UART_HPP
#define APEX_HAL_AVR_UART_HPP
/**
 * @file AvrUart.hpp
 * @brief ATmega328P USART0 implementation of IUart.
 *
 * Interrupt-driven UART with static circular buffers for the ATmega328P.
 * Uses USART0 (the only hardware USART on this MCU). RX and TX are
 * ISR-driven using USART_RX_vect and USART_UDRE_vect.
 *
 * The Arduino Uno R3 shares USART0 with the ATmega16U2 USB-serial bridge
 * on pins D0 (RXD) and D1 (TXD). External devices on these pins must be
 * disconnected during firmware upload.
 *
 * Usage:
 * @code
 * static AvrUart<128, 128> uart;
 * apex::hal::UartConfig cfg;
 * cfg.baudRate = 115200;
 * uart.init(cfg);
 * @endcode
 *
 * @note ISR linkage: This file defines ISR(USART_RX_vect) and
 *       ISR(USART_UDRE_vect) via a static instance pointer. Only one
 *       AvrUart instance is supported (ATmega328P has one USART).
 */

#include "src/system/core/hal/base/IUart.hpp"

#ifndef APEX_HAL_AVR_MOCK
#include <avr/interrupt.h>
#include <avr/io.h>
#else
#ifndef F_CPU
#define F_CPU 16000000UL
#endif
#endif

namespace apex {
namespace hal {
namespace avr {

/* ----------------------------- AvrUart ----------------------------- */

/**
 * @class AvrUart
 * @brief ATmega328P USART0 implementation with static circular buffers.
 *
 * @tparam RxBufSize Size of RX circular buffer (bytes).
 * @tparam TxBufSize Size of TX circular buffer (bytes).
 *
 * Memory usage: RxBufSize + TxBufSize + ~20 bytes overhead.
 */
template <size_t RxBufSize = 128, size_t TxBufSize = 128> class AvrUart final : public IUart {
public:
  static_assert(RxBufSize > 0, "RX buffer size must be > 0");
  static_assert(TxBufSize > 0, "TX buffer size must be > 0");
  static_assert(RxBufSize <= 256, "RX buffer too large for AVR SRAM");
  static_assert(TxBufSize <= 256, "TX buffer too large for AVR SRAM");

  AvrUart() noexcept = default;
  ~AvrUart() override { deinit(); }

  AvrUart(const AvrUart&) = delete;
  AvrUart& operator=(const AvrUart&) = delete;
  AvrUart(AvrUart&&) = delete;
  AvrUart& operator=(AvrUart&&) = delete;

  /* ----------------------------- IUart Interface ----------------------------- */

  [[nodiscard]] UartStatus init(const UartConfig& config) noexcept override {
    if (initialized_) {
      deinit();
    }

#ifndef APEX_HAL_AVR_MOCK
    // Set static instance pointer for ISR linkage
    instance_ = this;

    // U2X mode (8x oversampling) with rounding for better baud accuracy.
    // At 16 MHz / 115200: normal mode gives UBRR=7 (+8.5% error, broken),
    // U2X gives UBRR=16 (+2.1% error, within tolerance).
    UCSR0A = (1 << U2X0);
    const uint16_t UBRR_VAL =
        static_cast<uint16_t>(((F_CPU + 4UL * config.baudRate) / (8UL * config.baudRate)) - 1);
    UBRR0H = static_cast<uint8_t>(UBRR_VAL >> 8);
    UBRR0L = static_cast<uint8_t>(UBRR_VAL & 0xFF);

    // Frame format: 8N1 (default)
    uint8_t ucsrc = (1 << UCSZ01) | (1 << UCSZ00); // 8 data bits

    if (config.parity == UartParity::EVEN) {
      ucsrc |= (1 << UPM01);
    } else if (config.parity == UartParity::ODD) {
      ucsrc |= (1 << UPM01) | (1 << UPM00);
    }

    if (config.stopBits == UartStopBits::TWO) {
      ucsrc |= (1 << USBS0);
    }

    UCSR0C = ucsrc;

    // Enable TX, RX, and RX complete interrupt
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);

    // Clear buffers
    rxHead_ = 0;
    rxTail_ = 0;
    txHead_ = 0;
    txTail_ = 0;
    txActive_ = false;

    // Enable global interrupts
    sei();
#else
    (void)config;
    rxHead_ = 0;
    rxTail_ = 0;
    txHead_ = 0;
    txTail_ = 0;
    txActive_ = false;
#endif

    initialized_ = true;
    return UartStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_AVR_MOCK
    if (initialized_) {
      // Disable TX, RX, and all USART interrupts
      UCSR0B = 0;
      instance_ = nullptr;
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

#ifndef APEX_HAL_AVR_MOCK
    for (size_t i = 0; i < len; ++i) {
      uint8_t nextHead = static_cast<uint8_t>((txHead_ + 1) % TxBufSize);
      // Spin-wait if buffer full (UDRE ISR drains asynchronously)
      while (nextHead == txTail_) {
        UCSR0B |= (1 << UDRIE0);
      }
      txBuf_[txHead_] = data[i];
      txHead_ = nextHead;
    }

    stats_.bytesTx += static_cast<uint32_t>(len);

    // Enable UDRE interrupt to start transmission
    if (!txActive_) {
      txActive_ = true;
      UCSR0B |= (1 << UDRIE0);
    }

    return len;
#else
    // Mock: queue to TX buffer (non-blocking, return what fits)
    size_t written = 0;
    while (written < len) {
      uint8_t nextHead = static_cast<uint8_t>((txHead_ + 1) % TxBufSize);
      if (nextHead == txTail_) {
        break; // Buffer full
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

    size_t bytesRead = 0;

    while (bytesRead < maxLen && rxTail_ != rxHead_) {
      buffer[bytesRead] = rxBuf_[rxTail_];
      rxTail_ = static_cast<uint8_t>((rxTail_ + 1) % RxBufSize);
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
    uint8_t nextHead = static_cast<uint8_t>((txHead_ + 1) % TxBufSize);
    return nextHead != txTail_;
  }

  [[nodiscard]] bool txComplete() const noexcept override {
#ifndef APEX_HAL_AVR_MOCK
    return !txActive_ && (txHead_ == txTail_) && (UCSR0A & (1 << TXC0));
#else
    return txHead_ == txTail_;
#endif
  }

  void flushRx() noexcept override {
    rxHead_ = 0;
    rxTail_ = 0;
  }

  void flushTx() noexcept override {
#ifndef APEX_HAL_AVR_MOCK
    while (!txComplete()) {
      // Spin wait
    }
#endif
  }

  [[nodiscard]] const UartStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

  /* ----------------------------- ISR Handlers ----------------------------- */

#ifndef APEX_HAL_AVR_MOCK
  /**
   * @brief Handle RX complete interrupt.
   * @note Called from ISR(USART_RX_vect). Do not call directly.
   */
  void rxIsr() noexcept {
    // Check for errors before reading data
    const uint8_t STATUS = UCSR0A;

    if (STATUS & (1 << FE0)) {
      ++stats_.framingErrors;
    }
    if (STATUS & (1 << DOR0)) {
      ++stats_.overrunErrors;
    }
    if (STATUS & (1 << UPE0)) {
      ++stats_.parityErrors;
    }

    const uint8_t DATA = UDR0;
    const uint8_t NEXT_HEAD = static_cast<uint8_t>((rxHead_ + 1) % RxBufSize);

    if (NEXT_HEAD != rxTail_) {
      rxBuf_[rxHead_] = DATA;
      rxHead_ = NEXT_HEAD;
      ++stats_.bytesRx;
    } else {
      ++stats_.overrunErrors; // Buffer overrun
    }
  }

  /**
   * @brief Handle UDRE (data register empty) interrupt for TX.
   * @note Called from ISR(USART_UDRE_vect). Do not call directly.
   */
  void udreIsr() noexcept {
    if (txTail_ != txHead_) {
      UDR0 = txBuf_[txTail_];
      txTail_ = static_cast<uint8_t>((txTail_ + 1) % TxBufSize);
    } else {
      // TX buffer empty, disable UDRE interrupt
      UCSR0B &= ~(1 << UDRIE0);
      txActive_ = false;
    }
  }
#endif

  /* ----------------------------- Buffer Info ----------------------------- */

  [[nodiscard]] static constexpr size_t rxCapacity() noexcept { return RxBufSize - 1; }
  [[nodiscard]] static constexpr size_t txCapacity() noexcept { return TxBufSize - 1; }

  /* ----------------------------- Static ISR Linkage ----------------------------- */

#ifndef APEX_HAL_AVR_MOCK
  /**
   * @brief Static instance pointer for ISR dispatch.
   *
   * AVR ISRs are free functions (not class members). This static pointer
   * allows the ISR to dispatch to the active AvrUart instance. Only one
   * instance is supported (ATmega328P has one USART).
   */
  static AvrUart* instance_;
#endif

private:
  bool initialized_ = false;
  volatile bool txActive_ = false;

  uint8_t rxBuf_[RxBufSize] = {};
  volatile uint8_t rxHead_ = 0;
  volatile uint8_t rxTail_ = 0;

  uint8_t txBuf_[TxBufSize] = {};
  volatile uint8_t txHead_ = 0;
  volatile uint8_t txTail_ = 0;

  UartStats stats_ = {};
};

/* ----------------------------- Static Member Definition ----------------------------- */

#ifndef APEX_HAL_AVR_MOCK
template <size_t RxBufSize, size_t TxBufSize>
AvrUart<RxBufSize, TxBufSize>* AvrUart<RxBufSize, TxBufSize>::instance_ = nullptr;
#endif

} // namespace avr
} // namespace hal
} // namespace apex

#endif // APEX_HAL_AVR_UART_HPP
