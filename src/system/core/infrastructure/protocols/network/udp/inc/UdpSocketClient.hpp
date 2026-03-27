#ifndef APEX_PROTOCOLS_UDP_SOCKET_CLIENT_HPP
#define APEX_PROTOCOLS_UDP_SOCKET_CLIENT_HPP
/**
 * @file UdpSocketClient.hpp
 * @brief Single-epoll, nonblocking UDP client for RT/embedded systems.
 *
 * Design:
 *  - One epoll instance owns all waiting via processEvents(); I/O helpers never wait.
 *  - Supports CONNECTED (connect()) and UNCONNECTED (bind-only) modes.
 *  - Public API uses apex::compat::bytes_span for C++17+ portability; array overload remains.
 *
 * Callback execution context:
 *  - onReadable / onWritable run on the event-loop thread.
 *  - Keep callbacks fast; offload heavy work to a worker pool if needed.
 *
 * RT-safety:
 *  - init(): NOT RT-safe (syscalls, memory allocation)
 *  - processEvents(): NOT RT-safe (epoll_wait syscall)
 *  - read/write: RT-safe (nonblocking syscalls, no allocation)
 *  - Callbacks use Delegate (no heap allocation)
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpTypes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp" // apex::compat::bytes_span
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/helpers/inc/Fd.hpp"

#include <array>
#include <cstdint>
#include <functional>
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
 * @enum UdpClientStatus
 * @brief Unique status codes for UDP client operations.
 */
enum UdpClientStatus : uint8_t {
  UDP_CLIENT_SUCCESS = 0,            /**< Operation succeeded. */
  UDP_CLIENT_ERR_GAI_FAILURE,        /**< Address resolution failure. */
  UDP_CLIENT_ERR_SOCKET_CREATION,    /**< Failed to create socket. */
  UDP_CLIENT_ERR_CONNECTION_FAILURE, /**< Failed to connect (CONNECTED mode). */
  UDP_CLIENT_ERR_BIND_FAILURE,       /**< Failed to bind (UNCONNECTED mode). */
  UDP_CLIENT_ERR_EPOLL_CREATION,     /**< Failed to create the epoll instance. */
  UDP_CLIENT_ERR_EPOLL_CONFIG,       /**< Failed to add/modify epoll interest. */
  UDP_CLIENT_ERR_INTERNAL            /**< Generic internal error. */
};

/* ----------------------------- UdpSocketMode ----------------------------- */

/**
 * @enum UdpSocketMode
 * @brief Operating modes for the UDP socket.
 */
enum class UdpSocketMode {
  CONNECTED,  /**< connect() to fixed remote; use read()/write(). */
  UNCONNECTED /**< bind() locally; use readFrom()/writeTo().     */
};

/* ----------------------------- UdpSocketClient ----------------------------- */

class UdpSocketClient : public apex::protocols::ByteTrace {
public:
  /**
   * @brief Sender/peer metadata for UNCONNECTED reads and convenience APIs.
   */
  struct PeerInfo {
    struct sockaddr_storage addr;
    socklen_t addrLen{0};
  };

  /// Constructs a UdpSocketClient. CONNECTED: addr/port is remote; UNCONNECTED: local bind.
  /// @note NOT RT-safe: Allocates string storage.
  UdpSocketClient(const std::string& addr, const std::string& port);

