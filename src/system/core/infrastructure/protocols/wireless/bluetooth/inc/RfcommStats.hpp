#ifndef APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_STATS_HPP
#define APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_STATS_HPP
/**
 * @file RfcommStats.hpp
 * @brief Statistics tracking for Bluetooth RFCOMM operations.
 *
 * Provides counters for monitoring communication health and performance.
 * Follows the unified statistics pattern used by UART, Modbus, and LIN.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- RfcommStats ----------------------------- */

/**
 * @struct RfcommStats
 * @brief Statistics for RFCOMM operations.
 *
 * All counters are monotonically increasing. Call reset() to clear.
 * All methods are RT-safe (no allocation, O(1) operations).
 */
struct RfcommStats {
  std::uint64_t bytesRx{0};          ///< Total bytes received
  std::uint64_t bytesTx{0};          ///< Total bytes transmitted
  std::uint64_t readsCompleted{0};   ///< Successful read operations
  std::uint64_t writesCompleted{0};  ///< Successful write operations
  std::uint64_t readWouldBlock{0};   ///< Times read returned WOULD_BLOCK
  std::uint64_t writeWouldBlock{0};  ///< Times write returned WOULD_BLOCK
  std::uint64_t readErrors{0};       ///< Read errors (ERROR_*)
  std::uint64_t writeErrors{0};      ///< Write errors (ERROR_*)
  std::uint64_t connectAttempts{0};  ///< Connection attempts
  std::uint64_t connectSuccesses{0}; ///< Successful connections
  std::uint64_t disconnects{0};      ///< Disconnections (intentional or remote)

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe: O(1), no allocation.
   */
  void reset() noexcept {
    bytesRx = 0;
    bytesTx = 0;
    readsCompleted = 0;
    writesCompleted = 0;
    readWouldBlock = 0;
    writeWouldBlock = 0;
    readErrors = 0;
    writeErrors = 0;
    connectAttempts = 0;
    connectSuccesses = 0;
    disconnects = 0;
  }

  /**
   * @brief Get total bytes transferred (rx + tx).
   * @return Combined byte count.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] std::uint64_t totalBytes() const noexcept { return bytesRx + bytesTx; }

  /**
   * @brief Get total error count (read + write).
   * @return Combined error count.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] std::uint64_t totalErrors() const noexcept { return readErrors + writeErrors; }

  /**
   * @brief Get total completed operations (read + write).
   * @return Combined operation count.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] std::uint64_t totalOperations() const noexcept {
    return readsCompleted + writesCompleted;
  }

  /**
   * @brief Get total WOULD_BLOCK count (read + write).
   * @return Combined would-block count.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] std::uint64_t totalWouldBlock() const noexcept {
    return readWouldBlock + writeWouldBlock;
  }

  /**
   * @brief Get connection success rate (0.0 to 1.0).
   * @return Success rate, or 1.0 if no attempts.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] double connectionSuccessRate() const noexcept {
    if (connectAttempts == 0) {
      return 1.0;
    }
    return static_cast<double>(connectSuccesses) / static_cast<double>(connectAttempts);
  }
};

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_STATS_HPP
