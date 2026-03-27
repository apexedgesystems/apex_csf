#ifndef APEX_PROTOCOLS_SERIAL_UART_STATS_HPP
#define APEX_PROTOCOLS_SERIAL_UART_STATS_HPP
/**
 * @file UartStats.hpp
 * @brief Byte and operation statistics for UART device monitoring.
 *
 * Provides counters for monitoring UART activity without impacting
 * hot-path performance. Counters use simple increments suitable for
 * single-threaded RT contexts.
 *
 * For multi-threaded access, consider wrapping with atomics at the
 * application layer or using separate stats per thread.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* -------------------------------- UartStats -------------------------------- */

/**
 * @struct UartStats
 * @brief Byte and error counters for UART device monitoring.
 *
 * All counters are zero-initialized by default. Counters are designed for
 * single-threaded access in RT loops. For multi-threaded monitoring,
 * application code should handle synchronization.
 *
 * @note RT-safe: No allocation, simple POD struct.
 */
struct UartStats {
  std::uint64_t bytesRx = 0;         ///< Total bytes successfully received.
  std::uint64_t bytesTx = 0;         ///< Total bytes successfully transmitted.
  std::uint64_t readsCompleted = 0;  ///< Successful read operations (non-empty).
  std::uint64_t writesCompleted = 0; ///< Successful write operations (non-empty).
  std::uint64_t readWouldBlock = 0;  ///< Times read() returned WOULD_BLOCK.
  std::uint64_t writeWouldBlock = 0; ///< Times write() returned WOULD_BLOCK.
  std::uint64_t readErrors = 0;      ///< Times read() returned an ERROR_* status.
  std::uint64_t writeErrors = 0;     ///< Times write() returned an ERROR_* status.

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe: Simple assignment.
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
  }

  /**
   * @brief Get total bytes (received + transmitted).
   * @return Combined byte count.
   */
  [[nodiscard]] std::uint64_t totalBytes() const noexcept { return bytesRx + bytesTx; }

  /**
   * @brief Get total errors (read + write errors).
   * @return Combined error count.
   */
  [[nodiscard]] std::uint64_t totalErrors() const noexcept { return readErrors + writeErrors; }

  /**
   * @brief Get total operations (reads + writes completed).
   * @return Combined operation count.
   */
  [[nodiscard]] std::uint64_t totalOperations() const noexcept {
    return readsCompleted + writesCompleted;
  }

  /**
   * @brief Get total would-block events (read + write).
   * @return Combined would-block count.
   */
  [[nodiscard]] std::uint64_t totalWouldBlock() const noexcept {
    return readWouldBlock + writeWouldBlock;
  }
};

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SERIAL_UART_STATS_HPP
