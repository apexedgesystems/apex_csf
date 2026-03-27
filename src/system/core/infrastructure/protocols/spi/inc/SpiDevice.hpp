#ifndef APEX_PROTOCOLS_SPI_DEVICE_HPP
#define APEX_PROTOCOLS_SPI_DEVICE_HPP
/**
 * @file SpiDevice.hpp
 * @brief Abstract interface for SPI device operations.
 *
 * Defines the common interface for SPI master devices, including hardware
 * SPI controllers and user-space spidev devices. Implementations are
 * responsible for managing file descriptors and platform-specific configuration.
 *
 * SPI transfers are fundamentally bidirectional: data is simultaneously
 * transmitted and received. This interface provides both full-duplex transfer()
 * and convenience read()/write() methods.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiConfig.hpp"
#include "src/system/core/infrastructure/protocols/spi/inc/SpiStats.hpp"
#include "src/system/core/infrastructure/protocols/spi/inc/SpiStatus.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- SpiDevice ----------------------------- */

/**
 * @class SpiDevice
 * @brief Abstract interface for SPI device operations.
 *
 * This interface provides a common API for SPI communication regardless
 * of the underlying device type (hardware SPI, spidev, etc.).
 *
 * Transfer Semantics:
 *  - SPI is full-duplex: transfer() sends txData while receiving into rxData.
 *  - For transmit-only: pass nullptr for rxData (received data discarded).
 *  - For receive-only: pass nullptr for txData (zeros transmitted).
 *  - txData and rxData may point to the same buffer for in-place transfer.
 *
 * Timeout Semantics:
 *  - timeoutMs < 0: Block indefinitely until transfer completes or error.
 *  - timeoutMs == 0: Poll (return immediately with WOULD_BLOCK if not ready).
 *  - timeoutMs > 0: Wait up to timeoutMs milliseconds.
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - Concurrent transfers from different threads require external sync.
 *  - Stats updates are not atomic.
 */
class SpiDevice {
public:
  virtual ~SpiDevice() = default;

  /**
   * @brief Configure the device with the given settings.
   * @param config Configuration parameters.
   * @return SUCCESS if configured, ERROR_* on failure.
   * @note NOT RT-safe: May perform system calls.
   */
  [[nodiscard]] virtual Status configure(const SpiConfig& config) noexcept = 0;

  /**
   * @brief Perform a full-duplex SPI transfer.
   * @param txData Data to transmit (nullptr = send zeros).
   * @param rxData Buffer for received data (nullptr = discard received).
   * @param length Number of bytes to transfer.
   * @param timeoutMs Timeout in milliseconds (see class docs).
   * @return SUCCESS if transfer complete, WOULD_BLOCK if not ready, ERROR_* on failure.
   * @note RT-safe when device is configured and no errors occur.
   * @note txData and rxData may point to the same buffer.
   */
  [[nodiscard]] virtual Status transfer(const std::uint8_t* txData, std::uint8_t* rxData,
                                        std::size_t length, int timeoutMs) noexcept = 0;

  /**
   * @brief Transmit data (receive data discarded).
   * @param data Data to transmit.
   * @param length Number of bytes to transmit.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if transfer complete, ERROR_* on failure.
   * @note RT-safe: Convenience wrapper around transfer().
   */
  [[nodiscard]] Status write(const std::uint8_t* data, std::size_t length, int timeoutMs) noexcept {
    return transfer(data, nullptr, length, timeoutMs);
  }

  /**
   * @brief Receive data (zeros transmitted).
   * @param buffer Buffer for received data.
   * @param length Number of bytes to receive.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if transfer complete, ERROR_* on failure.
   * @note RT-safe: Convenience wrapper around transfer().
   */
  [[nodiscard]] Status read(std::uint8_t* buffer, std::size_t length, int timeoutMs) noexcept {
    return transfer(nullptr, buffer, length, timeoutMs);
  }

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
  [[nodiscard]] virtual const SpiStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   */
  virtual void resetStats() noexcept = 0;

  /**
   * @brief Get the device path (e.g., "/dev/spidev0.0").
   * @return Device path string, or empty if not applicable.
   */
  [[nodiscard]] virtual const char* devicePath() const noexcept = 0;

  /**
   * @brief Get the current configuration.
   * @return Current device configuration.
   */
  [[nodiscard]] virtual const SpiConfig& config() const noexcept = 0;

protected:
  SpiDevice() = default;
  SpiDevice(const SpiDevice&) = delete;
  SpiDevice& operator=(const SpiDevice&) = delete;
  SpiDevice(SpiDevice&&) = default;
  SpiDevice& operator=(SpiDevice&&) = default;
};

} // namespace spi
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SPI_DEVICE_HPP
