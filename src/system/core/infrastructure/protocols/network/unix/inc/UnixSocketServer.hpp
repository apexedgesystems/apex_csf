#ifndef APEX_PROTOCOLS_UNIX_SOCKET_SERVER_HPP
#define APEX_PROTOCOLS_UNIX_SOCKET_SERVER_HPP
/**
 * @file UnixSocketServer.hpp
 * @brief Optimized Unix domain socket server for real-time systems.
 *
 * Design:
 *  - One epoll instance owns all waiting (accept + client I/O) via processEvents().
 *  - Nonblocking I/O helpers (read/write) never wait; they return immediately (EAGAIN-safe).
 *  - Supports both STREAM (connection-oriented) and DATAGRAM modes.
 *
 * Callback execution context:
 *  - onNewConnection / onClientReadable / onClientWritable / onConnectionClosed
 *    run on the event-loop thread.
 *  - Keep callbacks fast; offload heavy work to a worker pool if needed.
 *
 * RT-safety:
 *  - init(): NOT RT-safe (syscalls, memory allocation)
 *  - processEvents(): NOT RT-safe (epoll_wait syscall)
 *  - read/write: RT-safe (nonblocking syscalls, no allocation)
 *  - Callbacks use Delegate (no heap allocation)
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixTypes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/helpers/inc/Fd.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unordered_set>

namespace apex {
namespace protocols {
namespace unix_socket {

using apex::helpers::fd::UniqueFd;

/* ----------------------------- Status ----------------------------- */

/**
 * @enum UnixServerStatus
 * @brief Unique status codes for Unix socket server operations.
 */
enum UnixServerStatus : uint8_t {
  UNIX_SERVER_SUCCESS = 0,         /**< Operation succeeded. */
  UNIX_SERVER_ERR_PATH_TOO_LONG,   /**< Socket path exceeds sun_path limit. */
  UNIX_SERVER_ERR_SOCKET_CREATION, /**< Failed to create a server socket. */
  UNIX_SERVER_ERR_BIND,            /**< Failed to bind the server socket. */
  UNIX_SERVER_ERR_LISTEN,          /**< Failed to listen on the server socket (STREAM mode). */
  UNIX_SERVER_ERR_EPOLL_CREATION,  /**< Failed to create the epoll instance. */
  UNIX_SERVER_ERR_EPOLL_CONFIG,    /**< Failed to add/modify epoll interest. */
  UNIX_SERVER_ERR_UNLINK,          /**< Failed to unlink existing socket file. */
  UNIX_SERVER_ERR_INTERNAL         /**< Generic internal error. */
};

/* ----------------------------- UnixSocketServer ----------------------------- */

/**
 * @class UnixSocketServer
 * @brief Single-epoll, nonblocking Unix domain socket server for RT/embedded workloads.
 *
 * Usage:
 *   1) Construct with socket path, call init().
 *   2) Register callbacks.
 *   3) Call processEvents(timeoutMs) in a loop on the I/O thread.
 *   4) Call stop() to break the loop and shut down cleanly.
 */
class UnixSocketServer : public apex::protocols::ByteTrace {
public:
  /**
   * @brief Constructs a UnixSocketServer with the specified socket path.
   * @param path The filesystem path for the Unix socket (e.g., "/tmp/my.sock").
   * @param mode STREAM for connection-oriented, DATAGRAM for message-oriented.
   * @note NOT RT-safe: Allocates string storage.
   */
  UnixSocketServer(const std::string& path, UnixSocketMode mode = UnixSocketMode::STREAM);

  /**
   * @brief Initializes the server: creates, binds, listens (if STREAM), and sets up epoll.
   * @param unlinkExisting If true, unlink any existing socket file before binding.
   * @param error Optional output parameter for a descriptive error message.
   * @return uint8_t Status code from UnixServerStatus.
   * @note NOT RT-safe: System calls, memory allocation.
   */
  uint8_t init(bool unlinkExisting = true, std::string* error = nullptr);

  ~UnixSocketServer();

  // --------------------------------------------------------------------------
  // Nonblocking I/O helpers (STREAM mode - per-connection)
  // --------------------------------------------------------------------------

