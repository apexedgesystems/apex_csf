#ifndef APEX_PROTOCOLS_I2C_SOCKET_DEVICE_HPP
#define APEX_PROTOCOLS_I2C_SOCKET_DEVICE_HPP
/**
 * @file I2cSocketDevice.hpp
 * @brief I2cDevice implementation over a Unix socketpair for virtual transport.
 *
 * Provides a software I2C device that routes transactions through a Unix
 * stream socket (one end of a socketpair). This allows HW_MODEL emulations
 * to communicate with DRIVER code using the same I2cDevice interface they
 * would use with real i2c-dev hardware.
 *
 * Wire Protocol (over the socketpair):
 *
 *   Write request:
 *     [addr:2 LE][wLen:2 LE][rLen:2 LE = 0][wData:wLen]
 *   Response:
 *     [status:1]
 *
 *   Read request:
 *     [addr:2 LE][wLen:2 LE = 0][rLen:2 LE][<no payload>]
 *   Response:
 *     [status:1][rData:rLen]
 *
 *   WriteRead request:
 *     [addr:2 LE][wLen:2 LE][rLen:2 LE][wData:wLen]
 *   Response:
 *     [status:1][rData:rLen]
 *
 * Status byte: 0 = ACK (success), 1 = NACK (device not responding).
 *
 * The peer (HW_MODEL side) reads the 6-byte header, then wLen bytes of
 * write data (if any), processes the transaction, and writes back the
 * status byte followed by rLen bytes of read data (if any).
 *
 * All public functions are RT-safe after configure() unless noted.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cDevice.hpp"

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- I2cSocketDevice ----------------------------- */

/**
 * @class I2cSocketDevice
 * @brief I2C device over Unix socketpair for hardware emulation.
 *
 * Lifecycle:
 *  1. Framework creates socketpair(AF_UNIX, SOCK_STREAM, 0)
 *  2. Construct I2cSocketDevice with one fd
 *  3. Call configure() to mark the device ready
 *  4. Call setSlaveAddress() to select target device
 *  5. Call read()/write()/writeRead() for I/O
 *  6. Call close() or let destructor clean up
 *
 * @note NOT thread-safe: External synchronization required for concurrent access.
 */
class I2cSocketDevice : public I2cDevice {
public:
  /**
   * @brief Construct a socket-backed I2C device.
   * @param socketFd One end of a Unix socketpair (SOCK_STREAM).
   * @param ownsFd If true, the fd is closed on destruction/close().
   * @note NOT RT-safe: Construction only.
   */
  explicit I2cSocketDevice(int socketFd, bool ownsFd = true) noexcept;

  ~I2cSocketDevice() override;

  I2cSocketDevice(const I2cSocketDevice&) = delete;
  I2cSocketDevice& operator=(const I2cSocketDevice&) = delete;
  I2cSocketDevice(I2cSocketDevice&& other) noexcept;
  I2cSocketDevice& operator=(I2cSocketDevice&& other) noexcept;

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

private:
  int fd_ = -1;
  bool ownsFd_ = false;
  bool configured_ = false;
  std::uint16_t slaveAddr_ = 0;
  I2cStats stats_;

  /**
   * @brief Execute a generic I2C transaction over the socketpair.
   * @param addr Slave address.
   * @param wData Write data (may be nullptr if wLen == 0).
   * @param wLen Write data length.
   * @param rBuf Read buffer (may be nullptr if rLen == 0).
   * @param rLen Expected read length.
   * @param bytesRead Output: actual bytes read (set to rLen on success).
   * @return Status code.
   */
  Status doTransaction(std::uint16_t addr, const std::uint8_t* wData, std::uint16_t wLen,
                       std::uint8_t* rBuf, std::uint16_t rLen, std::size_t& bytesRead) noexcept;

  /**
   * @brief Send all bytes, handling partial writes.
   * @return true on success.
   */
  bool sendAll(const void* data, std::size_t len) noexcept;

  /**
   * @brief Receive all bytes, handling partial reads.
   * @return true on success.
   */
  bool recvAll(void* data, std::size_t len) noexcept;
};

} // namespace i2c
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_I2C_SOCKET_DEVICE_HPP
