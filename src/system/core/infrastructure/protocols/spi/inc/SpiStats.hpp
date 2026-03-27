#ifndef APEX_PROTOCOLS_SPI_STATS_HPP
#define APEX_PROTOCOLS_SPI_STATS_HPP
/**
 * @file SpiStats.hpp
 * @brief Byte and operation statistics for SPI device monitoring.
 *
 * Provides counters for monitoring SPI activity without impacting
 * hot-path performance. Counters use simple increments suitable for
 * single-threaded RT contexts.
 *
 * For multi-threaded access, consider wrapping with atomics at the
 * application layer or using separate stats per thread.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace spi {

/* -------------------------------- SpiStats -------------------------------- */

/**
 * @struct SpiStats
 * @brief Byte and error counters for SPI device monitoring.
 *
 * All counters are zero-initialized by default. Counters are designed for
 * single-threaded access in RT loops. For multi-threaded monitoring,
 * application code should handle synchronization.
 *
 * @note RT-safe: No allocation, simple POD struct.
 */
struct SpiStats {
  std::uint64_t bytesRx = 0;            ///< Total bytes successfully received.
  std::uint64_t bytesTx = 0;            ///< Total bytes successfully transmitted.
  std::uint64_t transfersCompleted = 0; ///< Successful transfer operations.
  std::uint64_t transferWouldBlock = 0; ///< Times transfer() returned WOULD_BLOCK.
  std::uint64_t transferErrors = 0;     ///< Times transfer() returned an ERROR_* status.

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe: Simple assignment.
   */
  void reset() noexcept {
    bytesRx = 0;
    bytesTx = 0;
    transfersCompleted = 0;
    transferWouldBlock = 0;
    transferErrors = 0;
  }

  /**
   * @brief Get total bytes (received + transmitted).
   * @return Combined byte count.
   */
  [[nodiscard]] std::uint64_t totalBytes() const noexcept { return bytesRx + bytesTx; }

  /**
   * @brief Get total errors.
   * @return Error count.
   */
  [[nodiscard]] std::uint64_t totalErrors() const noexcept { return transferErrors; }

  /**
   * @brief Get total operations completed.
   * @return Operation count.
   */
  [[nodiscard]] std::uint64_t totalOperations() const noexcept { return transfersCompleted; }
};

} // namespace spi
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SPI_STATS_HPP
