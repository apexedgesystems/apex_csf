#ifndef APEX_PROTOCOLS_TCP_SOCKET_SERVER_HPP
#define APEX_PROTOCOLS_TCP_SOCKET_SERVER_HPP
/**
 * @file TcpSocketServer.hpp
 * @brief Optimized TCP server for real-time systems with a single epoll-driven event loop.
 *
 * Design:
 *  - One epoll instance owns all waiting (accept + client I/O) via processEvents().
 *  - Nonblocking I/O helpers (read/write) never wait; they return immediately (EAGAIN-safe).
 *  - Per-connection flow control/backpressure is handled internally in the .cpp.
 *  - Public API uses apex::compat::bytes_span for C++17+ portability; array overload remains for
 *    zero-alloc reads.
 *
 * Callback execution context:
 *  - onNewConnection / onClientReadable / onClientWritable run on the event-loop thread.
 *  - Keep callbacks fast; offload heavy work to a worker pool if needed.
 *
 * RT-safety:
 *  - init(): NOT RT-safe (syscalls, memory allocation)
 *  - processEvents(): NOT RT-safe (epoll_wait syscall)
 *  - read/write: RT-safe (nonblocking syscalls, no allocation)
 *  - Callbacks use Delegate (no heap allocation)
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpTypes.hpp"
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
#include <unordered_set>

namespace apex {
namespace protocols {
namespace tcp {

using apex::helpers::fd::UniqueFd;

/* ----------------------------- Status ----------------------------- */

/**
 * @enum TcpServerStatus
 * @brief Unique status codes for TCP server operations.
 */
enum TcpServerStatus : uint8_t {
  TCP_SERVER_SUCCESS = 0,         /**< Operation succeeded. */
  TCP_SERVER_ERR_GAI_FAILURE,     /**< Address resolution failure. */
  TCP_SERVER_ERR_SOCKET_CREATION, /**< Failed to create a server socket. */
  TCP_SERVER_ERR_BIND,            /**< Failed to bind the server socket. */
  TCP_SERVER_ERR_LISTEN,          /**< Failed to listen on the server socket. */
  TCP_SERVER_ERR_EPOLL_CREATION,  /**< Failed to create the epoll instance. */
  TCP_SERVER_ERR_EPOLL_CONFIG,    /**< Failed to add/modify epoll interest. */
  TCP_SERVER_ERR_ACCEPT,          /**< Accepting a client connection failed. */
  TCP_SERVER_ERR_CLIENT_EPOLL,    /**< Failed to configure client socket in epoll. */
  TCP_SERVER_ERR_INTERNAL         /**< Generic internal error. */
};

/* ----------------------------- TcpSocketServer ----------------------------- */

/**
 * @class TcpSocketServer
 * @brief Single-epoll, nonblocking TCP server for RT/embedded workloads.
 *
 * Usage:
 *   1) Construct with bind address/port, call init().
 *   2) Optionally set defaults (setNodelayDefault, setBufferSizes, setKeepalive, etc.).
 *   3) Register callbacks.
 *   4) Call processEvents(timeoutMs) in a loop on the I/O thread.
 *   5) Call stop() to break the loop and shut down cleanly.
 */
class TcpSocketServer : public apex::protocols::ByteTrace {
public:
  /**
   * @brief Constructs a TcpSocketServer with the specified bind address and port.
   * @param addr The bind address (empty string for INADDR_ANY).
   * @param port The port number to bind.
   * @note NOT RT-safe: Allocates string storage.
   */
  TcpSocketServer(const std::string& addr, const std::string& port);

