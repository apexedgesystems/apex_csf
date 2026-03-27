#ifndef APEX_PROTOCOLS_FIELDBUS_CAN_CAN_STATS_HPP
#define APEX_PROTOCOLS_FIELDBUS_CAN_CAN_STATS_HPP
/**
 * @file CanStats.hpp
 * @brief Frame statistics for CAN device operations.
 *
 * Provides counters for monitoring CAN bus activity without impacting
 * hot-path performance. Counters use simple increments suitable for
 * single-threaded RT contexts.
 *
 * For multi-threaded access, consider wrapping with atomics at the
 * application layer or using separate stats per thread.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

/* -------------------------------- CanStats -------------------------------- */

/**
 * @struct CanStats
 * @brief Frame and error counters for CAN device monitoring.
 *
 * All counters are zero-initialized by default. Counters are designed for
 * single-threaded access in RT loops. For multi-threaded monitoring,
 * application code should handle synchronization.
 *
 * @note RT-safe: No allocation, simple POD struct.
 */
struct CanStats {
  std::uint64_t framesSent = 0;       ///< Total frames successfully transmitted.
  std::uint64_t framesReceived = 0;   ///< Total frames successfully received.
  std::uint64_t errorFrames = 0;      ///< Error frames received (canId.error == true).
  std::uint64_t sendWouldBlock = 0;   ///< Times send() returned WOULD_BLOCK.
  std::uint64_t recvWouldBlock = 0;   ///< Times recv() returned WOULD_BLOCK.
  std::uint64_t sendErrors = 0;       ///< Times send() returned an ERROR_* status.
  std::uint64_t recvErrors = 0;       ///< Times recv() returned an ERROR_* status.
  std::uint64_t bytesTransmitted = 0; ///< Total payload bytes sent (sum of dlc).
  std::uint64_t bytesReceived = 0;    ///< Total payload bytes received (sum of dlc).

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe: Simple memset-equivalent.
   */
  void reset() noexcept {
    framesSent = 0;
    framesReceived = 0;
    errorFrames = 0;
    sendWouldBlock = 0;
    recvWouldBlock = 0;
    sendErrors = 0;
    recvErrors = 0;
    bytesTransmitted = 0;
    bytesReceived = 0;
  }

  /**
   * @brief Get total frames (sent + received).
   * @return Combined frame count.
   * @note RT-safe: no allocation or I/O
   */
  [[nodiscard]] std::uint64_t totalFrames() const noexcept { return framesSent + framesReceived; }

  /**
   * @brief Get total errors (send + recv + error frames).
   * @return Combined error count.
   * @note RT-safe: no allocation or I/O
   */
  [[nodiscard]] std::uint64_t totalErrors() const noexcept {
    return sendErrors + recvErrors + errorFrames;
  }

  /**
   * @brief Get total bytes (transmitted + received).
   * @return Combined byte count.
   * @note RT-safe: no allocation or I/O
   */
  [[nodiscard]] std::uint64_t totalBytes() const noexcept {
    return bytesTransmitted + bytesReceived;
  }
};

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_CAN_CAN_STATS_HPP
