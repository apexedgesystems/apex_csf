#ifndef APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_LOOPBACK_HPP
#define APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_LOOPBACK_HPP
/**
 * @file RfcommLoopback.hpp
 * @brief Test utility providing loopback connection for RFCOMM testing.
 *
 * Uses socketpair() to create a bidirectional communication channel,
 * similar to PtyPair for UART testing. This enables unit testing of
 * RfcommAdapter without requiring real Bluetooth hardware.
 *
 * Design:
 *  - Creates a connected socket pair
 *  - Server side (serverFd) is used by test harness
 *  - Client side (clientFd) is passed to RfcommAdapter via FD injection
 *  - No Bluetooth hardware or BlueZ stack required
 *
 * Usage:
 * @code
 * RfcommLoopback loopback;
 * ASSERT_EQ(loopback.open(), Status::SUCCESS);
 *
 * // Create adapter with injected FD
 * RfcommAdapter adapter(loopback.releaseClientFd());
 *
 * // Test harness writes to server side
 * std::size_t written;
 * uint8_t testData[] = {0xDE, 0xAD};
 * loopback.serverWrite(testData, written, 100);
 *
 * // Adapter reads from client side (as if from Bluetooth)
 * uint8_t buf[64];
 * std::size_t bytesRead;
 * adapter.read(buf, bytesRead, 100);
 * @endcode
 */

#include "RfcommStatus.hpp"

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- RfcommLoopback ----------------------------- */

/**
 * @class RfcommLoopback
 * @brief Test utility providing loopback for RFCOMM testing.
 *
 * Creates a socketpair for bidirectional communication without real Bluetooth.
 *
 * RT-Safety Classification:
 * - open(), close(): NOT RT-safe (syscalls)
 * - serverRead(), serverWrite(): RT-safe if timeout >= 0
 * - releaseClientFd(): RT-safe (O(1))
 * - Getters: RT-safe (O(1))
 */
class RfcommLoopback {
public:
  /**
   * @brief Construct uninitialized loopback.
   * @note NOT RT-safe: May allocate.
   */
  RfcommLoopback() noexcept = default;

  /**
   * @brief Destructor closes open sockets.
   * @note NOT RT-safe: Syscall.
   */
  ~RfcommLoopback();

  // Non-copyable
  RfcommLoopback(const RfcommLoopback&) = delete;
  RfcommLoopback& operator=(const RfcommLoopback&) = delete;

  // Movable
  RfcommLoopback(RfcommLoopback&& other) noexcept;
  RfcommLoopback& operator=(RfcommLoopback&& other) noexcept;

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Create the socket pair.
   * @return Status code.
   * @note NOT RT-safe: Syscall.
   */
  [[nodiscard]] Status open() noexcept;

  /**
   * @brief Close both sockets.
   * @note NOT RT-safe: Syscall.
   */
  void close() noexcept;

  /**
   * @brief Check if loopback is open.
   * @return true if sockets are open.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] bool isOpen() const noexcept;

  /* ----------------------------- Server Side ----------------------------- */

  /**
   * @brief Get server-side file descriptor.
   * @return Server FD, or -1 if not open.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] int serverFd() const noexcept { return serverFd_; }

  /**
   * @brief Read from server side (test harness perspective).
   * @param buffer Destination buffer.
   * @param bytesRead Output: bytes actually read.
   * @param timeoutMs Timeout (-1=block, 0=poll, >0=bounded).
   * @return Status code.
   * @note RT-safe if timeoutMs >= 0 (bounded syscalls).
   */
  [[nodiscard]] Status serverRead(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                                  int timeoutMs) noexcept;

  /**
   * @brief Write to server side (test harness perspective).
   * @param data Source data.
   * @param bytesWritten Output: bytes actually written.
   * @param timeoutMs Timeout (-1=block, 0=poll, >0=bounded).
   * @return Status code.
   * @note RT-safe if timeoutMs >= 0 (bounded syscalls).
   */
  [[nodiscard]] Status serverWrite(apex::compat::bytes_span data, std::size_t& bytesWritten,
                                   int timeoutMs) noexcept;

  /* ----------------------------- Client Side ----------------------------- */

  /**
   * @brief Release client-side FD for injection into RfcommAdapter.
   * @return Client FD, or -1 if not open or already released.
   * @note RT-safe: O(1), no allocation.
   *
   * After calling this, the loopback no longer owns the client FD.
   * The RfcommAdapter becomes responsible for closing it.
   */
  [[nodiscard]] int releaseClientFd() noexcept;

  /**
   * @brief Check if client FD has been released.
   * @return true if releaseClientFd() was called.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] bool clientReleased() const noexcept { return clientReleased_; }

private:
  int serverFd_{-1};
  int clientFd_{-1};
  bool clientReleased_{false};
};

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_LOOPBACK_HPP
