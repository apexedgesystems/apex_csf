#ifndef APEX_PROTOCOLS_FIELDBUS_LIN_CONTROLLER_HPP
#define APEX_PROTOCOLS_FIELDBUS_LIN_CONTROLLER_HPP
/**
 * @file LinController.hpp
 * @brief LIN master/slave controller with UART device composition.
 *
 * Provides a LIN 2.x compliant implementation that composes over a UartDevice
 * for physical layer communication. Supports both master and slave modes,
 * classic and enhanced checksums, break generation, and collision detection.
 *
 * Design Notes:
 *  - Composition over UART rather than inheritance for flexibility.
 *  - Break field generation via TCSBRK ioctl (Linux) or baud rate trick.
 *  - No heap allocation on the I/O path.
 *  - Thread safety: NOT thread-safe; external synchronization required.
 *
 * RT-Safety:
 *  - configure(): NOT RT-safe (performs termios setup).
 *  - Master operations (sendHeader, sendFrame, requestFrame): RT-safe after configure.
 *  - Slave operations (waitForHeader, respondToHeader): RT-safe after configure.
 *
 * Master Usage:
 * @code
 *   LinController lin(uart);
 *   lin.configure(cfg);
 *
 *   // Request data from slave (header + wait for response)
 *   FrameBuffer response;
 *   ParsedFrame parsed;
 *   lin.requestFrame(0x10, response, parsed);
 *
 *   // Publish data as master (header + data + checksum)
 *   lin.sendFrame(0x10, data, 4);
 * @endcode
 *
 * Slave Usage:
 * @code
 *   LinController lin(uart);
 *   lin.configure(cfg);
 *
 *   // Wait for master header
 *   std::uint8_t frameId;
 *   lin.waitForHeader(frameId);
 *
 *   // Respond with data if this is our frame
 *   if (frameId == 0x10) {
 *     lin.respondToHeader(frameId, data, 4);
 *   }
 * @endcode
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinConfig.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinFrame.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinStatus.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartDevice.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace lin {

/* ----------------------------- LinStats ----------------------------- */

/**
 * @struct LinStats
 * @brief Statistics for LIN controller operations.
 */
struct LinStats {
  std::uint64_t framesSent{0};     ///< Total frames sent (header or full).
  std::uint64_t framesReceived{0}; ///< Total responses received.
  std::uint64_t checksumErrors{0}; ///< Checksum validation failures.
  std::uint64_t parityErrors{0};   ///< PID parity errors.
  std::uint64_t syncErrors{0};     ///< Sync field errors.
  std::uint64_t timeouts{0};       ///< Response timeouts.
  std::uint64_t collisions{0};     ///< Bus collision detections.
  std::uint64_t breakErrors{0};    ///< Break generation/detection errors.

  /**
   * @brief Reset all statistics to zero.
   * @note RT-safe: O(1).
   */
  void reset() noexcept {
    framesSent = 0;
    framesReceived = 0;
    checksumErrors = 0;
    parityErrors = 0;
    syncErrors = 0;
    timeouts = 0;
    collisions = 0;
    breakErrors = 0;
  }
};

/* ----------------------------- LinController ----------------------------- */

/**
 * @class LinController
 * @brief LIN master/slave controller with UART device composition.
 *
 * Implements LIN 2.x master and slave functionality over a UART device.
 * The controller does not own the UART device; the caller manages its lifetime.
 *
 * Inherits from ByteTrace to provide optional byte-level tracing.
 * Call attachTrace() and setTraceEnabled(true) to enable tracing.
 */
class LinController : public apex::protocols::ByteTrace {
public:
  /**
   * @brief Construct controller with UART device reference.
   * @param uart Reference to configured UART device.
   * @note The UART device must outlive this controller.
   */
  explicit LinController(apex::protocols::serial::uart::UartDevice& uart) noexcept;

  /**
   * @brief Destructor.
   */
  ~LinController() = default;

  // Non-copyable, non-movable (holds reference to UART)
  LinController(const LinController&) = delete;
  LinController& operator=(const LinController&) = delete;
  LinController(LinController&&) = delete;
  LinController& operator=(LinController&&) = delete;

  /**
   * @brief Configure the LIN controller.
   * @param config LIN configuration parameters.
   * @return SUCCESS if configured, ERROR_* on failure.
   * @note NOT RT-safe: Configures underlying UART.
   */
  [[nodiscard]] Status configure(const LinConfig& config) noexcept;

  /**
   * @brief Check if controller is configured and ready.
   * @return true if configured.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isConfigured() const noexcept;

  /**
   * @brief Get current configuration.
   * @return Reference to current configuration.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const LinConfig& config() const noexcept;

  /**
   * @brief Send LIN break field.
   * @return SUCCESS if break sent, ERROR_* on failure.
   * @note RT-safe: Bounded syscall.
   *
   * Generates break field via UART break mechanism.
   */
  [[nodiscard]] Status sendBreak() noexcept;

  /**
   * @brief Send LIN header (break + sync + PID).
   * @param frameId 6-bit frame ID (0-63).
   * @return SUCCESS if header sent, ERROR_* on failure.
   * @note RT-safe: Bounded syscalls.
   *
   * Sends the complete LIN header sequence:
   *  1. Break field (via UART break)
   *  2. Sync byte (0x55)
   *  3. Protected ID (frameId with parity)
   */
  [[nodiscard]] Status sendHeader(std::uint8_t frameId) noexcept;

