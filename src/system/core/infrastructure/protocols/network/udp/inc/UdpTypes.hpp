#ifndef APEX_PROTOCOLS_UDP_TYPES_HPP
#define APEX_PROTOCOLS_UDP_TYPES_HPP
/**
 * @file UdpTypes.hpp
 * @brief Shared types for UDP client and server implementations.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace udp {

/* ----------------------------- DatagramStats ----------------------------- */

/**
 * @struct DatagramStats
 * @brief Statistics for monitoring and diagnostics.
 *
 * All counters are cumulative since initialization.
 * Thread-safe for reading; writes happen only on the I/O thread.
 */
struct DatagramStats {
  uint64_t bytesRx{0};       /**< Total bytes received. */
  uint64_t bytesTx{0};       /**< Total bytes transmitted. */
  uint64_t datagramsRx{0};   /**< Total datagrams received. */
  uint64_t datagramsTx{0};   /**< Total datagrams transmitted. */
  uint32_t errorsRx{0};      /**< Receive errors (EAGAIN excluded). */
  uint32_t errorsTx{0};      /**< Transmit errors (EAGAIN excluded). */
  int64_t initAtNs{0};       /**< Initialization time (steady_clock nanoseconds). */
  int64_t lastActivityNs{0}; /**< Last I/O activity (steady_clock nanoseconds). */
};

} // namespace udp
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_UDP_TYPES_HPP