  /// Initializes the socket and epoll. CONNECTED => connect(); UNCONNECTED => bind().
  /// @note NOT RT-safe: System calls, memory allocation.
  uint8_t init(UdpSocketMode mode = UdpSocketMode::CONNECTED,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  ~UdpSocketClient();

  // --------------------------------------------------------------------------
  // Nonblocking I/O helpers (no internal waits)
  // --------------------------------------------------------------------------

  /// Nonblocking recv (CONNECTED) into a fixed-size array (timeout ignored).
  /// @note RT-safe: Nonblocking syscall, no allocation.
  template <size_t S>
  ssize_t read(std::array<uint8_t, S>& bytes, int /*timeoutMilliSecs*/,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /// Nonblocking recvfrom (UNCONNECTED) into a fixed-size array + peer info (timeout ignored).
  /// @note RT-safe: Nonblocking syscall, no allocation.
  template <size_t S>
  ssize_t readFrom(std::array<uint8_t, S>& bytes, PeerInfo& from, int /*timeoutMilliSecs*/,
                   std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /// Nonblocking recv (CONNECTED) into caller buffer.
  /// @note RT-safe: Nonblocking syscall, no allocation.
  ssize_t read(apex::compat::bytes_span bytes,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /// Nonblocking recvfrom (UNCONNECTED) into caller buffer + peer info.
  /// @note RT-safe: Nonblocking syscall, no allocation.
  ssize_t readFrom(apex::compat::bytes_span bytes, PeerInfo& from,
                   std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Waits for socket to become readable using epoll.
   * @param timeoutMilliSecs Timeout in milliseconds (-1 = indefinite, 0 = poll).
   * @return true if readable, false on timeout.
   * @note NOT RT-safe (epoll_wait syscall).
   */
  bool waitReadable(int timeoutMilliSecs);

  /// Nonblocking send (CONNECTED). Returns bytes sent (>=0) or -1.
  /// @note RT-safe: Nonblocking syscall, no allocation.
  ssize_t write(apex::compat::bytes_span bytes, int /*timeoutMilliSecs*/,
                std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /// Nonblocking sendto (UNCONNECTED) to specific destination. Returns bytes sent (>=0) or -1.
  /// @note RT-safe: Nonblocking syscall, no allocation.
  ssize_t writeTo(apex::compat::bytes_span bytes, const PeerInfo& to, int /*timeoutMilliSecs*/,
                  std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  // --------------------------------------------------------------------------
  // Event loop
  // --------------------------------------------------------------------------

  /// Processes epoll events (readable + stop signal).
  /// @note NOT RT-safe: Blocks on epoll_wait syscall.
  void processEvents(int timeoutMilliSecs);

  /// Stop the event loop by waking epoll_wait(); safe from another thread.
  /// @note RT-safe: Writes to eventfd (single syscall, no allocation).
  void stop();

  /// Enables or disables lightweight logging.
  /// @note RT-safe: Sets a boolean flag.
  void enableLogging(bool enable);

  /**
   * @brief Registers a callback invoked when the socket is readable.
   * @param callback RT-safe delegate: void(void* ctx) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnReadable(apex::concurrency::Delegate<void> callback);

  /**
   * @brief Registers a callback invoked when the socket is writable (rarely used for UDP).
   * @param callback RT-safe delegate: void(void* ctx) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnWritable(apex::concurrency::Delegate<void> callback);

  // --------------------------------------------------------------------------
  // UDP policy (client-side)
  // --------------------------------------------------------------------------

  /// @note NOT RT-safe: setsockopt syscall.
  void setReusePort(bool on) { reusePort_ = on; }
  /// @note NOT RT-safe: setsockopt syscall.
  void setBroadcast(bool on) { broadcast_ = on; }
  /// @note NOT RT-safe: setsockopt syscall.
  void setTosDscp(int tos) { tosDscp_ = tos; } // -1 = leave default
  /// @note NOT RT-safe: setsockopt syscall.
  void setV6TrafficClass(int tclass) { v6TClass_ = tclass; } // -1 = leave default
  /// @note NOT RT-safe: setsockopt syscall.
  void bindToDevice(const std::string& ifname) { bindDevice_ = ifname; }

  // Multicast helpers (IPv4/IPv6)

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
  /// @note NOT RT-safe: setsockopt syscall.
  bool setMulticastIfaceV4(uint32_t ifaceBe);
  /// @note NOT RT-safe: setsockopt syscall.
  bool setMulticastIfaceV6(unsigned int ifindex);

  // Connected-peer convenience

  /// @note NOT RT-safe: System calls.
  bool connectPeer(const PeerInfo& to,
                   std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
  /// @note NOT RT-safe: System calls.
  bool disconnectPeer(std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
  /// @note NOT RT-safe: System calls.
  bool resolvePeer(const std::string& host, const std::string& port, PeerInfo& out,
                   std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  // Pktinfo toggles (ancillary I/O optional; kept as simple flags here)

  /// @note NOT RT-safe: setsockopt syscall.
  void setPktInfoV4(bool on) { pktinfoV4_ = on; }
  /// @note NOT RT-safe: setsockopt syscall.
  void setPktInfoV6(bool on) { pktinfoV6_ = on; }

  // --------------------------------------------------------------------------
  // Statistics
  // --------------------------------------------------------------------------

  /**
   * @brief Returns client statistics.
   * @note RT-safe: Returns a copy of the stats struct (no allocation).
   */
  DatagramStats stats() const noexcept { return stats_; }

  /**
   * @brief Resets all statistics to zero.
   * @note RT-safe: Simple assignment, no allocation.
   */
  void resetStats() noexcept { stats_ = DatagramStats{}; }

private:
  // RAII-managed descriptors
  UniqueFd sockfd_; /**< UDP socket. */
  UniqueFd epfd_;   /**< Epoll instance. */
  UniqueFd stopfd_; /**< Eventfd used to break epoll_wait() in stop(). */

  // Addressing
  const std::string addr_; /**< Remote (CONNECTED) or local (UNCONNECTED) address. */
  const std::string port_; /**< Remote (CONNECTED) or local (UNCONNECTED) port. */
  const std::string desc_; /**< Concatenated description for logging. */

  // Logging
  bool loggingEnabled_{false};

  // Mode + callbacks (RT-safe delegates, no heap allocation)
  UdpSocketMode mode_{UdpSocketMode::CONNECTED};
  apex::concurrency::Delegate<void> onReadable_;
  apex::concurrency::Delegate<void> onWritable_;
  std::mutex cbMutex_;

  // UDP policy (applied in init() where relevant; some can be used post-init)
  bool reusePort_{false};
  bool broadcast_{false};
  int tosDscp_{-1};
  int v6TClass_{-1};
  std::string bindDevice_;
  bool pktinfoV4_{false};
  bool pktinfoV6_{false};

  // Statistics
  DatagramStats stats_{};
};

} // namespace udp
} // namespace protocols
} // namespace apex

#include "src/system/core/infrastructure/protocols/network/udp/src/UdpSocketClient.tpp"

#endif // APEX_PROTOCOLS_UDP_SOCKET_CLIENT_HPP
