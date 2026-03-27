#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_STATS_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_STATS_HPP
/**
 * @file ModbusStats.hpp
 * @brief Statistics tracking for Modbus operations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- ModbusStats ----------------------------- */

/**
 * @struct ModbusStats
 * @brief Cumulative statistics for Modbus master/client operations.
 *
 * Thread Safety:
 *  - Reading stats is thread-safe (returns a copy).
 *  - Updating stats is NOT thread-safe (caller must synchronize).
 *  - For RT applications, copy the struct for analysis rather than
 *    accessing fields during I/O operations.
 *
 * RT-Safety:
 *  - All methods are O(1) with no allocation.
 */
struct ModbusStats {
  // Transaction counters
  std::uint64_t requestsSent{0};       ///< Total requests sent.
  std::uint64_t responsesReceived{0};  ///< Total valid responses received.
  std::uint64_t exceptionsReceived{0}; ///< Total Modbus exception responses received.

  // Byte counters
  std::uint64_t bytesTx{0}; ///< Total bytes transmitted.
  std::uint64_t bytesRx{0}; ///< Total bytes received.

  // Error counters
  std::uint64_t crcErrors{0};   ///< CRC validation failures (RTU only).
  std::uint64_t frameErrors{0}; ///< Malformed frame errors.
  std::uint64_t timeouts{0};    ///< Response timeout errors.
  std::uint64_t ioErrors{0};    ///< Transport I/O errors.

  // Timing (optional, for diagnostics)
  std::uint64_t lastResponseTimeUs{0}; ///< Last response round-trip time in microseconds.

  /* ----------------------------- Methods ----------------------------- */

  /**
   * @brief Reset all statistics to zero.
   * @note RT-safe: O(1), no allocation.
   */
  void reset() noexcept {
    requestsSent = 0;
    responsesReceived = 0;
    exceptionsReceived = 0;
    bytesTx = 0;
    bytesRx = 0;
    crcErrors = 0;
    frameErrors = 0;
    timeouts = 0;
    ioErrors = 0;
    lastResponseTimeUs = 0;
  }

  /**
   * @brief Total bytes transferred (TX + RX).
   * @return Sum of bytesTx and bytesRx.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint64_t totalBytes() const noexcept { return bytesTx + bytesRx; }

  /**
   * @brief Total transactions (requests sent).
   * @return Number of requests sent.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint64_t totalTransactions() const noexcept { return requestsSent; }

  /**
   * @brief Total successful transactions (valid responses, including exceptions).
   * @return responsesReceived + exceptionsReceived.
   * @note RT-safe: O(1).
   *
   * Note: Modbus exception responses are "successful" from a protocol standpoint
   * (the slave responded correctly), even though the operation was rejected.
   */
  [[nodiscard]] std::uint64_t totalResponses() const noexcept {
    return responsesReceived + exceptionsReceived;
  }

  /**
   * @brief Total errors (CRC, frame, timeout, I/O).
   * @return Sum of all error counters.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint64_t totalErrors() const noexcept {
    return crcErrors + frameErrors + timeouts + ioErrors;
  }

  /**
   * @brief Calculate success rate as a percentage.
   * @return Success rate (0.0 to 100.0), or 0.0 if no transactions.
   * @note RT-safe: O(1).
   *
   * Success = (responsesReceived / requestsSent) * 100.
   * Does not count exceptions as failures (they are valid responses).
   */
  [[nodiscard]] double successRate() const noexcept {
    if (requestsSent == 0) {
      return 0.0;
    }
    return (static_cast<double>(responsesReceived) / static_cast<double>(requestsSent)) * 100.0;
  }
};

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_STATS_HPP