  /**
   * @brief Initializes the server: creates, binds, listens, and sets up the epoll instance.
   * @param error Optional output parameter for a descriptive error message.
   * @return uint8_t Status code from TcpServerStatus.
   * @note NOT RT-safe: System calls, memory allocation.
   */
  uint8_t init(std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  // --------------------------------------------------------------------------
  // Nonblocking I/O helpers (no internal waits)
  // --------------------------------------------------------------------------

  /**
   * @brief Nonblocking read into a fixed-size array. Drains up to S bytes.
   * @note timeoutMilliSecs is ignored in nonblocking mode (kept for source compatibility).
   * @return bytes read (>0), 0 if no data available (would block), or -1 on error.
   * @note RT-safe: Nonblocking recv syscall, no allocation.
   */
  template <size_t S>
  ssize_t read(int clientfd, std::array<uint8_t, S>& bytes, int /*timeoutMilliSecs*/,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Nonblocking read into a caller-provided buffer.
   * @return bytes read (>0), 0 if no data available (would block), or -1 on error.
   * @note RT-safe: Nonblocking recv syscall, no allocation.
   */
  ssize_t read(int clientfd, apex::compat::bytes_span bytes,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Nonblocking write from a caller-provided buffer.
   * Writes as much as the kernel will accept now; any remainder is queued internally
   * and flushed when EPOLLOUT is signaled by the event loop.
   * @return bytes written immediately (>=0), or -1 on error.
   * @note RT-safe: Nonblocking send syscall, no allocation.
   */
  ssize_t write(int clientfd, apex::compat::bytes_span bytes,
                std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Enqueue a buffer to all connected clients (nonblocking).
   * Each connection will flush on its next EPOLLOUT opportunity.
   * @note NOT RT-safe: Iterates client set under lock.
   */
  void writeAll(apex::compat::bytes_span bytes);

  /**
   * @brief Closes the connection to a client.
   * Removes the client from the internal set, removes from epoll, and closes its socket.
   * @note NOT RT-safe: Modifies client set under lock, close syscall.
   */
  void closeConnection(int clientfd);

  /**
   * @brief Enables or disables lightweight logging.
   * @note RT-safe: Sets a boolean flag.
   */
  void enableLogging(bool enable);

  /**
   * @brief Processes events on the epoll instance (accept + client I/O + stop signal).
   * This is the *only* function that calls epoll_wait().
   * @param timeoutMilliSecs Timeout in milliseconds for epoll_wait.
   * @note NOT RT-safe: Blocks on epoll_wait syscall.
   */
  void processEvents(int timeoutMilliSecs);

  /**
   * @brief Registers a callback invoked after a new client has been accepted.
   * @param callback RT-safe delegate: void(void* ctx, int clientfd) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnNewConnection(apex::concurrency::Delegate<void, int> callback);

  /**
   * @brief Registers a callback invoked when a client fd becomes readable.
   * @param callback RT-safe delegate: void(void* ctx, int clientfd) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnClientReadable(apex::concurrency::Delegate<void, int> callback);

  /**
   * @brief Registers a callback invoked when a client fd becomes writable.
   * @param callback RT-safe delegate: void(void* ctx, int clientfd) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnClientWritable(apex::concurrency::Delegate<void, int> callback);

  /**
   * @brief Registers a callback invoked when a client connection is closed.
   * @param callback RT-safe delegate: void(void* ctx, int clientfd) noexcept.
   * @note RT-safe: No heap allocation.
   * @note Called before the fd is removed from the client set and closed.
   */
  void setOnConnectionClosed(apex::concurrency::Delegate<void, int> callback);

  /**
   * @brief Stop the event loop by waking epoll_wait(); safe to call from another thread.
   * @note RT-safe: Writes to eventfd (single syscall, no allocation).
   */
  void stop();

  ~TcpSocketServer();

  /**
   * @brief Retrieves the set of currently connected client file descriptors.
   * @note NOT RT-safe: Caller must lock getMutex() before iterating.
   */
  const std::unordered_set<int>& getClientFds() const;

  /**
   * @brief Provides access to the mutex for thread-safe operations on client set.
   * @note RT-safe: Returns reference, no allocation.
   */
  std::mutex& getMutex();

  // --------------------------------------------------------------------------
  // Statistics
  // --------------------------------------------------------------------------

  /**
   * @brief Returns aggregate server statistics (all connections combined).
   * @note RT-safe: Returns a copy of the stats struct (no allocation).
   */
  ConnectionStats stats() const noexcept { return stats_; }

  /**
   * @brief Resets all server statistics to zero.
   * @note RT-safe: Simple assignment, no allocation.
   */
  void resetStats() noexcept { stats_ = ConnectionStats{}; }

  /**
   * @brief Returns the number of currently connected clients.
   * @note NOT RT-safe: Acquires mutex.
   */
  size_t connectionCount() const;

  // --------------------------------------------------------------------------
  // Server default socket policy (applied to each accepted connection)
  // --------------------------------------------------------------------------

  /// Set default TCP_NODELAY for accepted connections (mutually exclusive with CORK).
  /// @note RT-safe: Sets member variables.
  void setNodelayDefault(bool on) {
    defaultNodelay_ = on;
    if (on)
      defaultCork_ = false;
  }

  /// Set default TCP_CORK for accepted connections (mutually exclusive with NODELAY).
  /// @note RT-safe: Sets member variables.
  void setCorkDefault(bool on) {
    defaultCork_ = on;
    if (on)
      defaultNodelay_ = false;
  }

  /// Set default SO_RCVBUF/SO_SNDBUF sizes (bytes) for accepted connections (0 = leave system
  /// default).
  /// @note RT-safe: Sets member variables.
  void setBufferSizes(int rcvBytes, int sndBytes) {
    defaultRcvbuf_ = rcvBytes;
    defaultSndbuf_ = sndBytes;
  }

  /// Set default TCP keepalive parameters applied to accepted connections.
  /// @note RT-safe: Sets member variables.
  void setKeepalive(bool on, int idleSec = 0, int intvlSec = 0, int cnt = 0) {
    kaOn_ = on;
    kaIdle_ = idleSec;
    kaIntvl_ = intvlSec;
    kaCnt_ = cnt;
  }

  /// Set default TCP_QUICKACK (Linux) preference applied to accepted connections.
  /// @note RT-safe: Sets member variable.
  void setQuickAckDefault(bool on) { defaultQuickAck_ = on; }

  /// Set default SO_BUSY_POLL (Linux) in microseconds for accepted connections (0 = disabled).
  /// @note RT-safe: Sets member variable.
  void setBusyPollDefault(int usec) { defaultBusyPollUsec_ = usec; }

  /// Configure backpressure thresholds (bytes) for per-connection TX queue (0 = internal defaults).
  /// @note RT-safe: Sets member variables.
  void setBackpressureThresholds(size_t highWatermarkBytes, size_t lowWatermarkBytes) {
    txHighWatermark_ = highWatermarkBytes;
    txLowWatermark_ = lowWatermarkBytes;
  }

  /**
   * @brief Sets the maximum number of simultaneous client connections (0 = unlimited).
   * @param maxConns Maximum connections allowed; new connections are rejected when limit reached.
   */
  void setMaxConnections(size_t maxConns) noexcept { maxConnections_ = maxConns; }

  /**
   * @brief Returns the current maximum connections limit (0 = unlimited).
   */
  size_t maxConnections() const noexcept { return maxConnections_; }

  /**
   * @brief Sets SO_LINGER option for graceful shutdown on accepted connections.
   * @param enable If true, enables SO_LINGER with the specified timeout.
   * @param timeoutSecs Linger timeout in seconds (0 = RST on close, >0 = wait for pending data).
   * @note Applied to newly accepted connections; does not affect existing connections.
   */
  void setLingerDefault(bool enable, int timeoutSecs = 0) noexcept {
    lingerOn_ = enable;
    lingerSecs_ = timeoutSecs;
  }

private:
  // RAII-managed descriptors
  UniqueFd serverfd_; /**< Listening socket. */
  UniqueFd epfd_;     /**< Epoll instance. */
  UniqueFd stopfd_;   /**< Eventfd/timerfd used to break epoll_wait() in stop(). */

  // Bind details
  const std::string addr_; /**< Bind address. */
  const std::string port_; /**< Port number. */
  const std::string desc_; /**< Concatenated description for logging. */

  // External client view (per-connection internals are defined in .cpp)
  std::unordered_set<int> clientsSockfds_;
  mutable std::mutex clientsMutex_;

  // Logging
  bool loggingEnabled_{false};
  std::string rxLogFile_;
  std::string txLogFile_;

  // Statistics (aggregate across all connections)
  ConnectionStats stats_{};

  // Default socket policy applied on accept()
  bool defaultNodelay_{true};
  bool defaultCork_{false};
  bool defaultQuickAck_{false};
  bool kaOn_{false};
  int kaIdle_{0};
  int kaIntvl_{0};
  int kaCnt_{0};
  int defaultRcvbuf_{0};       // 0 = leave system default
  int defaultSndbuf_{0};       // 0 = leave system default
  int defaultBusyPollUsec_{0}; // 0 = disabled

  // Backpressure thresholds for TX queue per connection (internal defaults chosen in .cpp if zero)
  size_t txHighWatermark_{0};
  size_t txLowWatermark_{0};

  // Connection limit (0 = unlimited)
  size_t maxConnections_{0};

  // Graceful shutdown (SO_LINGER)
  bool lingerOn_{false};
  int lingerSecs_{0};

  /**
   * @brief Legacy helper retained for minimal churn; will be replaced by compat net helpers in
   * .cpp.
   * @return true on success, false on failure.
   */
  bool setNonBlocking(int fd,
                      std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  // Callback storage (RT-safe delegates, no heap allocation).
  apex::concurrency::Delegate<void, int> onNewConnection_;
  apex::concurrency::Delegate<void, int> onClientReadable_;
  apex::concurrency::Delegate<void, int> onClientWritable_;
  apex::concurrency::Delegate<void, int> onConnectionClosed_;
};

} // namespace tcp
} // namespace protocols
} // namespace apex

#include "src/system/core/infrastructure/protocols/network/tcp/src/TcpSocketServer.tpp"

#endif // APEX_PROTOCOLS_TCP_SOCKET_SERVER_HPP
