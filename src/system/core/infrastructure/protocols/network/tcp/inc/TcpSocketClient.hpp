#ifndef APEX_PROTOCOLS_TCP_SOCKET_CLIENT_HPP
#define APEX_PROTOCOLS_TCP_SOCKET_CLIENT_HPP
/**
 * @file TcpSocketClient.hpp
 * @brief Provides an optimized TCP client implementation for real-time systems,
 *        featuring non-blocking connection (with timeout), epoll-based read/write operations
 *        with timeouts, and integrated event-driven I/O.
 *
 * All functions return status codes or counts and, if an optional error is provided,
 * populate it with a descriptive error message.
 *
 * RT-safety:
 *  - init(): NOT RT-safe (syscalls, memory allocation)
 *  - processEvents(): NOT RT-safe (epoll_wait syscall)
 *  - read/write: RT-safe (nonblocking syscalls, no allocation)
 *  - Callbacks use Delegate (no heap allocation)
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpTypes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/helpers/inc/Fd.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <netdb.h>
#include <optional>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace apex {
namespace protocols {
namespace tcp {

using apex::helpers::fd::UniqueFd;

/* ----------------------------- Status ----------------------------- */

/**
 * @enum TcpClientStatus
 * @brief Unique status codes for TCP client operations.
 */
enum TcpClientStatus : uint8_t {
  TCP_CLIENT_SUCCESS = 0,            /**< Operation succeeded. */
  TCP_CLIENT_ERR_GAI_FAILURE,        /**< Address resolution failure (getaddrinfo). */
  TCP_CLIENT_ERR_SOCKET_CREATION,    /**< Failure creating a socket. */
  TCP_CLIENT_ERR_CONNECTION_FAILURE, /**< Failure to establish a connection. */
  TCP_CLIENT_ERR_SOCKET_FLAG,        /**< Failure configuring non-blocking mode on socket. */
  TCP_CLIENT_ERR_CONNECT_TIMEOUT,    /**< Connection attempt timeout expired. */
  TCP_CLIENT_ERR_EPOLL_CREATION,     /**< Failure creating an epoll instance. */
  TCP_CLIENT_ERR_EPOLL_CONFIG,       /**< Failure adding the socket to epoll. */
  TCP_CLIENT_ERR_RECV,               /**< Error during data reception. */
  TCP_CLIENT_ERR_WRITE               /**< Error during data transmission. */
};

/* ----------------------------- TcpSocketClient ----------------------------- */

/**
 * @class TcpSocketClient
 * @brief An optimized TCP client for real-time systems with integrated event-driven I/O.
 *
 * Encapsulates connection setup (with a non-blocking connect timeout),
 * non-blocking I/O via epoll with configurable timeouts and event processing,
 * and clear error reporting. In addition to the existing synchronous read() and write() methods,
 * this class now offers a unified event loop via processEvents() and allows registration of
 * custom callbacks for read/write events.
 */
class TcpSocketClient : public apex::protocols::ByteTrace {
public:
  /**
   * @brief Constructs a TcpSocketClient with the given server address and port.
   *
   * Actual connection setup occurs in init().
   *
   * @param addr The server address.
   * @param port The server port.
   * @note NOT RT-safe: Allocates string storage.
   */
  TcpSocketClient(const std::string& addr, const std::string& port);

