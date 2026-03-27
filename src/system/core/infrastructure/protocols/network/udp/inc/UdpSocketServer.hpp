#ifndef APEX_PROTOCOLS_UDP_SOCKET_SERVER_HPP
#define APEX_PROTOCOLS_UDP_SOCKET_SERVER_HPP
/**
 * @file UdpSocketServer.hpp
 * @brief Single-epoll, nonblocking UDP server for RT/embedded systems.
 *
 * Design:
 *  - One epoll instance owns all waiting (readable + stop) via processEvents().
 *  - Nonblocking I/O helpers (read/write) never wait; they return immediately (EAGAIN-safe).
 *  - Public API uses apex::compat::bytes_span for C++17+ portability; array overload remains for
 *    zero-alloc reads.
 *
 * Callback execution context:
 *  - onDatagramReceived runs on the event-loop thread.
 *  - Keep callbacks fast; offload heavy work to a worker pool if needed.
 *
 * RT-safety:
 *  - init(): NOT RT-safe (syscalls, memory allocation)
 *  - processEvents(): NOT RT-safe (epoll_wait syscall)
 *  - read/write: RT-safe (nonblocking syscalls, no allocation)
 *  - onDatagramReceived uses Delegate (no heap allocation)
 *  - onError uses std::function (error path, not RT-critical)
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpTypes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp" // apex::compat::bytes_span
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/helpers/inc/Fd.hpp"

#include <array>
#include <cstdint>
#include <functional> // Keep for onError_ which takes std::string&
#include <mutex>
#include <optional>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace apex {
namespace protocols {
namespace udp {

using apex::helpers::fd::UniqueFd;

/* ----------------------------- Status ----------------------------- */

/**
 * @enum UdpServerStatus
 * @brief Unique status codes for UDP server operations.
 */
enum UdpServerStatus : uint8_t {
  UDP_SERVER_SUCCESS = 0,         /**< Operation succeeded. */
  UDP_SERVER_ERR_GAI_FAILURE,     /**< Address resolution failure. */
  UDP_SERVER_ERR_SOCKET_CREATION, /**< Failed to create a UDP socket. */
  UDP_SERVER_ERR_BIND,            /**< Failed to bind the UDP socket. */
  UDP_SERVER_ERR_EPOLL_CREATION,  /**< Failed to create the epoll instance. */
  UDP_SERVER_ERR_EPOLL_CONFIG,    /**< Failed to add/modify epoll interest. */
  UDP_SERVER_ERR_INTERNAL         /**< Generic internal error. */
};

/* ----------------------------- UdpSocketServer ----------------------------- */

/**
 * @class UdpSocketServer
 * @brief Single-epoll, nonblocking UDP server for RT/embedded workloads.
 *
 * Usage:
 *   1) Construct with bind address/port, call init().
 *   2) Register callbacks.
 *   3) Call processEvents(timeoutMs) in a loop on the I/O thread.
 *   4) Call stop() to break the loop and shut down cleanly.
 */
class UdpSocketServer : public apex::protocols::ByteTrace {
public:
  /**
   * @brief Received datagram metadata.
   *
   * Contains sender address + length and the number of bytes read.
   * If nread equals the provided buffer size, the datagram may have been truncated.
   */
  struct RecvInfo {
    struct sockaddr_storage addr; /**< Sender's address. */
    socklen_t addrLen{0};         /**< Length of the sender's address. */
    ssize_t nread{-1};            /**< Bytes read (>=0), 0 if would block, or -1 on error. */
  };

  /**
   * @brief Constructs a UdpSocketServer with the specified bind address and port.
   * @param addr The bind address (empty string for INADDR_ANY).
   * @param port The port number to bind.
   * @note NOT RT-safe: Allocates string storage.
   */
  UdpSocketServer(const std::string& addr, const std::string& port);

