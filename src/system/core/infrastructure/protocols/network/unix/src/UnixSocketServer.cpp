/**
 * @file UnixSocketServer.cpp
 * @brief Unix domain socket server implementation.
 */

#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketServer.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace std::chrono;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/// Helper to get current time in nanoseconds (for stats timestamps).
inline int64_t nowNs() noexcept {
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

/// Check if errno indicates EAGAIN/EWOULDBLOCK.
inline bool isWouldBlock(int err) noexcept { return err == EAGAIN || err == EWOULDBLOCK; }

/// Add fd to epoll with events.
inline int epollAdd(int epfd, int fd, uint32_t events) noexcept {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  return ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

/// Modify fd events in epoll.
inline int epollMod(int epfd, int fd, uint32_t events) noexcept {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  return ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

/// Remove fd from epoll.
inline int epollDel(int epfd, int fd) noexcept {
  return ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

} // namespace

/* ----------------------- UnixSocketServer Methods ----------------------- */

namespace apex {
namespace protocols {
namespace unix_socket {

using apex::protocols::TraceDirection;

/* ----------------------------- Construction ----------------------------- */

UnixSocketServer::UnixSocketServer(const std::string& path, UnixSocketMode mode)
    : path_(path), mode_(mode) {}

UnixSocketServer::~UnixSocketServer() {
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (int clientfd : clientsSockfds_) {
      ::close(clientfd);
    }
    clientsSockfds_.clear();
  }
  // UniqueFd destructors close stopfd_, epfd_, serverfd_
  // Unlink socket file on destruction
  if (!path_.empty()) {
    (void)::unlink(path_.c_str());
  }
}

/* ----------------------------- Initialization ----------------------------- */

uint8_t UnixSocketServer::init(bool unlinkExisting, std::string* error) {
  // Validate path length
  struct sockaddr_un addr{};
  if (path_.size() >= sizeof(addr.sun_path)) {
    if (error)
      *error = "Socket path too long (max " + std::to_string(sizeof(addr.sun_path) - 1) + ")";
    return UNIX_SERVER_ERR_PATH_TOO_LONG;
  }

  // Optionally remove existing socket file
  if (unlinkExisting) {
    if (::unlink(path_.c_str()) != 0 && errno != ENOENT) {
      if (error)
        *error = std::string("Failed to unlink existing socket: ") + std::strerror(errno);
      return UNIX_SERVER_ERR_UNLINK;
    }
  }

  // Create socket
  const int SOCK_TYPE = (mode_ == UnixSocketMode::STREAM) ? SOCK_STREAM : SOCK_DGRAM;
  int sock = ::socket(AF_UNIX, SOCK_TYPE | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sock < 0) {
    if (error)
      *error = std::string("Failed to create socket: ") + std::strerror(errno);
    return UNIX_SERVER_ERR_SOCKET_CREATION;
  }
  serverfd_.reset(sock);

  // Bind
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
  if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    if (error)
      *error = std::string("Failed to bind: ") + std::strerror(errno);
    serverfd_.reset();
    return UNIX_SERVER_ERR_BIND;
  }

  // Listen (STREAM mode only)
  if (mode_ == UnixSocketMode::STREAM) {
    if (::listen(sock, SOMAXCONN) != 0) {
      if (error)
        *error = std::string("Failed to listen: ") + std::strerror(errno);
      serverfd_.reset();
      return UNIX_SERVER_ERR_LISTEN;
    }
  }

  // Create epoll
  int ep = ::epoll_create1(EPOLL_CLOEXEC);
  if (ep < 0) {
    if (error)
      *error = std::string("Failed to create epoll: ") + std::strerror(errno);
    serverfd_.reset();
    return UNIX_SERVER_ERR_EPOLL_CREATION;
  }
  epfd_.reset(ep);

  // Add server socket to epoll (edge-triggered)
  if (epollAdd(ep, sock, EPOLLIN | EPOLLET) != 0) {
    if (error)
      *error = std::string("Failed to add server to epoll: ") + std::strerror(errno);
    epfd_.reset();
    serverfd_.reset();
    return UNIX_SERVER_ERR_EPOLL_CONFIG;
  }

  // Create stop eventfd
  int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (efd >= 0) {
    stopfd_.reset(efd);
    (void)epollAdd(ep, efd, EPOLLIN | EPOLLET);
  }

  // Record init time for stats
  stats_.connectedAtNs = nowNs();

  return UNIX_SERVER_SUCCESS;
}

/* ----------------------------- I/O Methods ----------------------------- */

ssize_t UnixSocketServer::read(int clientfd, apex::compat::bytes_span bytes, std::string* error) {
  if (bytes.size() == 0)
    return 0;

  size_t total = 0;
  for (;;) {
    ssize_t n =
        ::recv(clientfd, const_cast<unsigned char*>(bytes.data()) + total, bytes.size() - total, 0);
    if (n > 0) {
      total += static_cast<size_t>(n);
      if (total == bytes.size())
        break;
      continue;
    }
    if (n == 0) {
      // Peer closed
      break;
    }
    if (isWouldBlock(errno)) {
      break;
    }
    ++stats_.errorsRx;
    if (error)
      *error = std::string("recv failed: ") + std::strerror(errno);
    return -1;
  }

  if (total > 0) {
    stats_.bytesRx += total;
    ++stats_.packetsRx;
    stats_.lastActivityNs = nowNs();
    invokeTrace(TraceDirection::RX, bytes.data(), total);
  }
  return static_cast<ssize_t>(total);
}

ssize_t UnixSocketServer::write(int clientfd, apex::compat::bytes_span bytes, std::string* error) {
  if (bytes.size() == 0)
    return 0;

  ssize_t n = ::send(clientfd, bytes.data(), bytes.size(),
#ifdef MSG_NOSIGNAL
                     MSG_NOSIGNAL
#else
                     0
#endif
  );

  if (n >= 0) {
    if (n > 0) {
      stats_.bytesTx += static_cast<size_t>(n);
      ++stats_.packetsTx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::TX, bytes.data(), static_cast<std::size_t>(n));
    }
    if (static_cast<size_t>(n) < bytes.size()) {
      (void)epollMod(epfd_.get(), clientfd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
    }
    return n;
  }

  if (isWouldBlock(errno)) {
    (void)epollMod(epfd_.get(), clientfd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
    return 0;
  }

  ++stats_.errorsTx;
  if (error)
    *error = std::string("send failed: ") + std::strerror(errno);
  return -1;
}

void UnixSocketServer::closeConnection(int clientfd) {
  if (onConnectionClosed_) {
    onConnectionClosed_(clientfd);
  }
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clientsSockfds_.erase(clientfd);
  }
  (void)epollDel(epfd_.get(), clientfd);
  ::close(clientfd);
}

/* ----------------------------- Event Loop ----------------------------- */

void UnixSocketServer::processEvents(int timeoutMilliSecs) {
  constexpr int MAX_EVENTS = 64;
  std::array<epoll_event, MAX_EVENTS> events{};
  int nfds = ::epoll_wait(epfd_.get(), events.data(), MAX_EVENTS, timeoutMilliSecs);
  if (nfds <= 0)
    return;

  for (int i = 0; i < nfds; ++i) {
    const int FD = events[i].data.fd;
    const uint32_t EV = events[i].events;

    // Stop signal
    if (stopfd_ && FD == stopfd_.get()) {
      uint64_t v = 0;
      ssize_t n = -1;
      do {
        n = ::read(stopfd_.get(), &v, sizeof(v));
      } while (n < 0 && errno == EINTR);
      return;
    }

    // Listening socket: accept loop (STREAM mode)
    if (FD == serverfd_.get()) {
      if (mode_ == UnixSocketMode::STREAM) {
        for (;;) {
          struct sockaddr_un clientAddr{};
          socklen_t addrLen = sizeof(clientAddr);
          int clientfd = ::accept4(serverfd_.get(), reinterpret_cast<struct sockaddr*>(&clientAddr),
                                   &addrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
          if (clientfd < 0) {
            if (isWouldBlock(errno))
              break;
            if (loggingEnabled_) {
              std::printf("accept failed: %s\n", std::strerror(errno));
            }
            break;
          }

          // Max connections check
          if (maxConnections_ > 0) {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            if (clientsSockfds_.size() >= maxConnections_) {
              ::close(clientfd);
              if (loggingEnabled_) {
                std::printf("Connection rejected: at max capacity (%zu)\n", maxConnections_);
              }
              continue;
            }
          }

          // Add to epoll
          (void)epollAdd(epfd_.get(), clientfd, EPOLLIN | EPOLLRDHUP | EPOLLET);

          // Track
          {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clientsSockfds_.insert(clientfd);
          }

          if (onNewConnection_) {
            onNewConnection_(clientfd);
          }
          if (loggingEnabled_) {
            std::printf("Accepted Unix connection (fd=%d)\n", clientfd);
          }
        }
      } else {
        // DATAGRAM mode: server socket itself is readable
        if (onClientReadable_) {
          onClientReadable_(serverfd_.get());
        }
      }
      continue;
    }

    // Client socket (STREAM mode)
    if (EV & (EPOLLERR | EPOLLHUP)) {
      closeConnection(FD);
      continue;
    }
    if (EV & EPOLLRDHUP) {
      closeConnection(FD);
      continue;
    }

    if (EV & EPOLLIN) {
      if (onClientReadable_) {
        onClientReadable_(FD);
      }
    }

    if (EV & EPOLLOUT) {
      if (onClientWritable_) {
        onClientWritable_(FD);
      }
      (void)epollMod(epfd_.get(), FD, EPOLLIN | EPOLLRDHUP | EPOLLET);
    }
  }
}

void UnixSocketServer::stop() {
  if (!stopfd_)
    return;
  uint64_t one = 1;
  ssize_t n = -1;
  do {
    n = ::write(stopfd_.get(), &one, sizeof(one));
  } while (n < 0 && errno == EINTR);
}

/* ----------------------------- Callbacks ----------------------------- */

void UnixSocketServer::setOnNewConnection(apex::concurrency::Delegate<void, int> callback) {
  onNewConnection_ = callback;
}

void UnixSocketServer::setOnClientReadable(apex::concurrency::Delegate<void, int> callback) {
  onClientReadable_ = callback;
}

void UnixSocketServer::setOnClientWritable(apex::concurrency::Delegate<void, int> callback) {
  onClientWritable_ = callback;
}

void UnixSocketServer::setOnConnectionClosed(apex::concurrency::Delegate<void, int> callback) {
  onConnectionClosed_ = callback;
}

/* ----------------------------- Statistics ----------------------------- */

size_t UnixSocketServer::connectionCount() const {
  std::lock_guard<std::mutex> lock(clientsMutex_);
  return clientsSockfds_.size();
}

} // namespace unix_socket
} // namespace protocols
} // namespace apex
