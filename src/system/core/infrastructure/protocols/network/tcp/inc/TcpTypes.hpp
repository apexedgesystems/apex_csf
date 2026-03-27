#ifndef APEX_PROTOCOLS_TCP_TYPES_HPP
#define APEX_PROTOCOLS_TCP_TYPES_HPP
/**
 * @file TcpTypes.hpp
 * @brief Shared types for TCP client and server implementations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace tcp {

/* ----------------------------- ConnectionStats ----------------------------- */

/**
 * @struct ConnectionStats
 * @brief Per-connection statistics for monitoring and diagnostics.
 *
 * All counters are cumulative since connection establishment.
 * Thread-safe for reading; writes happen only on the I/O thread.
 */
struct ConnectionStats {
  uint64_t bytesRx{0};       /**< Total bytes received. */
  uint64_t bytesTx{0};       /**< Total bytes transmitted. */
  uint64_t packetsRx{0};     /**< Total packets/messages received. */
  uint64_t packetsTx{0};     /**< Total packets/messages transmitted. */
  uint32_t errorsRx{0};      /**< Receive errors (EAGAIN excluded). */
  uint32_t errorsTx{0};      /**< Transmit errors (EAGAIN excluded). */
  int64_t connectedAtNs{0};  /**< Connection time (steady_clock nanoseconds). */
  int64_t lastActivityNs{0}; /**< Last I/O activity (steady_clock nanoseconds). */
};

} // namespace tcp
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_TCP_TYPES_HPP
