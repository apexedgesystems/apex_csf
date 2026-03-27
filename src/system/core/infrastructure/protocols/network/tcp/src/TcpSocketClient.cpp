/**
 * @file TcpSocketClient.cpp
 * @brief TCP socket client implementation.
 */

#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"

#include "src/utilities/helpers/inc/Net.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono;

/* ----------------------------- File Helpers ----------------------------- */

namespace {
/// Helper to get current time in nanoseconds since epoch (for stats timestamps).
inline int64_t nowNs() noexcept {
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

/* -------------------------- TcpSocketClient Methods ------------------------- */

namespace apex {
namespace protocols {
namespace tcp {

using apex::protocols::TraceDirection;

TcpSocketClient::TcpSocketClient(const std::string& addr, const std::string& port)
    : addr_(addr), port_(port), desc_(addr + ":" + port) {
  rxLogFile_ = "tcp_client_" + desc_ + "_rx.log";
  txLogFile_ = "tcp_client_" + desc_ + "_tx.log";
}

uint8_t TcpSocketClient::init(int connectTimeoutMilliSecs,
                              std::optional<std::reference_wrapper<std::string>> error) {
  // Resolve server address (v4/v6)
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* addresses = nullptr;
  int retVal = ::getaddrinfo(addr_.c_str(), port_.c_str(), &hints, &addresses);
  if (retVal != 0) {
    if (error)
      error->get() =
          "Failed to resolve address " + addr_ + ":" + port_ + ": " + ::gai_strerror(retVal);
    return TCP_CLIENT_ERR_GAI_FAILURE;
  }

  bool connected = false;
  for (struct addrinfo* info = addresses; info != nullptr; info = info->ai_next) {
    int sock = apex::helpers::net::socketCloexecNonblock(info->ai_family, info->ai_socktype,
                                                         info->ai_protocol);
    if (sock < 0)
      continue;

    int rc = ::connect(sock, info->ai_addr, info->ai_addrlen);
    if (rc == 0) {
      // Immediate connect success.
      sockfd_.reset(sock);
      connected = true;
      break;
    }

    if (rc < 0 && errno == EINPROGRESS) {
      // Wait for connect completion using a temporary epoll instance.
      UniqueFd tempEpfd(::epoll_create1(EPOLL_CLOEXEC));
      if (!tempEpfd) {
        ::close(sock);
        continue;
      }
      if (apex::helpers::net::epollAdd(tempEpfd.get(), sock, EPOLLOUT | EPOLLERR) != 0) {
        ::close(sock);
        continue;
      }

      epoll_event ev{};
      int epRet = ::epoll_wait(tempEpfd.get(), &ev, 1, connectTimeoutMilliSecs);
      if (epRet <= 0) {
        // Timeout or error
        if (epRet == 0 && error) {
          error->get() = "connect timeout expired.";
        }
        ::close(sock);
        continue;
      }

      // Check for connect status via SO_ERROR.
      int soError = 0;
      socklen_t len = sizeof(soError);
      if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &len) != 0 || soError != 0) {
        if (error) {
          error->get() = std::string("connect failed: ") +
                         (soError ? std::strerror(soError) : std::strerror(errno));
        }
        ::close(sock);
        continue;
      }

      sockfd_.reset(sock);
      connected = true;
      break;
    }

    // Immediate failure (not EINPROGRESS)
    ::close(sock);
  }
  ::freeaddrinfo(addresses);

  if (!sockfd_) {
    if (error)
      error->get() = "Failed to establish a connection with " + addr_ + ":" + port_;
    return TCP_CLIENT_ERR_CONNECTION_FAILURE;
  }

  // Create epoll instance for I/O and add the socket (level-triggered is fine here).
  epfd_.reset(::epoll_create1(EPOLL_CLOEXEC));
  if (!epfd_) {
    if (error)
      error->get() = std::string("Failed to create epoll instance: ") + std::strerror(errno);
    sockfd_.reset();
    return TCP_CLIENT_ERR_EPOLL_CREATION;
  }

  if (apex::helpers::net::epollAdd(epfd_.get(), sockfd_.get(), EPOLLIN) != 0) {
    if (error)
      error->get() = std::string("Failed to add socket to epoll: ") + std::strerror(errno);
    epfd_.reset();
    sockfd_.reset();
    return TCP_CLIENT_ERR_EPOLL_CONFIG;
  }

  (void)connected; // keeps the variable around for clarity; logic already ensured sockfd_ set.

  // Record connection time for stats
  stats_.connectedAtNs = nowNs();

  return TCP_CLIENT_SUCCESS;
}

TcpSocketClient::~TcpSocketClient() {
  // UniqueFd destructors automatically close the file descriptors.
}

void TcpSocketClient::enableLogging(bool enable) { loggingEnabled_ = enable; }

ssize_t TcpSocketClient::write(apex::compat::span<const uint8_t> bytes, int timeoutMilliSecs,
                               std::optional<std::reference_wrapper<std::string>> error) {
  size_t totalSent = 0;
  auto startTime = steady_clock::now();

  while (totalSent < bytes.size()) {
    ssize_t nsent = ::send(sockfd_.get(), bytes.data() + totalSent, bytes.size() - totalSent,
#ifdef MSG_NOSIGNAL
                           MSG_NOSIGNAL
#else
                           0
#endif
    );
    if (nsent > 0) {
      totalSent += static_cast<size_t>(nsent);
      continue;
    }
    if (nsent < 0) {
      if (!apex::helpers::net::isWouldBlock(errno)) {
        ++stats_.errorsTx;
        if (error)
          error->get() = std::string("send failed: ") + std::strerror(errno);
        return -1;
      }
    }

    // Wait for writability within the remaining timeout
    const auto ELAPSED = duration_cast<milliseconds>(steady_clock::now() - startTime).count();
    int remaining = timeoutMilliSecs - static_cast<int>(ELAPSED);
    if (remaining <= 0) {
      ++stats_.errorsTx;
      if (error)
        error->get() = "write timeout: socket not ready for writing.";
      return -1;
    }

    epoll_event ev{};
    ev.events = EPOLLOUT;
    ev.data.fd = sockfd_.get();
    // Temporarily wait on writability using the same epoll fd.
    int retval = ::epoll_wait(epfd_.get(), &ev, 1, remaining);
    if (retval == 0) {
      ++stats_.errorsTx;
      if (error)
        error->get() = "write timeout: socket not ready for writing.";
      return -1;
    } else if (retval < 0) {
      ++stats_.errorsTx;
      if (error)
        error->get() = std::string("epoll_wait failed during write: ") + std::strerror(errno);
      return -1;
    }
    // Loop and try send again.
  }

  // Update stats on success
  if (totalSent > 0) {
    stats_.bytesTx += totalSent;
    ++stats_.packetsTx;
    stats_.lastActivityNs = nowNs();
    invokeTrace(TraceDirection::TX, bytes.data(), totalSent);
  }

  return static_cast<ssize_t>(totalSent);
}

ssize_t TcpSocketClient::read(apex::compat::mutable_bytes_span bytes, int timeoutMilliSecs,
                              std::optional<std::reference_wrapper<std::string>> error) {
  epoll_event event;
  int retval = epoll_wait(epfd_.get(), &event, 1, timeoutMilliSecs);
  if (retval == 0) {
    if (error)
      error->get() = "Read timeout: no data available within the specified timeout.";
    return 0;
  }
  if (retval < 0) {
    ++stats_.errorsRx;
    if (error)
      error->get() = std::string("epoll_wait failed: ") + std::strerror(errno);
    return -1;
  }

  // Handle peer shutdown (read-half or full close).
  if (event.events & (EPOLLERR | EPOLLHUP)) {
    // Let caller detect closure via recv==0; we don't close here.
  }

  ssize_t nread = ::recv(sockfd_.get(), bytes.data(), bytes.size(), 0);
  if (nread < 0) {
    ++stats_.errorsRx;
    if (error)
      error->get() = std::string("recv failed: ") + std::strerror(errno);
    return -1;
  }

  // Update stats on success
  if (nread > 0) {
    stats_.bytesRx += static_cast<size_t>(nread);
    ++stats_.packetsRx;
    stats_.lastActivityNs = nowNs();
    invokeTrace(TraceDirection::RX, bytes.data(), static_cast<std::size_t>(nread));
  }

  // nread == 0 means orderly shutdown by peer.
  return nread;
}

void TcpSocketClient::processEvents(int timeoutMilliSecs) {
  constexpr size_t MAX_EVENTS = 10;
  std::array<epoll_event, MAX_EVENTS> events{};
  int nfds = ::epoll_wait(epfd_.get(), events.data(), MAX_EVENTS, timeoutMilliSecs);
  for (int i = 0; i < nfds; ++i) {
    const uint32_t EV = events[i].events;

    if (EV & EPOLLIN) {
      if (onReadable_)
        onReadable_();
    }
    if (EV & EPOLLOUT) {
      if (onWritable_)
        onWritable_();
      // Typically drop EPOLLOUT interest after notifying; callers can re-arm if needed.
    }
    if (EV & (EPOLLERR | EPOLLHUP)) {
      // Optional: surface via a user error callback or let read() return 0/-1.
    }
  }
}

void TcpSocketClient::setOnReadable(apex::concurrency::Delegate<void> callback) {
  onReadable_ = callback;
}

void TcpSocketClient::setOnWritable(apex::concurrency::Delegate<void> callback) {
  onWritable_ = callback;
}

bool TcpSocketClient::setLinger(bool enable, int timeoutSecs) {
  struct linger l;
  l.l_onoff = enable ? 1 : 0;
  l.l_linger = timeoutSecs;
  return ::setsockopt(sockfd_.get(), SOL_SOCKET, SO_LINGER, &l, sizeof(l)) == 0;
}

} // namespace tcp
} // namespace protocols
} // namespace apex
