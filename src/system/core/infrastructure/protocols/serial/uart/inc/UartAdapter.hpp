#ifndef APEX_PROTOCOLS_SERIAL_UART_ADAPTER_HPP
#define APEX_PROTOCOLS_SERIAL_UART_ADAPTER_HPP
/**
 * @file UartAdapter.hpp
 * @brief Linux UART device adapter with termios configuration.
 *
 * Provides a complete implementation of UartDevice for Linux serial ports,
 * including hardware UARTs (/dev/ttySN), USB-serial adapters (/dev/ttyUSBN,
 * /dev/ttyACMN), and pseudo-terminals (/dev/pts/N).
 *
 * Features:
 *  - Full termios configuration (baud, parity, stop bits, flow control)
 *  - RS-485 half-duplex support via TIOCSRS485
 *  - Low-latency mode via TIOCGSERIAL/TIOCSSERIAL
 *  - Exclusive access via flock()
 *  - Poll-based timeout handling
 *  - Statistics tracking
 *  - Optional byte-level tracing via ByteTrace mixin
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartDevice.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <string>
#include <sys/uio.h>

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* ----------------------------- UartAdapter ----------------------------- */

/**
 * @class UartAdapter
 * @brief Linux UART device implementation.
 *
 * Lifecycle:
 *  1. Construct with device path (does not open yet)
 *  2. Call configure() to open and set up the port
 *  3. Call read()/write() for I/O
 *  4. Call close() or let destructor clean up
 *
 * @note NOT thread-safe: External synchronization required for concurrent access.
 */
class UartAdapter : public UartDevice, public apex::protocols::ByteTrace {
public:
  /**
   * @brief Construct adapter for a device path.
   * @param devicePath Path to serial device (e.g., "/dev/ttyUSB0").
   * @note Does not open the device; call configure() to open.
   */
  explicit UartAdapter(const std::string& devicePath);

  /**
   * @brief Construct adapter for a device path (move variant).
   * @param devicePath Path to serial device.
   */
  explicit UartAdapter(std::string&& devicePath);

  ~UartAdapter() override;

  UartAdapter(const UartAdapter&) = delete;
  UartAdapter& operator=(const UartAdapter&) = delete;
  UartAdapter(UartAdapter&& other) noexcept;
  UartAdapter& operator=(UartAdapter&& other) noexcept;

  /* ----------------------------- UartDevice Interface ----------------------------- */

  [[nodiscard]] Status configure(const UartConfig& config) noexcept override;

  [[nodiscard]] Status read(std::uint8_t* buffer, std::size_t bufferSize, std::size_t& bytesRead,
                            int timeoutMs) noexcept override;

  [[nodiscard]] Status write(const std::uint8_t* data, std::size_t dataSize,
                             std::size_t& bytesWritten, int timeoutMs) noexcept override;

  [[nodiscard]] Status flush(bool flushRx, bool flushTx) noexcept override;

  [[nodiscard]] Status close() noexcept override;

  [[nodiscard]] bool isOpen() const noexcept override;

  [[nodiscard]] int fd() const noexcept override;

  [[nodiscard]] const UartStats& stats() const noexcept override;

  void resetStats() noexcept override;

  [[nodiscard]] const char* devicePath() const noexcept override;

  /* ----------------------------- RT Extensions ----------------------------- */

  /**
   * @brief Write multiple buffers in a single syscall (scatter-gather).
   * @param iov Array of iovec structures describing buffers.
   * @param iovcnt Number of iovec structures.
   * @param bytesWritten Output: total bytes written across all buffers.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if bytes written, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe: Single syscall for multiple buffers reduces latency jitter.
   * @note Useful for protocols with header + payload (e.g., framed messages).
   */
  [[nodiscard]] Status writeVectored(const struct iovec* iov, int iovcnt, std::size_t& bytesWritten,
                                     int timeoutMs) noexcept COMPAT_HOT;

  /**
   * @brief Read into multiple buffers in a single syscall (scatter-gather).
   * @param iov Array of iovec structures describing buffers.
   * @param iovcnt Number of iovec structures.
   * @param bytesRead Output: total bytes read across all buffers.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if bytes read, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe: Single syscall for multiple buffers reduces latency jitter.
   */
  [[nodiscard]] Status readVectored(struct iovec* iov, int iovcnt, std::size_t& bytesRead,
                                    int timeoutMs) noexcept COMPAT_HOT;

  /* ----------------------------- Span API ----------------------------- */

  /**
   * @brief Read bytes into a span (zero-copy integration with protocol libs).
   * @param buffer Destination span for received data.
   * @param bytesRead Output: number of bytes actually read.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if bytes read, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe: Inline wrapper, no additional overhead.
   */
  [[nodiscard]] Status read(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                            int timeoutMs) noexcept COMPAT_HOT;

  /**
   * @brief Write bytes from a span (zero-copy integration with protocol libs).
   * @param data Source span containing data to transmit.
   * @param bytesWritten Output: number of bytes actually written.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if bytes written, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe: Inline wrapper, no additional overhead.
   */
  [[nodiscard]] Status write(apex::compat::bytes_span data, std::size_t& bytesWritten,
                             int timeoutMs) noexcept COMPAT_HOT;

private:
  std::string devicePath_;
  int fd_ = -1;
  bool configured_ = false;
  UartStats stats_;

  Status openDevice(bool exclusiveAccess) noexcept;
  Status applyTermios(const UartConfig& config) noexcept;
  Status applyRs485(const UartConfig::Rs485Config& rs485) noexcept;
  Status applyLowLatency(bool enable) noexcept;
  Status waitForReady(bool forWrite, int timeoutMs) noexcept;
};

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SERIAL_UART_ADAPTER_HPP
