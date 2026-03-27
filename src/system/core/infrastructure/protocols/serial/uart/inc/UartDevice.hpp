#ifndef APEX_PROTOCOLS_SERIAL_UART_DEVICE_HPP
#define APEX_PROTOCOLS_SERIAL_UART_DEVICE_HPP
/**
 * @file UartDevice.hpp
 * @brief Abstract interface for UART device operations.
 *
 * Defines the common interface for UART-like devices, including hardware
 * serial ports, USB-serial adapters, and pseudo-terminals (for testing).
 * Implementations are responsible for managing file descriptors and
 * platform-specific configuration.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStats.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStatus.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* ----------------------------- UartDevice ----------------------------- */

/**
 * @class UartDevice
 * @brief Abstract interface for UART device operations.
 *
 * This interface provides a common API for serial communication regardless
 * of the underlying device type (hardware UART, USB-serial, PTY, etc.).
 *
 * Timeout Semantics:
 *  - timeoutMs < 0: Block indefinitely until data available or error.
 *  - timeoutMs == 0: Poll (return immediately with WOULD_BLOCK if not ready).
 *  - timeoutMs > 0: Wait up to timeoutMs milliseconds.
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - Concurrent read/write from different threads requires external sync.
 *  - Stats updates are not atomic.
 */
class UartDevice {
public:
  virtual ~UartDevice() = default;

  /**
   * @brief Configure the device with the given settings.
   * @param config Configuration parameters.
   * @return SUCCESS if configured, ERROR_* on failure.
   * @note NOT RT-safe: May perform system calls.
   */
  [[nodiscard]] virtual Status configure(const UartConfig& config) noexcept = 0;

  /**
   * @brief Read bytes from the device.
   * @param buffer Destination buffer for received data.
   * @param bufferSize Size of the buffer in bytes.
   * @param bytesRead Output: number of bytes actually read.
   * @param timeoutMs Timeout in milliseconds (see class docs).
   * @return SUCCESS if bytes read, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe when device is configured and no errors occur.
   */
  [[nodiscard]] virtual Status read(std::uint8_t* buffer, std::size_t bufferSize,
                                    std::size_t& bytesRead, int timeoutMs) noexcept = 0;

  /**
   * @brief Write bytes to the device.
   * @param data Source data to transmit.
   * @param dataSize Size of the data in bytes.
   * @param bytesWritten Output: number of bytes actually written.
   * @param timeoutMs Timeout in milliseconds (see class docs).
   * @return SUCCESS if bytes written, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe when device is configured and no errors occur.
   */
  [[nodiscard]] virtual Status write(const std::uint8_t* data, std::size_t dataSize,
                                     std::size_t& bytesWritten, int timeoutMs) noexcept = 0;

  /**
   * @brief Write a null-terminated string to the device.
   * @param text Null-terminated string to transmit.
   * @param bytesWritten Output: number of bytes actually written.
   * @param timeoutMs Timeout in milliseconds (see class docs).
   * @return SUCCESS if bytes written, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe when device is configured and no errors occur.
   * @note Convenience wrapper around write() for ASCII/text protocols.
   */
  [[nodiscard]] Status writeAscii(const char* text, std::size_t& bytesWritten,
                                  int timeoutMs) noexcept {
    if (text == nullptr) {
      return Status::ERROR_INVALID_ARG;
    }
    std::size_t len = 0;
    while (text[len] != '\0') {
      ++len;
    }
    return write(reinterpret_cast<const std::uint8_t*>(text), len, bytesWritten, timeoutMs);
  }

  /**
   * @brief Flush pending input and/or output data.
   * @param flushRx Flush receive buffer.
   * @param flushTx Flush transmit buffer.
   * @return SUCCESS on success, ERROR_* on failure.
   * @note NOT RT-safe: May block briefly.
   */
  [[nodiscard]] virtual Status flush(bool flushRx, bool flushTx) noexcept = 0;

  /**
   * @brief Close the device and release resources.
   * @return SUCCESS on success, ERROR_* on failure.
   * @note NOT RT-safe: Releases system resources.
   */
  [[nodiscard]] virtual Status close() noexcept = 0;

  /**
   * @brief Check if the device is open and ready for I/O.
   * @return true if device is open and configured.
   */
  [[nodiscard]] virtual bool isOpen() const noexcept = 0;

  /**
   * @brief Get the underlying file descriptor.
   * @return File descriptor, or -1 if not open.
   * @note Exposed for epoll/select integration.
   */
  [[nodiscard]] virtual int fd() const noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   */
  [[nodiscard]] virtual const UartStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   */
  virtual void resetStats() noexcept = 0;

  /**
   * @brief Get the device path (e.g., "/dev/ttyUSB0").
   * @return Device path string, or empty if not applicable.
   */
  [[nodiscard]] virtual const char* devicePath() const noexcept = 0;

protected:
  UartDevice() = default;
  UartDevice(const UartDevice&) = delete;
  UartDevice& operator=(const UartDevice&) = delete;
  UartDevice(UartDevice&&) = default;
  UartDevice& operator=(UartDevice&&) = default;
};

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SERIAL_UART_DEVICE_HPP
