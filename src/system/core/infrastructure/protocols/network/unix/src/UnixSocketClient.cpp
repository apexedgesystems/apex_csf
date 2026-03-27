/**
 * @file UnixSocketClient.cpp
 * @brief Unix domain socket client implementation.
 */

#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketClient.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fcntl.h>
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

} // namespace

/* ----------------------- UnixSocketClient Methods ----------------------- */

namespace apex {
namespace protocols {
namespace unix_socket {

using apex::protocols::TraceDirection;

/* ----------------------------- Construction ----------------------------- */

UnixSocketClient::UnixSocketClient(const std::string& serverPath, UnixSocketMode mode)
    : serverPath_(serverPath), mode_(mode) {}

UnixSocketClient::~UnixSocketClient() {
  // UniqueFd destructors automatically close sockfd_, epfd_, stopfd_
}

/* ----------------------------- Initialization ----------------------------- */

uint8_t UnixSocketClient::init(std::string* error) {
  // Validate path length
  struct sockaddr_un addr{};
  if (serverPath_.size() >= sizeof(addr.sun_path)) {
    if (error)
      *error = "Server path too long (max " + std::to_string(sizeof(addr.sun_path) - 1) + ")";
    return UNIX_CLIENT_ERR_PATH_TOO_LONG;
  }

  // Create socket
  const int SOCK_TYPE = (mode_ == UnixSocketMode::STREAM) ? SOCK_STREAM : SOCK_DGRAM;
  int sock = ::socket(AF_UNIX, SOCK_TYPE | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sock < 0) {
    if (error)
      *error = std::string("Failed to create socket: ") + std::strerror(errno);
    return UNIX_CLIENT_ERR_SOCKET_CREATION;
  }
  sockfd_.reset(sock);

  // Connect
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, serverPath_.c_str(), sizeof(addr.sun_path) - 1);
  if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    if (errno != EINPROGRESS) {
      if (error)
        *error = std::string("Failed to connect: ") + std::strerror(errno);
      sockfd_.reset();
      return UNIX_CLIENT_ERR_CONNECT;
    }
    // EINPROGRESS is OK for nonblocking connect
  }

  // Create epoll
  int ep = ::epoll_create1(EPOLL_CLOEXEC);
  if (ep < 0) {
    if (error)
      *error = std::string("Failed to create epoll: ") + std::strerror(errno);
    sockfd_.reset();
    return UNIX_CLIENT_ERR_EPOLL_CREATION;
  }
  epfd_.reset(ep);

  // Add socket to epoll (edge-triggered)
  if (epollAdd(ep, sock, EPOLLIN | EPOLLRDHUP | EPOLLET) != 0) {
    if (error)
      *error = std::string("Failed to add socket to epoll: ") + std::strerror(errno);
    epfd_.reset();
    sockfd_.reset();
    return UNIX_CLIENT_ERR_EPOLL_CONFIG;
  }

  // Create stop eventfd
  int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (efd >= 0) {
    stopfd_.reset(efd);
    (void)epollAdd(ep, efd, EPOLLIN | EPOLLET);
  }

  // Record connection time
  stats_.connectedAtNs = nowNs();

  return UNIX_CLIENT_SUCCESS;
}

/* ----------------------------- I/O Methods ----------------------------- */

ssize_t UnixSocketClient::read(apex::compat::bytes_span bytes, std::string* error) {
  if (bytes.size() == 0)
    return 0;

  size_t total = 0;
  for (;;) {
    ssize_t n = ::recv(sockfd_.get(), const_cast<unsigned char*>(bytes.data()) + total,
                       bytes.size() - total, 0);
    if (n > 0) {
      total += static_cast<size_t>(n);
      if (total == bytes.size())
        break;
      continue;
    }
    if (n == 0) {
      // Server closed
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

ssize_t UnixSocketClient::write(apex::compat::bytes_span bytes, std::string* error) {
  if (bytes.size() == 0)
    return 0;

  ssize_t n = ::send(sockfd_.get(), bytes.data(), bytes.size(),
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
      (void)epollMod(epfd_.get(), sockfd_.get(), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
    }
    return n;
  }

  if (isWouldBlock(errno)) {
    (void)epollMod(epfd_.get(), sockfd_.get(), EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
    return 0;
  }

  ++stats_.errorsTx;
  if (error)
    *error = std::string("send failed: ") + std::strerror(errno);
  return -1;
}

bool UnixSocketClient::waitReadable(int timeoutMilliSecs) {
  epoll_event ev{};
  const int N = ::epoll_wait(epfd_.get(), &ev, 1, timeoutMilliSecs);
  if (N <= 0)
    return false;
  return (ev.data.fd == sockfd_.get()) && (ev.events & EPOLLIN);
}

/* ----------------------------- Event Loop ----------------------------- */

void UnixSocketClient::processEvents(int timeoutMilliSecs) {
  constexpr int MAX_EVENTS = 16;
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

    // Socket events
    if (FD == sockfd_.get()) {
      if (EV & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        if (onDisconnected_) {
          onDisconnected_();
        }
        sockfd_.reset();
        continue;
      }

      if (EV & EPOLLIN) {
        if (onReadable_) {
          onReadable_();
        }
      }

      if (EV & EPOLLOUT) {
        if (onWritable_) {
          onWritable_();
        }
        (void)epollMod(epfd_.get(), sockfd_.get(), EPOLLIN | EPOLLRDHUP | EPOLLET);
      }
    }
  }
}

void UnixSocketClient::stop() {
  if (!stopfd_)
    return;
  uint64_t one = 1;
  ssize_t n = -1;
  do {
    n = ::write(stopfd_.get(), &one, sizeof(one));
  } while (n < 0 && errno == EINTR);
}

/* ----------------------------- Callbacks ----------------------------- */

void UnixSocketClient::setOnReadable(apex::concurrency::Delegate<void> callback) {
  onReadable_ = callback;
}

void UnixSocketClient::setOnWritable(apex::concurrency::Delegate<void> callback) {
  onWritable_ = callback;
}

void UnixSocketClient::setOnDisconnected(apex::concurrency::Delegate<void> callback) {
  onDisconnected_ = callback;
}

} // namespace unix_socket
} // namespace protocols
} // namespace apex
