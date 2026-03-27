#ifndef APEX_PROTOCOLS_UNIX_SOCKET_CLIENT_HPP
#define APEX_PROTOCOLS_UNIX_SOCKET_CLIENT_HPP
/**
 * @file UnixSocketClient.hpp
 * @brief Optimized Unix domain socket client for real-time systems.
 *
 * Design:
 *  - Single epoll instance for client I/O.
 *  - Nonblocking I/O helpers (read/write) never wait; they return immediately (EAGAIN-safe).
 *  - Supports both STREAM and DATAGRAM modes.
 *
 * RT-safety:
 *  - init(): NOT RT-safe (syscalls, memory allocation)
 *  - processEvents(): NOT RT-safe (epoll_wait syscall)
 *  - read/write: RT-safe (nonblocking syscalls, no allocation)
 *  - waitReadable(): NOT RT-safe (epoll_wait syscall)
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixTypes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/helpers/inc/Fd.hpp"

#include <cstdint>
#include <string>
#include <sys/epoll.h>
#include <sys/un.h>

namespace apex {
namespace protocols {
namespace unix_socket {

using apex::helpers::fd::UniqueFd;

/* ----------------------------- Status ----------------------------- */

/**
 * @enum UnixClientStatus
 * @brief Unique status codes for Unix socket client operations.
 */
enum UnixClientStatus : uint8_t {
  UNIX_CLIENT_SUCCESS = 0,         /**< Operation succeeded. */
  UNIX_CLIENT_ERR_PATH_TOO_LONG,   /**< Socket path exceeds sun_path limit. */
  UNIX_CLIENT_ERR_SOCKET_CREATION, /**< Failed to create socket. */
  UNIX_CLIENT_ERR_CONNECT,         /**< Failed to connect to server. */
  UNIX_CLIENT_ERR_EPOLL_CREATION,  /**< Failed to create epoll instance. */
  UNIX_CLIENT_ERR_EPOLL_CONFIG,    /**< Failed to add socket to epoll. */
  UNIX_CLIENT_ERR_INTERNAL         /**< Generic internal error. */
};

/* ----------------------------- UnixSocketClient ----------------------------- */

/**
 * @class UnixSocketClient
 * @brief Single-epoll, nonblocking Unix domain socket client for RT/embedded workloads.
 *
 * Usage:
 *   1) Construct with server path, call init().
 *   2) Register callbacks.
 *   3) Call processEvents(timeoutMs) in a loop on the I/O thread.
 *   4) Call stop() to break the loop and shut down cleanly.
 */
class UnixSocketClient : public apex::protocols::ByteTrace {
public:
  /**
   * @brief Constructs a UnixSocketClient for the specified server path.
   * @param serverPath The filesystem path of the Unix socket server.
   * @param mode STREAM for connection-oriented, DATAGRAM for message-oriented.
   * @note NOT RT-safe: Allocates string storage.
   */
  UnixSocketClient(const std::string& serverPath, UnixSocketMode mode = UnixSocketMode::STREAM);

  /**
   * @brief Initializes the client: creates socket, connects, and sets up epoll.
   * @param error Optional output parameter for a descriptive error message.
   * @return uint8_t Status code from UnixClientStatus.
   * @note NOT RT-safe: System calls, memory allocation.
   */
  uint8_t init(std::string* error = nullptr);

  ~UnixSocketClient();

  // --------------------------------------------------------------------------
  // Nonblocking I/O
  // --------------------------------------------------------------------------

  /**
   * @brief Nonblocking read from the server.
   * @return bytes read (>0), 0 if no data available (would block), or -1 on error.
   * @note RT-safe: Nonblocking syscall, no allocation.
   */
  ssize_t read(apex::compat::bytes_span bytes, std::string* error = nullptr);

  /**
   * @brief Nonblocking write to the server.
   * @return bytes written (>=0), or -1 on error.
   * @note RT-safe: Nonblocking syscall, no allocation.
   */
  ssize_t write(apex::compat::bytes_span bytes, std::string* error = nullptr);

  /**
   * @brief Wait for socket to become readable (uses epoll_wait).
   * @param timeoutMilliSecs Timeout in milliseconds.
   * @return true if readable, false on timeout or error.
   * @note NOT RT-safe: Blocks on epoll_wait syscall.
   */
  bool waitReadable(int timeoutMilliSecs);

  // --------------------------------------------------------------------------
  // Event loop
  // --------------------------------------------------------------------------

  /**
   * @brief Processes events on the epoll instance.
   * @param timeoutMilliSecs Timeout in milliseconds for epoll_wait.
   * @note NOT RT-safe: Blocks on epoll_wait syscall.
   */
  void processEvents(int timeoutMilliSecs);

  /**
   * @brief Stop the event loop by waking epoll_wait(); safe to call from another thread.
   * @note RT-safe: Writes to eventfd (single syscall, no allocation).
   */
  void stop();

  // --------------------------------------------------------------------------
  // Callbacks
  // --------------------------------------------------------------------------

  /// @note RT-safe: No heap allocation.
  void setOnReadable(apex::concurrency::Delegate<void> callback);
  /// @note RT-safe: No heap allocation.
  void setOnWritable(apex::concurrency::Delegate<void> callback);
  /// @note RT-safe: No heap allocation.
  void setOnDisconnected(apex::concurrency::Delegate<void> callback);

  // --------------------------------------------------------------------------
  // Statistics
  // --------------------------------------------------------------------------

  /// @note RT-safe: Returns a copy, no allocation.
  ConnectionStats stats() const noexcept { return stats_; }
  /// @note RT-safe: Simple assignment, no allocation.
  void resetStats() noexcept { stats_ = ConnectionStats{}; }

  /// Returns the server path.
  /// @note RT-safe: Returns member, no allocation.
  const std::string& serverPath() const noexcept { return serverPath_; }

  /// Returns true if connected.
  /// @note RT-safe: Returns member, no allocation.
  bool isConnected() const noexcept { return static_cast<bool>(sockfd_); }

private:
  UniqueFd sockfd_; /**< Client socket. */
  UniqueFd epfd_;   /**< Epoll instance. */
  UniqueFd stopfd_; /**< Eventfd used to break epoll_wait() in stop(). */

  const std::string serverPath_; /**< Server socket path. */
  const UnixSocketMode mode_;    /**< STREAM or DATAGRAM. */

  ConnectionStats stats_{};

  // Callbacks (RT-safe delegates)
  apex::concurrency::Delegate<void> onReadable_;
  apex::concurrency::Delegate<void> onWritable_;
  apex::concurrency::Delegate<void> onDisconnected_;
};

} // namespace unix_socket
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_UNIX_SOCKET_CLIENT_HPP
