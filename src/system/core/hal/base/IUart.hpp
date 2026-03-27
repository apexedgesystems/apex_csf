#ifndef APEX_HAL_IUART_HPP
#define APEX_HAL_IUART_HPP
/**
 * @file IUart.hpp
 * @brief Abstract UART interface for embedded systems.
 *
 * Platform-agnostic UART interface designed for MCUs and embedded Linux.
 * Unlike the protocols/serial/uart/ library (which uses POSIX file descriptors),
 * this interface has no OS dependencies and can be implemented on bare-metal.
 *
 * Design principles:
 *  - No heap allocation
 *  - No POSIX dependencies (no fd, no termios)
 *  - Static buffers managed by implementation
 *  - Suitable for interrupt-driven or DMA operation
 *
 * Implementations:
 *  - Stm32Uart (STM32 HAL/LL)
 *  - PicoUart (RP2040 Pico SDK)
 *  - Esp32Uart (ESP-IDF)
 */

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace hal {

/* ----------------------------- UartParity ----------------------------- */

/**
 * @brief UART parity setting.
 */
enum class UartParity : uint8_t { NONE = 0, ODD = 1, EVEN = 2 };

/* ----------------------------- UartStopBits ----------------------------- */

/**
 * @brief UART stop bits setting.
 */
enum class UartStopBits : uint8_t { ONE = 0, TWO = 1 };

/* ----------------------------- UartStatus ----------------------------- */

/**
 * @brief Status codes for UART operations.
 */
enum class UartStatus : uint8_t {
  OK = 0,           ///< Operation succeeded.
  WOULD_BLOCK,      ///< No data available (non-blocking read).
  BUSY,             ///< Peripheral busy (TX in progress).
  ERROR_TIMEOUT,    ///< Operation timed out.
  ERROR_OVERRUN,    ///< RX buffer overrun.
  ERROR_FRAMING,    ///< Framing error detected.
  ERROR_PARITY,     ///< Parity error detected.
  ERROR_NOISE,      ///< Noise detected on line.
  ERROR_NOT_INIT,   ///< UART not initialized.
  ERROR_INVALID_ARG ///< Invalid argument.
};

/**
 * @brief Convert UartStatus to string.
 * @param s Status value.
 * @return Human-readable string.
 * @note RT-safe: Returns static string literal.
 */
inline const char* toString(UartStatus s) noexcept {
  switch (s) {
  case UartStatus::OK:
    return "OK";
  case UartStatus::WOULD_BLOCK:
    return "WOULD_BLOCK";
  case UartStatus::BUSY:
    return "BUSY";
  case UartStatus::ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case UartStatus::ERROR_OVERRUN:
    return "ERROR_OVERRUN";
  case UartStatus::ERROR_FRAMING:
    return "ERROR_FRAMING";
  case UartStatus::ERROR_PARITY:
    return "ERROR_PARITY";
  case UartStatus::ERROR_NOISE:
    return "ERROR_NOISE";
  case UartStatus::ERROR_NOT_INIT:
    return "ERROR_NOT_INIT";
  case UartStatus::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- UartConfig ----------------------------- */

/**
 * @brief UART configuration parameters.
 */
struct UartConfig {
  uint32_t baudRate = 115200;                ///< Baud rate in bits per second.
  uint8_t dataBits = 8;                      ///< Data bits (7 or 8).
  UartParity parity = UartParity::NONE;      ///< Parity setting.
  UartStopBits stopBits = UartStopBits::ONE; ///< Stop bits.
  bool hwFlowControl = false;                ///< Enable RTS/CTS hardware flow control.
};

/* ----------------------------- UartStats ----------------------------- */

/**
 * @brief UART statistics for monitoring.
 */
struct UartStats {
  uint32_t bytesRx = 0;       ///< Total bytes received.
  uint32_t bytesTx = 0;       ///< Total bytes transmitted.
  uint32_t overrunErrors = 0; ///< RX overrun count.
  uint32_t framingErrors = 0; ///< Framing error count.
  uint32_t parityErrors = 0;  ///< Parity error count.
  uint32_t noiseErrors = 0;   ///< Noise error count.

  /**
   * @brief Reset all counters to zero.
   */
  void reset() noexcept {
    bytesRx = 0;
    bytesTx = 0;
    overrunErrors = 0;
    framingErrors = 0;
    parityErrors = 0;
    noiseErrors = 0;
  }

  /**
   * @brief Get total error count.
   * @return Sum of all error counters.
   */
  [[nodiscard]] uint32_t totalErrors() const noexcept {
    return overrunErrors + framingErrors + parityErrors + noiseErrors;
  }
};

/* ----------------------------- IUart ----------------------------- */

/**
 * @class IUart
 * @brief Abstract UART interface for embedded systems.
 *
 * Provides a common API for UART communication across different MCU platforms.
 * Implementations manage their own buffers and interrupt/DMA configuration.
 *
 * Lifecycle:
 *  1. Construct implementation (platform-specific)
 *  2. Call init() with configuration
 *  3. Call write()/read() for I/O
 *  4. Call deinit() to release peripheral
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - For RTOS use, wrap with mutex or use from single task.
 *
 * RT-Safety:
 *  - init()/deinit(): NOT RT-safe (peripheral setup)
 *  - read()/write(): RT-safe after init (no allocation, bounded time)
 *  - available()/txReady(): RT-safe (register reads)
 */
class IUart {
public:
  virtual ~IUart() = default;

  /**
   * @brief Initialize the UART peripheral.
   * @param config Configuration parameters.
   * @return OK on success, ERROR_* on failure.
   * @note NOT RT-safe: Configures peripheral registers.
   */
  [[nodiscard]] virtual UartStatus init(const UartConfig& config) noexcept = 0;

  /**
   * @brief Deinitialize the UART peripheral.
   * @note NOT RT-safe: Releases peripheral.
   */
  virtual void deinit() noexcept = 0;

  /**
   * @brief Check if UART is initialized and ready.
   * @return true if initialized.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

  /**
   * @brief Write bytes to the UART (non-blocking).
   * @param data Data to transmit.
   * @param len Number of bytes to transmit.
   * @return Number of bytes actually queued for transmission.
   * @note RT-safe: Copies to TX buffer, returns immediately.
   * @note Returns 0 if TX buffer is full (call txReady() first).
   */
  virtual size_t write(const uint8_t* data, size_t len) noexcept = 0;

  /**
   * @brief Read bytes from the UART (non-blocking).
   * @param buffer Destination buffer.
   * @param maxLen Maximum bytes to read.
   * @return Number of bytes actually read.
   * @note RT-safe: Copies from RX buffer, returns immediately.
   * @note Returns 0 if no data available (call available() first).
   */
  virtual size_t read(uint8_t* buffer, size_t maxLen) noexcept = 0;

  /**
   * @brief Get number of bytes available to read.
   * @return Bytes in RX buffer.
   * @note RT-safe.
   */
  [[nodiscard]] virtual size_t available() const noexcept = 0;

  /**
   * @brief Check if TX is ready to accept more data.
   * @return true if TX buffer has space.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool txReady() const noexcept = 0;

  /**
   * @brief Check if all TX data has been sent.
   * @return true if TX buffer is empty and shift register is clear.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool txComplete() const noexcept = 0;

  /**
   * @brief Flush RX buffer (discard pending data).
   * @note RT-safe.
   */
  virtual void flushRx() noexcept = 0;

  /**
   * @brief Wait for TX to complete (blocking).
   * @note NOT RT-safe: May block.
   */
  virtual void flushTx() noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe.
   */
  [[nodiscard]] virtual const UartStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe.
   */
  virtual void resetStats() noexcept = 0;

  /* ----------------------------- Convenience Methods ----------------------------- */

  /**
   * @brief Write a null-terminated string.
   * @param str String to transmit (null terminator not sent).
   * @return Number of bytes actually queued.
   * @note RT-safe.
   */
  size_t print(const char* str) noexcept {
    if (str == nullptr) {
      return 0;
    }
    size_t len = 0;
    while (str[len] != '\0') {
      ++len;
    }
    return write(reinterpret_cast<const uint8_t*>(str), len);
  }

  /**
   * @brief Write a string followed by newline (CRLF).
   * @param str String to transmit.
   * @return Number of bytes actually queued (including CRLF).
   * @note RT-safe.
   */
  size_t println(const char* str) noexcept {
    size_t n = print(str);
    static constexpr uint8_t CRLF[2] = {'\r', '\n'};
    n += write(CRLF, 2);
    return n;
  }

  /**
   * @brief Write a single byte.
   * @param b Byte to transmit.
   * @return 1 if queued, 0 if buffer full.
   * @note RT-safe.
   */
  size_t writeByte(uint8_t b) noexcept { return write(&b, 1); }

  /**
   * @brief Read a single byte.
   * @param b Output: byte read.
   * @return true if byte read, false if no data available.
   * @note RT-safe.
   */
  bool readByte(uint8_t& b) noexcept { return read(&b, 1) == 1; }

protected:
  IUart() = default;
  IUart(const IUart&) = delete;
  IUart& operator=(const IUart&) = delete;
  IUart(IUart&&) = default;
  IUart& operator=(IUart&&) = default;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_IUART_HPP