  /**
   * @brief Nonblocking read from a client connection (STREAM mode).
   * @return bytes read (>0), 0 if no data available (would block), or -1 on error.
   * @note RT-safe: Nonblocking syscall, no allocation.
   */
  ssize_t read(int clientfd, apex::compat::bytes_span bytes, std::string* error = nullptr);

  /**
   * @brief Nonblocking write to a client connection (STREAM mode).
   * @return bytes written (>=0), or -1 on error.
   * @note RT-safe: Nonblocking syscall, no allocation.
   */
  ssize_t write(int clientfd, apex::compat::bytes_span bytes, std::string* error = nullptr);

  /**
   * @brief Closes a client connection (STREAM mode).
   * @note NOT RT-safe: Modifies client set under lock, close syscall.
   */
  void closeConnection(int clientfd);

  // --------------------------------------------------------------------------
  // Event loop
  // --------------------------------------------------------------------------

  /**
   * @brief Processes events on the epoll instance (accept + client I/O + stop signal).
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
  void setOnNewConnection(apex::concurrency::Delegate<void, int> callback);
  /// @note RT-safe: No heap allocation.
  void setOnClientReadable(apex::concurrency::Delegate<void, int> callback);
  /// @note RT-safe: No heap allocation.
  void setOnClientWritable(apex::concurrency::Delegate<void, int> callback);
  /// @note RT-safe: No heap allocation.
  void setOnConnectionClosed(apex::concurrency::Delegate<void, int> callback);

  // --------------------------------------------------------------------------
  // Statistics
  // --------------------------------------------------------------------------

  /// @note RT-safe: Returns a copy, no allocation.
  ConnectionStats stats() const noexcept { return stats_; }
  /// @note RT-safe: Simple assignment, no allocation.
  void resetStats() noexcept { stats_ = ConnectionStats{}; }
  /// @note NOT RT-safe: Acquires mutex.
  size_t connectionCount() const;

  // --------------------------------------------------------------------------
  // Configuration
  // --------------------------------------------------------------------------

  /// @note RT-safe: Sets/returns member variable.
  void setMaxConnections(size_t maxConns) noexcept { maxConnections_ = maxConns; }
  /// @note RT-safe: Sets/returns member variable.
  size_t maxConnections() const noexcept { return maxConnections_; }

  /// Enable or disable lightweight logging.
  /// @note RT-safe: Sets a boolean flag.
  void enableLogging(bool enable) { loggingEnabled_ = enable; }

  /**
   * @brief Returns the set of currently connected client file descriptors (STREAM mode).
   * @note External callers should lock getMutex() before iterating.
   * @note NOT RT-safe: Caller must lock getMutex() before iterating.
   */
  const std::unordered_set<int>& getClientFds() const { return clientsSockfds_; }
  /// @note RT-safe: Returns reference, no allocation.
  std::mutex& getMutex() { return clientsMutex_; }

  /// Returns the socket path.
  /// @note RT-safe: Returns member, no allocation.
  const std::string& path() const noexcept { return path_; }

private:
  UniqueFd serverfd_; /**< Listening/bound socket. */
  UniqueFd epfd_;     /**< Epoll instance. */
  UniqueFd stopfd_;   /**< Eventfd used to break epoll_wait() in stop(). */

  const std::string path_;    /**< Socket path. */
  const UnixSocketMode mode_; /**< STREAM or DATAGRAM. */

  std::unordered_set<int> clientsSockfds_;
  mutable std::mutex clientsMutex_;

  bool loggingEnabled_{false};
  ConnectionStats stats_{};
  size_t maxConnections_{0};

  // Callbacks (RT-safe delegates)
  apex::concurrency::Delegate<void, int> onNewConnection_;
  apex::concurrency::Delegate<void, int> onClientReadable_;
  apex::concurrency::Delegate<void, int> onClientWritable_;
  apex::concurrency::Delegate<void, int> onConnectionClosed_;
};

} // namespace unix_socket
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_UNIX_SOCKET_SERVER_HPP
