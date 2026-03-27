#ifndef APEX_PROTOCOLS_SPI_SOCKET_DEVICE_HPP
#define APEX_PROTOCOLS_SPI_SOCKET_DEVICE_HPP
/**
 * @file SpiSocketDevice.hpp
 * @brief SpiDevice implementation over a Unix socketpair for virtual transport.
 *
 * Provides a software SPI device that routes full-duplex transfers through a
 * Unix stream socket (one end of a socketpair). This allows HW_MODEL emulations
 * to communicate with DRIVER code using the same SpiDevice interface they would
 * use with real spidev hardware.
 *
 * Wire Protocol (over the socketpair):
 *   Request:  [length:4 LE][txData:length]
 *   Response: [rxData:length]
 *
 * The peer (HW_MODEL side) reads the request, processes the transfer, and
 * writes back exactly `length` bytes of response data. This models the
 * full-duplex nature of SPI where MOSI and MISO exchange simultaneously.
 *
 * The framework auto-provisions a socketpair during HW_MODEL registration.
 * One fd goes to the SpiSocketDevice (DRIVER side), the other to the HW_MODEL.
 *
 * All public functions are RT-safe after configure() unless noted.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiDevice.hpp"

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- SpiSocketDevice ----------------------------- */

/**
 * @class SpiSocketDevice
 * @brief SPI device over Unix socketpair for hardware emulation.
 *
 * Lifecycle:
 *  1. Framework creates socketpair(AF_UNIX, SOCK_STREAM, 0)
 *  2. Construct SpiSocketDevice with one fd
 *  3. Call configure() to mark the device ready
 *  4. Call transfer()/read()/write() for I/O
 *  5. Call close() or let destructor clean up
 *
 * @note NOT thread-safe: External synchronization required for concurrent access.
 */
class SpiSocketDevice : public SpiDevice {
public:
  /**
   * @brief Construct a socket-backed SPI device.
   * @param socketFd One end of a Unix socketpair (SOCK_STREAM).
   * @param ownsFd If true, the fd is closed on destruction/close().
   * @note NOT RT-safe: Construction only.
   */
  explicit SpiSocketDevice(int socketFd, bool ownsFd = true) noexcept;

  ~SpiSocketDevice() override;

  SpiSocketDevice(const SpiSocketDevice&) = delete;
  SpiSocketDevice& operator=(const SpiSocketDevice&) = delete;
  SpiSocketDevice(SpiSocketDevice&& other) noexcept;
  SpiSocketDevice& operator=(SpiSocketDevice&& other) noexcept;

  /* ----------------------------- SpiDevice Interface ----------------------------- */

  [[nodiscard]] Status configure(const SpiConfig& config) noexcept override;

  [[nodiscard]] Status transfer(const std::uint8_t* txData, std::uint8_t* rxData,
                                std::size_t length, int timeoutMs) noexcept override;

  [[nodiscard]] Status close() noexcept override;

  [[nodiscard]] bool isOpen() const noexcept override;

  [[nodiscard]] int fd() const noexcept override;

  [[nodiscard]] const SpiStats& stats() const noexcept override;

  void resetStats() noexcept override;

  [[nodiscard]] const char* devicePath() const noexcept override;

  [[nodiscard]] const SpiConfig& config() const noexcept override;

private:
  int fd_ = -1;
  bool ownsFd_ = false;
  bool configured_ = false;
  SpiConfig config_;
  SpiStats stats_;

  /**
   * @brief Send all bytes, handling partial writes.
   * @param data Source buffer.
   * @param len Bytes to send.
   * @return true on success, false on error.
   * @note RT-safe: Blocking write syscalls.
   */
  bool sendAll(const void* data, std::size_t len) noexcept;

  /**
   * @brief Receive all bytes, handling partial reads.
   * @param data Destination buffer.
   * @param len Bytes to receive.
   * @return true on success, false on error.
   * @note RT-safe: Blocking read syscalls.
   */
  bool recvAll(void* data, std::size_t len) noexcept;
};

} // namespace spi
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SPI_SOCKET_DEVICE_HPP