  /**
   * @brief Receive slave response after header.
   * @param frameId Frame ID (for checksum calculation).
   * @param response Output buffer for response data.
   * @param parsed Output: parsed response fields.
   * @return SUCCESS if response received, ERROR_* on failure/timeout.
   * @note RT-safe: Bounded by response timeout.
   *
   * Waits for and parses slave response (data + checksum).
   * Data length is determined from frame ID or config.
   */
  [[nodiscard]] Status receiveResponse(std::uint8_t frameId, FrameBuffer& response,
                                       ParsedFrame& parsed) noexcept;

  /**
   * @brief Receive response with explicit data length.
   * @param frameId Frame ID.
   * @param dataLen Expected data length.
   * @param response Output buffer.
   * @param parsed Output: parsed response.
   * @return SUCCESS if response received, ERROR_* on failure.
   * @note RT-safe: Bounded by response timeout.
   */
  [[nodiscard]] Status receiveResponse(std::uint8_t frameId, std::size_t dataLen,
                                       FrameBuffer& response, ParsedFrame& parsed) noexcept;

  /**
   * @brief Send complete frame (header + data + checksum) as master.
   * @param frameId 6-bit frame ID.
   * @param data Frame data bytes.
   * @param dataLen Number of data bytes (1-8).
   * @return SUCCESS if frame sent, ERROR_* on failure.
   * @note RT-safe: Bounded syscalls.
   *
   * Used for master-published frames where master provides the data.
   * Includes collision detection if enabled.
   */
  [[nodiscard]] Status sendFrame(std::uint8_t frameId, const std::uint8_t* data,
                                 std::size_t dataLen) noexcept;

  /**
   * @brief Request frame from slave (master sends header, waits for response).
   * @param frameId 6-bit frame ID.
   * @param response Output buffer for response data.
   * @param parsed Output: parsed response fields.
   * @return SUCCESS if response received, ERROR_* on failure/timeout.
   * @note RT-safe: Bounded by response timeout.
   *
   * Convenience method combining sendHeader() + receiveResponse().
   */
  [[nodiscard]] Status requestFrame(std::uint8_t frameId, FrameBuffer& response,
                                    ParsedFrame& parsed) noexcept;

  /**
   * @brief Request frame with explicit data length.
   * @param frameId 6-bit frame ID.
   * @param dataLen Expected data length.
   * @param response Output buffer.
   * @param parsed Output: parsed response.
   * @return SUCCESS if response received, ERROR_* on failure.
   * @note RT-safe: Bounded by response timeout.
   */
  [[nodiscard]] Status requestFrame(std::uint8_t frameId, std::size_t dataLen,
                                    FrameBuffer& response, ParsedFrame& parsed) noexcept;

  /* ----------------------------- Slave API ----------------------------- */

  /**
   * @brief Wait for LIN header from master (slave mode).
   * @param frameId Output: received frame ID (6-bit, parity verified).
   * @return SUCCESS if header received, ERROR_* on failure/timeout.
   * @note RT-safe: Bounded by response timeout.
   *
   * Waits for break + sync + PID sequence from master.
   * Validates sync byte and PID parity.
   */
  [[nodiscard]] Status waitForHeader(std::uint8_t& frameId) noexcept;

  /**
   * @brief Wait for header with explicit timeout.
   * @param frameId Output: received frame ID.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if header received, ERROR_* on failure.
   * @note RT-safe: Bounded by timeout.
   */
  [[nodiscard]] Status waitForHeader(std::uint8_t& frameId, std::uint16_t timeoutMs) noexcept;

  /**
   * @brief Respond to header with data (slave mode).
   * @param frameId Frame ID (for checksum calculation).
   * @param data Response data bytes.
   * @param dataLen Number of data bytes (1-8).
   * @return SUCCESS if response sent, ERROR_* on failure.
   * @note RT-safe: Bounded syscalls.
   *
   * Sends data + checksum in response to master header.
   * Must be called after waitForHeader() for the same frameId.
   */
  [[nodiscard]] Status respondToHeader(std::uint8_t frameId, const std::uint8_t* data,
                                       std::size_t dataLen) noexcept;

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const LinStats& stats() const noexcept;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe: O(1).
   */
  void resetStats() noexcept;

private:
  apex::protocols::serial::uart::UartDevice& uart_; ///< UART device reference.
  LinConfig config_{};                              ///< Current configuration.
  LinStats stats_{};                                ///< Statistics.
  bool configured_{false};                          ///< Configuration flag.

  /**
   * @brief Write bytes with collision detection.
   * @param data Data to write.
   * @param len Number of bytes.
   * @return SUCCESS if written without collision.
   * @note RT-safe: Bounded syscalls.
   */
  [[nodiscard]] Status writeWithCollisionCheck(const std::uint8_t* data, std::size_t len) noexcept;

  /**
   * @brief Read bytes with inter-byte timeout.
   * @param buffer Output buffer.
   * @param count Number of bytes to read.
   * @param bytesRead Output: actual bytes read.
   * @return SUCCESS if all bytes read, ERROR_* on failure/timeout.
   * @note RT-safe: Bounded by timeout.
   */
  [[nodiscard]] Status readBytes(std::uint8_t* buffer, std::size_t count,
                                 std::size_t& bytesRead) noexcept;
};

} // namespace lin
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_LIN_CONTROLLER_HPP
