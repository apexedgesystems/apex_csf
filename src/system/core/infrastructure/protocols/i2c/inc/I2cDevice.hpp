#ifndef APEX_PROTOCOLS_I2C_DEVICE_HPP
#define APEX_PROTOCOLS_I2C_DEVICE_HPP
/**
 * @file I2cDevice.hpp
 * @brief Abstract interface for I2C device operations.
 *
 * Defines the common interface for I2C master devices. Implementations are
 * responsible for managing file descriptors and platform-specific configuration.
 *
 * I2C operations are addressed to specific slave devices. This interface
 * provides read/write operations to a currently selected slave address.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cConfig.hpp"
#include "src/system/core/infrastructure/protocols/i2c/inc/I2cStats.hpp"
#include "src/system/core/infrastructure/protocols/i2c/inc/I2cStatus.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- I2cDevice ----------------------------- */

/**
 * @class I2cDevice
 * @brief Abstract interface for I2C device operations.
 *
 * This interface provides a common API for I2C communication regardless
 * of the underlying device type (hardware I2C, i2c-dev, etc.).
 *
 * Usage Pattern:
 *  1. configure() - Open the bus and set configuration
 *  2. setSlaveAddress() - Select the target device
 *  3. read()/write()/writeRead() - Perform transactions
 *  4. close() - Release resources
 *
 * Timeout Semantics:
 *  - timeoutMs < 0: Block indefinitely until complete or error.
 *  - timeoutMs == 0: Poll (return immediately with WOULD_BLOCK if not ready).
 *  - timeoutMs > 0: Wait up to timeoutMs milliseconds.
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - Concurrent access from different threads requires external sync.
 */
class I2cDevice {
public:
  virtual ~I2cDevice() = default;

  /**
   * @brief Configure the device with the given settings.
   * @param config Configuration parameters.
   * @return SUCCESS if configured, ERROR_* on failure.
   * @note NOT RT-safe: May perform system calls.
   */
  [[nodiscard]] virtual Status configure(const I2cConfig& config) noexcept = 0;

  /**
   * @brief Set the slave address for subsequent operations.
   * @param address 7-bit or 10-bit slave address.
   * @return SUCCESS if set, ERROR_* on failure.
   * @note RT-safe after configure().
   * @note Must be called before read/write operations.
   */
  [[nodiscard]] virtual Status setSlaveAddress(std::uint16_t address) noexcept = 0;

  /**
   * @brief Read bytes from the current slave device.
   * @param buffer Destination buffer for received data.
   * @param length Number of bytes to read.
   * @param bytesRead Output: number of bytes actually read.
   * @param timeoutMs Timeout in milliseconds (see class docs).
   * @return SUCCESS if bytes read, ERROR_* on failure.
   * @note RT-safe when device is configured.
   */
  [[nodiscard]] virtual Status read(std::uint8_t* buffer, std::size_t length,
                                    std::size_t& bytesRead, int timeoutMs) noexcept = 0;

  /**
   * @brief Write bytes to the current slave device.
   * @param data Data to transmit.
   * @param length Number of bytes to write.
   * @param bytesWritten Output: number of bytes actually written.
   * @param timeoutMs Timeout in milliseconds (see class docs).
   * @return SUCCESS if bytes written, ERROR_* on failure.
   * @note RT-safe when device is configured.
   */
  [[nodiscard]] virtual Status write(const std::uint8_t* data, std::size_t length,
                                     std::size_t& bytesWritten, int timeoutMs) noexcept = 0;

  /**
   * @brief Write then read in a single transaction (combined message).
   *
   * Performs a write followed by a repeated-start and read. This is the
   * standard pattern for reading from I2C registers:
   *   START -> ADDR+W -> regAddr -> RESTART -> ADDR+R -> data -> STOP
   *
   * @param writeData Data to write (e.g., register address).
   * @param writeLength Number of bytes to write.
   * @param readBuffer Buffer for received data.
   * @param readLength Number of bytes to read.
   * @param bytesRead Output: number of bytes actually read.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if complete, ERROR_* on failure.
   * @note RT-safe when device is configured.
   */
  [[nodiscard]] virtual Status writeRead(const std::uint8_t* writeData, std::size_t writeLength,
                                         std::uint8_t* readBuffer, std::size_t readLength,
                                         std::size_t& bytesRead, int timeoutMs) noexcept = 0;

  /**
   * @brief Close the device and release resources.
   * @return SUCCESS on success, ERROR_* on failure.
   * @note NOT RT-safe: Releases system resources.
   */
  [[nodiscard]] virtual Status close() noexcept = 0;

  /**
   * @brief Check if the device is open and ready for I/O.
   * @return true if device is open and configured.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] virtual bool isOpen() const noexcept = 0;

  /**
   * @brief Get the underlying file descriptor.
   * @return File descriptor, or -1 if not open.
   * @note RT-safe: O(1), no allocation. Exposed for epoll/select integration.
   */
  [[nodiscard]] virtual int fd() const noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] virtual const I2cStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe: O(1), no allocation.
   */
  virtual void resetStats() noexcept = 0;

  /**
   * @brief Get the device path (e.g., "/dev/i2c-1").
   * @return Device path string, or empty if not applicable.
   * @note RT-safe: O(1), returns pointer to internal storage.
   */
  [[nodiscard]] virtual const char* devicePath() const noexcept = 0;

  /**
   * @brief Get the current slave address.
   * @return Currently selected slave address, or 0 if not set.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] virtual std::uint16_t slaveAddress() const noexcept = 0;

protected:
  I2cDevice() = default;
  I2cDevice(const I2cDevice&) = delete;
  I2cDevice& operator=(const I2cDevice&) = delete;
  I2cDevice(I2cDevice&&) = default;
  I2cDevice& operator=(I2cDevice&&) = default;
};

} // namespace i2c
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_I2C_DEVICE_HPP
