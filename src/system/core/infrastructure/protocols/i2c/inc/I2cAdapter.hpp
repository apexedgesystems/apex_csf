#ifndef APEX_PROTOCOLS_I2C_ADAPTER_HPP
#define APEX_PROTOCOLS_I2C_ADAPTER_HPP
/**
 * @file I2cAdapter.hpp
 * @brief Linux I2C device adapter using i2c-dev interface.
 *
 * Provides a complete implementation of I2cDevice for Linux I2C buses
 * accessed via the i2c-dev driver (/dev/i2c-X).
 *
 * Features:
 *  - 7-bit and 10-bit addressing
 *  - Combined write-then-read transactions (I2C_RDWR)
 *  - SMBus PEC support (if hardware supports)
 *  - Statistics tracking
 *  - Optional byte-level tracing via ByteTrace mixin
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/i2c/inc/I2cDevice.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <string>

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- I2cAdapter ----------------------------- */

/**
 * @class I2cAdapter
 * @brief Linux I2C device implementation using i2c-dev.
 *
 * Lifecycle:
 *  1. Construct with bus number or device path (does not open yet)
 *  2. Call configure() to open the bus
 *  3. Call setSlaveAddress() to select target device
 *  4. Call read()/write()/writeRead() for I/O
 *  5. Call close() or let destructor clean up
 *
 * @note NOT thread-safe: External synchronization required for concurrent access.
 */
class I2cAdapter : public I2cDevice, public apex::protocols::ByteTrace {
public:
  /**
   * @brief Construct adapter for a device path.
   * @param devicePath Path to I2C bus (e.g., "/dev/i2c-1").
   * @note NOT RT-safe: Construction only. Does not open the device; call configure() to open.
   */
  explicit I2cAdapter(const std::string& devicePath);

  /**
   * @brief Construct adapter for a device path (move variant).
   * @param devicePath Path to I2C bus.
   * @note NOT RT-safe: Construction only.
   */
  explicit I2cAdapter(std::string&& devicePath);

  /**
   * @brief Construct adapter by bus number.
   * @param busNumber I2C bus number (X in /dev/i2c-X).
   * @note NOT RT-safe: Construction only.
   */
  explicit I2cAdapter(std::uint32_t busNumber);

  ~I2cAdapter() override;

  I2cAdapter(const I2cAdapter&) = delete;
  I2cAdapter& operator=(const I2cAdapter&) = delete;
  I2cAdapter(I2cAdapter&& other) noexcept;
  I2cAdapter& operator=(I2cAdapter&& other) noexcept;

  /* ----------------------------- I2cDevice Interface ----------------------------- */

  [[nodiscard]] Status configure(const I2cConfig& config) noexcept override;

  [[nodiscard]] Status setSlaveAddress(std::uint16_t address) noexcept override;

  [[nodiscard]] Status read(std::uint8_t* buffer, std::size_t length, std::size_t& bytesRead,
                            int timeoutMs) noexcept override;

  [[nodiscard]] Status write(const std::uint8_t* data, std::size_t length,
                             std::size_t& bytesWritten, int timeoutMs) noexcept override;

  [[nodiscard]] Status writeRead(const std::uint8_t* writeData, std::size_t writeLength,
                                 std::uint8_t* readBuffer, std::size_t readLength,
                                 std::size_t& bytesRead, int timeoutMs) noexcept override;

  [[nodiscard]] Status close() noexcept override;

  [[nodiscard]] bool isOpen() const noexcept override;

  [[nodiscard]] int fd() const noexcept override;

  [[nodiscard]] const I2cStats& stats() const noexcept override;

  void resetStats() noexcept override;

  [[nodiscard]] const char* devicePath() const noexcept override;

  [[nodiscard]] std::uint16_t slaveAddress() const noexcept override;

  /* ----------------------------- Span API ----------------------------- */

  /**
   * @brief Read bytes using span.
   * @param buffer Destination span for received data.
   * @param bytesRead Output: number of bytes actually read.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if bytes read, ERROR_* on failure.
   * @note RT-safe: Inline wrapper.
   */
  [[nodiscard]] Status read(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                            int timeoutMs) noexcept;

  /**
   * @brief Write bytes using span.
   * @param data Source span containing data to transmit.
   * @param bytesWritten Output: number of bytes actually written.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if bytes written, ERROR_* on failure.
   * @note RT-safe: Inline wrapper.
   */
  [[nodiscard]] Status write(apex::compat::bytes_span data, std::size_t& bytesWritten,
                             int timeoutMs) noexcept;

  /* ----------------------------- Register Access ----------------------------- */

  /**
   * @brief Read a single byte from a register.
   * @param regAddr Register address.
   * @param value Output: register value.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if read, ERROR_* on failure.
   * @note RT-safe: Convenience wrapper around writeRead().
   */
  [[nodiscard]] Status readRegister(std::uint8_t regAddr, std::uint8_t& value,
                                    int timeoutMs) noexcept;

  /**
   * @brief Write a single byte to a register.
   * @param regAddr Register address.
   * @param value Value to write.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if written, ERROR_* on failure.
   * @note RT-safe: Convenience wrapper around write().
   */
  [[nodiscard]] Status writeRegister(std::uint8_t regAddr, std::uint8_t value,
                                     int timeoutMs) noexcept;

  /**
   * @brief Read multiple bytes starting from a register.
   * @param regAddr Starting register address.
   * @param buffer Output buffer.
   * @param length Number of bytes to read.
   * @param bytesRead Output: number of bytes actually read.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if read, ERROR_* on failure.
   * @note RT-safe: Convenience wrapper around writeRead().
   */
  [[nodiscard]] Status readRegisters(std::uint8_t regAddr, std::uint8_t* buffer, std::size_t length,
                                     std::size_t& bytesRead, int timeoutMs) noexcept;

  /* ----------------------------- Bus Probing ----------------------------- */

  /**
   * @brief Check if a device responds at the current slave address.
   * @return true if device acknowledges.
   * @note RT-safe: Performs a quick write/read probe after configure.
   */
  [[nodiscard]] bool probeDevice() noexcept;

private:
  std::string devicePath_;
  int fd_ = -1;
  bool configured_ = false;
  std::uint16_t slaveAddress_ = 0;
  bool slaveAddressSet_ = false;
  I2cConfig config_;
  I2cStats stats_;

  Status openDevice() noexcept;
  Status applySlaveAddress() noexcept;
};

} // namespace i2c
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_I2C_ADAPTER_HPP
