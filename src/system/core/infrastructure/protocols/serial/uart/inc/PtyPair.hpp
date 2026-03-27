#ifndef APEX_PROTOCOLS_SERIAL_UART_PTY_PAIR_HPP
#define APEX_PROTOCOLS_SERIAL_UART_PTY_PAIR_HPP
/**
 * @file PtyPair.hpp
 * @brief Pseudo-terminal pair utility for UART testing without hardware.
 *
 * Creates a master/slave PTY pair where:
 *  - The slave path can be opened by UartAdapter as if it were real hardware.
 *  - The master FD is used by test code to inject/capture data.
 *
 * This allows testing UART code paths without physical serial ports.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStatus.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* ----------------------------- PtyPair ----------------------------- */

/**
 * @class PtyPair
 * @brief Creates and manages a pseudo-terminal pair for testing.
 *
 * Usage:
 * @code
 *   PtyPair pty;
 *   if (pty.open() == Status::SUCCESS) {
 *     // UartAdapter can open pty.slavePath() like "/dev/pts/X"
 *     UartAdapter adapter(pty.slavePath());
 *
 *     // Test code writes to master, adapter reads from slave
 *     pty.writeMaster(testData, sizeof(testData), written, 100);
 *
 *     // Adapter writes to slave, test code reads from master
 *     pty.readMaster(buffer, sizeof(buffer), bytesRead, 100);
 *   }
 * @endcode
 *
 * The PTY pair is closed automatically on destruction.
 *
 * @note NOT RT-safe: Uses heap allocation for paths, system calls for PTY.
 */
class PtyPair {
public:
  PtyPair() = default;
  ~PtyPair();

  PtyPair(const PtyPair&) = delete;
  PtyPair& operator=(const PtyPair&) = delete;
  PtyPair(PtyPair&& other) noexcept;
  PtyPair& operator=(PtyPair&& other) noexcept;

  /**
   * @brief Create the PTY pair.
   * @return SUCCESS if created, ERROR_IO on failure.
   */
  [[nodiscard]] Status open() noexcept;

  /**
   * @brief Close the PTY pair and release resources.
   * @return SUCCESS on success, ERROR_* on failure.
   */
  [[nodiscard]] Status close() noexcept;

  /**
   * @brief Check if the PTY pair is open.
   * @return true if open.
   */
  [[nodiscard]] bool isOpen() const noexcept;

  /**
   * @brief Get the slave device path.
   * @return Path like "/dev/pts/X", or empty if not open.
   */
  [[nodiscard]] const char* slavePath() const noexcept;

  /**
   * @brief Get the master file descriptor.
   * @return Master FD, or -1 if not open.
   * @note Exposed for epoll/select integration in tests.
   */
  [[nodiscard]] int masterFd() const noexcept;

  /**
   * @brief Read data from the master side.
   * @param buffer Destination buffer.
   * @param bufferSize Buffer size in bytes.
   * @param bytesRead Output: bytes actually read.
   * @param timeoutMs Timeout in milliseconds (<0 block, 0 poll, >0 bounded).
   * @return SUCCESS if read, WOULD_BLOCK if not ready, ERROR_* on failure.
   */
  [[nodiscard]] Status readMaster(std::uint8_t* buffer, std::size_t bufferSize,
                                  std::size_t& bytesRead, int timeoutMs) noexcept;

  /**
   * @brief Write data to the master side.
   * @param data Source data.
   * @param dataSize Data size in bytes.
   * @param bytesWritten Output: bytes actually written.
   * @param timeoutMs Timeout in milliseconds (<0 block, 0 poll, >0 bounded).
   * @return SUCCESS if written, WOULD_BLOCK if not ready, ERROR_* on failure.
   */
  [[nodiscard]] Status writeMaster(const std::uint8_t* data, std::size_t dataSize,
                                   std::size_t& bytesWritten, int timeoutMs) noexcept;

private:
  int masterFd_ = -1;
  int slaveFd_ = -1;
  std::string slavePath_;
};

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SERIAL_UART_PTY_PAIR_HPP
