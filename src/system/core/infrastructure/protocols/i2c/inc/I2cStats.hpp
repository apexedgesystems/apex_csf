#ifndef APEX_PROTOCOLS_I2C_STATS_HPP
#define APEX_PROTOCOLS_I2C_STATS_HPP
/**
 * @file I2cStats.hpp
 * @brief Byte and operation statistics for I2C device monitoring.
 *
 * Provides counters for monitoring I2C activity without impacting
 * hot-path performance. Counters use simple increments suitable for
 * single-threaded RT contexts.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace i2c {

/* -------------------------------- I2cStats -------------------------------- */

/**
 * @struct I2cStats
 * @brief Byte and error counters for I2C device monitoring.
 *
 * All counters are zero-initialized by default. Counters are designed for
 * single-threaded access in RT loops.
 *
 * @note RT-safe: No allocation, simple POD struct.
 */
struct I2cStats {
  std::uint64_t bytesRx = 0;         ///< Total bytes successfully received.
  std::uint64_t bytesTx = 0;         ///< Total bytes successfully transmitted.
  std::uint64_t readsCompleted = 0;  ///< Successful read operations.
  std::uint64_t writesCompleted = 0; ///< Successful write operations.
  std::uint64_t readWouldBlock = 0;  ///< Times read() returned WOULD_BLOCK.
  std::uint64_t writeWouldBlock = 0; ///< Times write() returned WOULD_BLOCK.
  std::uint64_t readErrors = 0;      ///< Times read() returned an ERROR_* status.
  std::uint64_t writeErrors = 0;     ///< Times write() returned an ERROR_* status.
  std::uint64_t nackCount = 0;       ///< Times a NACK was received.

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
    nackCount = 0;
  }

  /**
   * @brief Get total bytes (received + transmitted).
   * @return Combined byte count.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] std::uint64_t totalBytes() const noexcept { return bytesRx + bytesTx; }

  /**
   * @brief Get total errors (read + write errors).
   * @return Combined error count.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] std::uint64_t totalErrors() const noexcept { return readErrors + writeErrors; }

  /**
   * @brief Get total operations (reads + writes completed).
   * @return Combined operation count.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] std::uint64_t totalOperations() const noexcept {
    return readsCompleted + writesCompleted;
  }
};

} // namespace i2c
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_I2C_STATS_HPP