  /**
   * @brief Initializes the server: creates, binds, and sets up the epoll instance.
   * @param error Optional output parameter for a descriptive error message.
   * @return uint8_t Status code from UdpServerStatus.
   * @note NOT RT-safe: System calls, memory allocation.
   */
  uint8_t init(std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  ~UdpSocketServer();

  // --------------------------------------------------------------------------
  // Nonblocking I/O helpers (no internal waits)
  // --------------------------------------------------------------------------

  /**
   * @brief Nonblocking recvfrom into a fixed-size array. Reads a single datagram.
   * @note timeoutMilliSecs is ignored in nonblocking mode (kept for source compatibility).
   * @return RecvInfo with sender address and bytes read (>=0); nread==0 if no data (would block).
   * @note RT-safe: Nonblocking syscall, no allocation.
   */
  template <size_t S>
  RecvInfo read(std::array<uint8_t, S>& bytes, int /*timeoutMilliSecs*/,
                std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Nonblocking recvfrom into a caller-provided buffer. Reads a single datagram.
   * @return bytes read (>=0), 0 if no data available (would block), or -1 on error.
   *         On success, fills out sender info.
   * @note RT-safe: Nonblocking syscall, no allocation.
   */
  ssize_t read(apex::compat::bytes_span bytes, RecvInfo& info,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Nonblocking sendto to the address contained in RecvInfo (e.g., reply).
   * Writes as much as the kernel will accept now. Since UDP is message-oriented, either
   * the whole datagram is sent or an error is returned.
   * @note timeoutMilliSecs is ignored in nonblocking mode (kept for source compatibility).
   * @return bytes sent (>=0), or -1 on error.
   * @note RT-safe: Nonblocking syscall, no allocation.
   */
  ssize_t write(const RecvInfo& client, apex::compat::bytes_span bytes, int /*timeoutMilliSecs*/,
                std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  // --------------------------------------------------------------------------
  // Batched I/O (Linux recvmmsg/sendmmsg for high-throughput scenarios)
  // --------------------------------------------------------------------------

  /**
   * @brief Receives multiple datagrams in a single syscall (Linux recvmmsg).
   * @param bufs Array of spans to receive into.
   * @param infos Array of RecvInfo structs to fill with sender info.
   * @param count Number of buffers/infos.
   * @return Number of datagrams received (0 to count), or -1 on error.
   * @note Falls back to single recvfrom on non-Linux or if recvmmsg unavailable.
   * @note RT-safe: Nonblocking recvmmsg/sendmmsg syscall.
   */
  int readBatch(apex::compat::bytes_span* bufs, RecvInfo* infos, size_t count);

  /**
   * @brief Sends multiple datagrams in a single syscall (Linux sendmmsg).
   * @param clients Array of RecvInfo structs with destination addresses.
   * @param bufs Array of spans containing data to send.
   * @param count Number of messages to send.
   * @return Number of datagrams sent (0 to count), or -1 on error.
   * @note Falls back to single sendto on non-Linux or if sendmmsg unavailable.
   * @note RT-safe: Nonblocking recvmmsg/sendmmsg syscall.
   */
  int writeBatch(const RecvInfo* clients, const apex::compat::bytes_span* bufs, size_t count);

  /**
   * @brief Processes events on the epoll instance (readable + stop signal).
   * This is the *only* function that calls epoll_wait().
   * @param timeoutMilliSecs Timeout in milliseconds for epoll_wait.
   * @note NOT RT-safe: Blocks on epoll_wait syscall.
   */
  void processEvents(int timeoutMilliSecs);

  /**
   * @brief Stop the event loop by waking epoll_wait(); safe to call from another thread.
   * @note RT-safe: Writes to eventfd (single syscall, no allocation).
   */
  void stop();

  /// Enables or disables lightweight logging.
  /// @note RT-safe: Sets a boolean flag.
  void enableLogging(bool enable);

  /// Sets a simple numeric log level (0 = minimal).
  /// @note RT-safe: Sets an integer field.
  void setLogLevel(int level);

  /**
   * @brief Registers a callback invoked when the UDP socket becomes readable.
   * @param callback RT-safe delegate: void(void* ctx) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnDatagramReceived(apex::concurrency::Delegate<void> callback);

  /// Registers a callback invoked when an error condition should be surfaced.
  /// @note RT-safe: No heap allocation.
  void setOnError(const std::function<void(const std::string&)>& callback);

  // --------------------------------------------------------------------------
  // Statistics
  // --------------------------------------------------------------------------

  /**
   * @brief Returns server statistics.
   * @note RT-safe: Returns a copy of the stats struct (no allocation).
   */
  DatagramStats stats() const noexcept { return stats_; }

  /**
   * @brief Resets all statistics to zero.
   * @note RT-safe: Simple assignment, no allocation.
   */
  void resetStats() noexcept { stats_ = DatagramStats{}; }

  // --------------------------------------------------------------------------
  // UDP-specific policy & features (set before or after init; applied when possible)
  // --------------------------------------------------------------------------

  /// Enable SO_REUSEPORT fan-out (best effort; may need to be set before bind).
  /// @note NOT RT-safe: setsockopt syscall.
  void setReusePort(bool on) { reusePort_ = on; }

  /// Enable IPv4 broadcast (SO_BROADCAST).
  /// @note NOT RT-safe: setsockopt syscall.
  void setBroadcast(bool on) { broadcast_ = on; }

  /// Set IPv4 DSCP/TOS (IP_TOS) and IPv6 Traffic Class (IPV6_TCLASS).
  /// @note NOT RT-safe: setsockopt syscall.
  void setTosDscp(int tos) { tosDscp_ = tos; } // -1 = leave default
  /// @note NOT RT-safe: setsockopt syscall.
  void setV6TrafficClass(int tclass) { v6TClass_ = tclass; } // -1 = leave default

  /// Control delivery of per-packet interface/dst info via ancillary data.
  /// @note NOT RT-safe: setsockopt syscall.
  void setPktInfoV4(bool on) { pktinfoV4_ = on; }
  /// @note NOT RT-safe: setsockopt syscall.
  void setPktInfoV6(bool on) { pktinfoV6_ = on; }

  /// Bind all traffic to a specific interface (Linux: SO_BINDTODEVICE).
  /// @note NOT RT-safe: setsockopt syscall.
  void bindToDevice(const std::string& ifname) { bindDevice_ = ifname; }

  /// Multicast helpers (IPv4/IPv6). Addresses are in network byte order where applicable.

  /// @note NOT RT-safe: setsockopt syscall.
  bool joinMulticastV4(uint32_t groupBe, uint32_t ifaceBe /*0=INADDR_ANY*/);
  /// @note NOT RT-safe: setsockopt syscall.
  bool leaveMulticastV4(uint32_t groupBe, uint32_t ifaceBe);
  /// @note NOT RT-safe: setsockopt syscall.
  bool joinMulticastV6(const void* group16, unsigned int ifindex);
  /// @note NOT RT-safe: setsockopt syscall.
  bool leaveMulticastV6(const void* group16, unsigned int ifindex);
  /// @note NOT RT-safe: setsockopt syscall.
  bool setMulticastLoopV4(bool on);
  /// @note NOT RT-safe: setsockopt syscall.
  bool setMulticastTtlV4(int ttl);
  /// @note NOT RT-safe: setsockopt syscall.
  bool setMulticastHopsV6(int hops);

private:
  // RAII-managed descriptors
  UniqueFd sockfd_; /**< UDP socket. */
  UniqueFd epfd_;   /**< Epoll instance. */
  UniqueFd stopfd_; /**< Eventfd used to break epoll_wait() in stop(). */

  // Bind details
  const std::string addr_; /**< Bind address. */
  const std::string port_; /**< Port number. */
  const std::string desc_; /**< Concatenated description for logging. */

  // Logging
  bool loggingEnabled_{false};
  std::string rxLogFile_;
  std::string txLogFile_;
  int logLevel_{0};

  // UDP policy (applied in init() when possible; some can be toggled post-init)
  bool reusePort_{false};
  bool broadcast_{false};
  int tosDscp_{-1};
  int v6TClass_{-1};
  bool pktinfoV4_{false};
  bool pktinfoV6_{false};
  std::string bindDevice_; // empty = none

  // Callback storage.
  apex::concurrency::Delegate<void> onDatagramReceived_; // RT-safe, no heap allocation
  std::function<void(const std::string&)> onError_;      // Error path, not RT-critical
  std::mutex callbackMutex_;

  // Statistics
  DatagramStats stats_{};
};

} // namespace udp
} // namespace protocols
} // namespace apex

#include "src/system/core/infrastructure/protocols/network/udp/src/UdpSocketServer.tpp"

#endif // APEX_PROTOCOLS_UDP_SOCKET_SERVER_HPP
