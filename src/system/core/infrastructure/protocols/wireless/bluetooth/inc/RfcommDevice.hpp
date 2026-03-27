#ifndef APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_DEVICE_HPP
#define APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_DEVICE_HPP
/**
 * @file RfcommDevice.hpp
 * @brief Abstract interface for Bluetooth RFCOMM devices.
 *
 * Defines the interface that all RFCOMM implementations must provide.
 * Follows the transport abstraction pattern used by UART and Modbus.
 */

#include "RfcommConfig.hpp"
#include "RfcommStats.hpp"
#include "RfcommStatus.hpp"

#include "src/utilities/compatibility/inc/compat_span.hpp"

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- RfcommDevice ----------------------------- */

/**
 * @class RfcommDevice
 * @brief Abstract interface for RFCOMM connections.
 *
 * Implementations include:
 * - RfcommAdapter: Linux AF_BLUETOOTH socket implementation
 *
 * RT-Safety Classification:
 * - configure(), connect(): NOT RT-safe (syscalls, may block)
 * - disconnect(): NOT RT-safe (syscalls)
 * - read(), write(): RT-safe if timeout > 0 (bounded syscalls)
 * - All getters: RT-safe (no syscalls, no allocation)
 */
class RfcommDevice {
public:
  virtual ~RfcommDevice() = default;

  /* ----------------------------- Identification ----------------------------- */

  /**
   * @brief Get device description.
   * @return Null-terminated description string.
   * @note RT-safe: Returns internal buffer, no allocation.
   */
  [[nodiscard]] virtual const char* description() const noexcept = 0;

  /* ----------------------------- Connection ----------------------------- */

  /**
   * @brief Configure the device with connection parameters.
   * @param cfg Configuration parameters.
   * @return Status code.
   * @note NOT RT-safe: May allocate, perform setup.
   *
   * Must be called before connect(). Does not initiate connection.
   */
  [[nodiscard]] virtual Status configure(const RfcommConfig& cfg) = 0;

  /**
   * @brief Establish connection to remote device.
   * @return Status code.
   * @note NOT RT-safe: May block up to connectTimeoutMs.
   *
   * Uses timeout from configure(). Returns immediately if already connected.
   */
  [[nodiscard]] virtual Status connect() = 0;

  /**
   * @brief Disconnect from remote device.
   * @return Status code.
   * @note NOT RT-safe: Socket close syscall.
   *
   * Safe to call if not connected (returns SUCCESS).
   */
  [[nodiscard]] virtual Status disconnect() noexcept = 0;

  /**
   * @brief Check if connected.
   * @return true if connected to remote device.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] virtual bool isConnected() const noexcept = 0;

  /* ----------------------------- I/O ----------------------------- */

  /**
   * @brief Read data from connection.
   * @param buffer Destination buffer.
   * @param bytesRead Output: number of bytes actually read.
   * @param timeoutMs Timeout (-1=block, 0=poll, >0=bounded wait).
   * @return Status code.
   * @note RT-safe if timeoutMs >= 0 (bounded syscalls, no allocation).
   *
   * Partial reads are normal. Returns SUCCESS with bytesRead=0 only on timeout.
   */
  [[nodiscard]] virtual Status read(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                                    int timeoutMs) noexcept = 0;

  /**
   * @brief Write data to connection.
   * @param data Source data.
   * @param bytesWritten Output: number of bytes actually written.
   * @param timeoutMs Timeout (-1=block, 0=poll, >0=bounded wait).
   * @return Status code.
   * @note RT-safe if timeoutMs >= 0 (bounded syscalls, no allocation).
   *
   * Partial writes may occur. Caller should retry with remaining data.
   */
  [[nodiscard]] virtual Status write(apex::compat::bytes_span data, std::size_t& bytesWritten,
                                     int timeoutMs) noexcept = 0;

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics struct.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] virtual const RfcommStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe: O(1), no allocation.
   */
  virtual void resetStats() noexcept = 0;

  /* ----------------------------- File Descriptor ----------------------------- */

  /**
   * @brief Get underlying file descriptor.
   * @return Socket FD, or -1 if not connected.
   * @note RT-safe: O(1), no allocation.
   *
   * Useful for epoll/poll integration.
   */
  [[nodiscard]] virtual int fd() const noexcept = 0;
};

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_DEVICE_HPP