  /**
   * @brief Initializes the TCP connection and epoll infrastructure.
   *
   * Resolves the server address, creates a non-blocking socket, and attempts a non-blocking connect
   * that must complete within connectTimeoutMilliSecs. On success, an epoll instance is created
   * for subsequent I/O.
   *
   * @param connectTimeoutMilliSecs Timeout (in milliseconds) for the connection attempt.
   * @param error Optional output parameter for an error message.
   * @return uint8_t Status code from TcpClientStatus.
   * @note NOT RT-safe: System calls, memory allocation.
   */
  uint8_t init(int connectTimeoutMilliSecs,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Destructor cleans up resources.
   */
  ~TcpSocketClient();

  /**
   * @brief Enables or disables lightweight logging.
   *
   * In a real-time system, logging overhead must be minimal. This merely toggles
   * a flag; you can integrate asynchronous or ring-buffer logging as needed.
   *
   * @param enable True to enable logging.
   * @note RT-safe: Sets a boolean flag.
   */
  void enableLogging(bool enable);

  /**
   * @brief Reads bytes from the TCP socket using epoll with a timeout.
   *
   * Data is read into the provided array. If no data becomes available within timeoutMilliSecs,
   * a timeout message is output (if an error parameter is provided).
   *
   * @tparam S The size of the destination byte array.
   * @param bytes Array to store received data.
   * @param timeoutMilliSecs Timeout in milliseconds (0 for non-blocking, -1 for indefinite
   * blocking).
   * @param error Optional output parameter for an error message.
   * @return ssize_t Number of bytes read, 0 on timeout, or -1 on error.
   * @note NOT RT-safe: Blocks on epoll_wait with timeout.
   */
  template <size_t S>
  ssize_t read(std::array<uint8_t, S>& bytes, int timeoutMilliSecs,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Reads bytes from the TCP socket into a mutable span using epoll with a timeout.
   *
   * @param bytes Mutable span buffer to store received data.
   * @param timeoutMilliSecs Timeout in milliseconds (0 for non-blocking, -1 for indefinite
   * blocking).
   * @param error Optional output parameter for an error message.
   * @return ssize_t Number of bytes read, 0 on timeout, or -1 on error.
   * @note NOT RT-safe: Blocks on epoll_wait with timeout.
   */
  ssize_t read(apex::compat::mutable_bytes_span bytes, int timeoutMilliSecs,
               std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Writes bytes to the TCP socket.
   *
   * Sends the complete payload from the provided span. If the socket is temporarily not ready,
   * epoll_wait is used to wait (taking the overall timeout into account) until write readiness.
   *
   * @param bytes Span containing the data to write.
   * @param timeoutMilliSecs Overall timeout in milliseconds for the write operation.
   * @param error Optional output parameter for an error message.
   * @return ssize_t Total number of bytes written, or -1 on error or if timed out.
   * @note NOT RT-safe: Blocks on epoll_wait with timeout.
   */
  ssize_t write(apex::compat::span<const uint8_t> bytes, int timeoutMilliSecs,
                std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

  /**
   * @brief Processes events on the client's epoll instance.
   *
   * This unified event loop handles both EPOLLIN and EPOLLOUT events and dispatches
   * them to the registered callbacks. It blocks for up to timeoutMilliSecs waiting for events.
   *
   * @param timeoutMilliSecs Timeout in milliseconds for epoll_wait.
   * @note NOT RT-safe: Blocks on epoll_wait syscall.
   */
  void processEvents(int timeoutMilliSecs);

  /**
   * @brief Registers a callback invoked when the socket is readable.
   * @param callback RT-safe delegate: void(void* ctx) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnReadable(apex::concurrency::Delegate<void> callback);

  /**
   * @brief Registers a callback invoked when the socket is write-ready.
   * @param callback RT-safe delegate: void(void* ctx) noexcept.
   * @note RT-safe: No heap allocation.
   */
  void setOnWritable(apex::concurrency::Delegate<void> callback);

  // --------------------------------------------------------------------------
  // Statistics
  // --------------------------------------------------------------------------

  /**
   * @brief Returns connection statistics.
   * @note RT-safe: Returns a copy of the stats struct (no allocation).
   */
  ConnectionStats stats() const noexcept { return stats_; }

  /**
   * @brief Resets all statistics to zero.
   * @note RT-safe: Simple assignment, no allocation.
   */
  void resetStats() noexcept { stats_ = ConnectionStats{}; }

  /**
   * @brief Sets SO_LINGER option for graceful shutdown.
   * @param enable If true, enables SO_LINGER with the specified timeout.
   * @param timeoutSecs Linger timeout in seconds (0 = RST on close, >0 = wait for pending data).
   * @return true on success, false on failure.
   * @note NOT RT-safe: setsockopt syscall. Must be called after init().
   */
  bool setLinger(bool enable, int timeoutSecs = 0);

private:
  UniqueFd sockfd_;            /**< RAII-managed socket file descriptor. */
  UniqueFd epfd_;              /**< RAII-managed epoll file descriptor. */
  const std::string addr_;     /**< Server address. */
  const std::string port_;     /**< Server port. */
  const std::string desc_;     /**< Concatenated description (addr:port) for logging purposes. */
  bool loggingEnabled_{false}; /**< Logging flag. */
  std::string rxLogFile_;      /**< Log file name for received data. */
  std::string txLogFile_;      /**< Log file name for transmitted data. */

  // Callbacks for asynchronous event notifications (RT-safe delegates, no heap allocation).
  apex::concurrency::Delegate<void> onReadable_;
  apex::concurrency::Delegate<void> onWritable_;

  // Statistics
  ConnectionStats stats_{};

  // Optional: a write buffer for pending data in asynchronous writes.
  std::vector<uint8_t> writeBuffer_;
};

} // namespace tcp
} // namespace protocols
} // namespace apex

#include "src/system/core/infrastructure/protocols/network/tcp/src/TcpSocketClient.tpp"

#endif // APEX_PROTOCOLS_TCP_SOCKET_CLIENT_HPP
