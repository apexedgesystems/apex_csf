/**
 * @file TcpSocketServer.cpp
 * @brief TCP socket server implementation.
 */

#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketServer.hpp"

#include "src/utilities/helpers/inc/Net.hpp" // FD + socket option helpers

#include <array>
#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
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

/* -------------------------- TcpSocketServer Methods ------------------------- */

namespace apex {
namespace protocols {
namespace tcp {

using apex::protocols::TraceDirection;

TcpSocketServer::TcpSocketServer(const std::string& addr, const std::string& port)
    : addr_(addr), port_(port), desc_(addr + ":" + port) {
  rxLogFile_ = "tcp_server_" + desc_ + "_rx.log";
  txLogFile_ = "tcp_server_" + desc_ + "_tx.log";
}

uint8_t TcpSocketServer::init(std::optional<std::reference_wrapper<std::string>> error) {
  // Resolve bind address
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC; // v4 or v6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo* addresses = nullptr;
  int retVal =
      ::getaddrinfo(addr_.empty() ? nullptr : addr_.c_str(), port_.c_str(), &hints, &addresses);
  if (retVal != 0) {
    if (error)
      error->get() = "Failed to resolve address: " + std::string(::gai_strerror(retVal));
    return TCP_SERVER_ERR_GAI_FAILURE;
  }

  // Create, bind (with REUSEADDR), and keep first successful socket
  for (struct addrinfo* info = addresses; info != nullptr; info = info->ai_next) {
    int sock = apex::helpers::net::socketCloexecNonblock(info->ai_family, info->ai_socktype,
                                                         info->ai_protocol);
    if (sock < 0)
      continue;

    (void)apex::helpers::net::setReuseAddr(sock, true);
#if defined(IPV6_V6ONLY)
    if (info->ai_family == AF_INET6) {
      // Prefer dual-stack.
      int v6only = 0;
      ::setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }
#endif

    if (::bind(sock, info->ai_addr, info->ai_addrlen) == 0) {
      serverfd_.reset(sock);
      break;
    }
    ::close(sock);
  }
  ::freeaddrinfo(addresses);

  if (!serverfd_) {
    if (error)
      error->get() = "Failed to bind server socket.";
    return TCP_SERVER_ERR_BIND;
  }

  if (::listen(serverfd_.get(), SOMAXCONN) != 0) {
    if (error)
      error->get() = std::string("Failed to listen on server socket: ") + std::strerror(errno);
    serverfd_.reset();
    return TCP_SERVER_ERR_LISTEN;
  }

  // Create epoll (CLOEXEC)
  epfd_.reset(::epoll_create1(EPOLL_CLOEXEC));
  if (!epfd_) {
    if (error)
      error->get() = std::string("Failed to create server epoll instance: ") + std::strerror(errno);
    serverfd_.reset();
    return TCP_SERVER_ERR_EPOLL_CREATION;
  }

  // Add listening socket (edge-triggered)
  if (apex::helpers::net::epollAdd(epfd_.get(), serverfd_.get(), EPOLLIN | EPOLLET) != 0) {
    if (error)
      error->get() = std::string("Failed to add server socket to epoll: ") + std::strerror(errno);
    epfd_.reset();
    serverfd_.reset();
    return TCP_SERVER_ERR_EPOLL_CONFIG;
  }

  // Create stop eventfd and add to epoll
  int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (efd >= 0) {
    stopfd_.reset(efd);
    (void)apex::helpers::net::epollAdd(epfd_.get(), stopfd_.get(), EPOLLIN | EPOLLET);
  } else {
    // Not fatal; stop() will be a no-op if eventfd not available
  }

  return TCP_SERVER_SUCCESS;
}

// New nonblocking read overload (bytes_span)
ssize_t TcpSocketServer::read(int clientfd, apex::compat::bytes_span bytes,
                              std::optional<std::reference_wrapper<std::string>> error) {
  if (bytes.size() == 0)
    return 0;

  size_t total = 0;
  for (;;) {
    ssize_t n =
        ::recv(clientfd, const_cast<unsigned char*>(bytes.data()) + total, bytes.size() - total, 0);
    if (n > 0) {
      total += static_cast<size_t>(n);
      if (total == bytes.size())
        break; // buffer full
      // Nonblocking drain loop: continue until EAGAIN or buffer full
      continue;
    }
    if (n == 0) {
      // Peer closed (read-half). Caller may close fd.
      break;
    }
    // n < 0
    if (apex::helpers::net::isWouldBlock(errno)) {
      // Nothing more available right now
      break;
    }
    ++stats_.errorsRx;
    if (error)
      error->get() = std::string("recv failed from client: ") + std::strerror(errno);
    return -1;
  }
  // Update stats
  if (total > 0) {
    stats_.bytesRx += total;
    ++stats_.packetsRx;
    stats_.lastActivityNs = nowNs();
    invokeTrace(TraceDirection::RX, bytes.data(), total);
  }
  return static_cast<ssize_t>(total);
}

// Nonblocking write: write what the kernel will take now; enqueueing/backpressure handled in event
// loop later.
ssize_t TcpSocketServer::write(int clientfd, apex::compat::bytes_span bytes,
                               std::optional<std::reference_wrapper<std::string>> error) {
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
    // Update stats
    if (n > 0) {
      stats_.bytesTx += static_cast<size_t>(n);
      ++stats_.packetsTx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::TX, bytes.data(), static_cast<std::size_t>(n));
    }
    // If kernel couldn't take all, ask for EPOLLOUT so app can continue on writable notification
    if (static_cast<size_t>(n) < bytes.size()) {
      (void)apex::helpers::net::epollMod(epfd_.get(), clientfd,
                                         EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
    }
    return n;
  }

  if (apex::helpers::net::isWouldBlock(errno)) {
    // Request EPOLLOUT; app will get onClientWritable and can try again
    (void)apex::helpers::net::epollMod(epfd_.get(), clientfd,
                                       EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
    return 0;
  }

  ++stats_.errorsTx;
  if (error)
    error->get() = std::string("send failed: ") + std::strerror(errno);
  return -1;
}

void TcpSocketServer::writeAll(apex::compat::bytes_span bytes) {
  std::unordered_set<int> copy;
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    copy = clientsSockfds_;
  }
  for (int fd : copy) {
    std::optional<std::reference_wrapper<std::string>> errOpt = std::nullopt;
    (void)write(fd, bytes, errOpt); // nonblocking; may request EPOLLOUT
  }
}

void TcpSocketServer::closeConnection(int clientfd) {
  // Invoke callback before cleanup (allows final reads/state inspection)
  if (onConnectionClosed_) {
    onConnectionClosed_(clientfd);
  }
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clientsSockfds_.erase(clientfd);
  }
  (void)apex::helpers::net::epollDel(epfd_.get(), clientfd);
  ::close(clientfd);
}

void TcpSocketServer::enableLogging(bool enable) { loggingEnabled_ = enable; }

bool TcpSocketServer::setNonBlocking(int fd,
                                     std::optional<std::reference_wrapper<std::string>> error) {
  if (apex::helpers::net::setNonblock(fd, true) != 0) {
    if (error)
      error->get() = std::string("setNonblock failed: ") + std::strerror(errno);
    return false;
  }
  return true;
}

void TcpSocketServer::processEvents(int timeoutMilliSecs) {
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
      // Drain eventfd
      uint64_t v = 0;
      ssize_t n = -1;
      do {
        n = ::read(stopfd_.get(), &v, sizeof(v));
      } while (n < 0 && errno == EINTR);
      // Return early to let outer loop exit (EAGAIN is fine on nonblocking)
      return;
    }

    // Listening socket: drain accept loop (edge-triggered)
    if (FD == serverfd_.get()) {
      for (;;) {
        struct sockaddr_storage clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientfd = apex::helpers::net::acceptCloexecNonblock(
            serverfd_.get(), reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
        if (clientfd < 0) {
          if (apex::helpers::net::isWouldBlock(errno))
            break; // drained
          // Log but continue; accept error may be transient (e.g., EINTR)
          if (loggingEnabled_) {
            std::printf("accept failed: %s\n", std::strerror(errno));
          }
          break;
        }

        // Check max connections limit (0 = unlimited)
        if (maxConnections_ > 0) {
          std::lock_guard<std::mutex> lock(clientsMutex_);
          if (clientsSockfds_.size() >= maxConnections_) {
            // Reject: at capacity
            ::close(clientfd);
            if (loggingEnabled_) {
              std::printf("Connection rejected: at max capacity (%zu)\n", maxConnections_);
            }
            continue;
          }
        }

        // Apply default socket policy
        if (defaultNodelay_) {
          (void)apex::helpers::net::setNoDelay(clientfd, true);
        }
        if (defaultCork_) {
          (void)apex::helpers::net::setCork(clientfd, true);
        }
        if (defaultQuickAck_) {
          (void)apex::helpers::net::setQuickAck(clientfd, true);
        }
        if (defaultRcvbuf_ > 0) {
          (void)apex::helpers::net::setRcvBuf(clientfd, defaultRcvbuf_);
        }
        if (defaultSndbuf_ > 0) {
          (void)apex::helpers::net::setSndBuf(clientfd, defaultSndbuf_);
        }
        if (kaOn_) {
          (void)apex::helpers::net::setKeepAlive(clientfd, true, kaIdle_, kaIntvl_, kaCnt_);
        }
        if (defaultBusyPollUsec_ > 0) {
          (void)apex::helpers::net::setBusyPoll(clientfd, defaultBusyPollUsec_);
        }
        if (lingerOn_) {
          struct linger l;
          l.l_onoff = 1;
          l.l_linger = lingerSecs_;
          (void)::setsockopt(clientfd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
        }

        // Add to epoll: readable + half-close + edge-triggered
        (void)apex::helpers::net::epollAdd(epfd_.get(), clientfd, EPOLLIN | EPOLLRDHUP | EPOLLET);

        // Track client fd
        {
          std::lock_guard<std::mutex> lock(clientsMutex_);
          clientsSockfds_.insert(clientfd);
        }

        // Callback + optional log
        if (onNewConnection_) {
          onNewConnection_(clientfd);
        }
        if (loggingEnabled_) {
          std::array<char, NI_MAXHOST> host{};
          std::array<char, NI_MAXSERV> service{};
          if (::getnameinfo(reinterpret_cast<struct sockaddr*>(&clientAddr), addrLen, host.data(),
                            static_cast<socklen_t>(host.size()), service.data(),
                            static_cast<socklen_t>(service.size()),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            std::printf("Accepted connection from %s:%s\n", host.data(), service.data());
          }
        }
      }
      continue;
    }

    // Client socket
    // Handle errors/hangups first
    if (EV & (EPOLLERR | EPOLLHUP)) {
      closeConnection(FD);
      continue;
    }
    if (EV & EPOLLRDHUP) {
      // Read-half closed by peer; close fully for simplicity
      closeConnection(FD);
      continue;
    }

    if (EV & EPOLLIN) {
      if (onClientReadable_) {
        onClientReadable_(FD);
      }
      // Edge-triggered: callback should drain; nothing to do here
    }

    if (EV & EPOLLOUT) {
      if (onClientWritable_) {
        onClientWritable_(FD);
      }
      // After giving writable chance, drop EPOLLOUT to avoid busy loops.
      // Caller write() will re-enable if still needed.
      (void)apex::helpers::net::epollMod(epfd_.get(), FD, EPOLLIN | EPOLLRDHUP | EPOLLET);
    }
  }
}

void TcpSocketServer::setOnNewConnection(apex::concurrency::Delegate<void, int> callback) {
  onNewConnection_ = callback;
}

void TcpSocketServer::setOnClientReadable(apex::concurrency::Delegate<void, int> callback) {
  onClientReadable_ = callback;
}

void TcpSocketServer::setOnClientWritable(apex::concurrency::Delegate<void, int> callback) {
  onClientWritable_ = callback;
}

void TcpSocketServer::setOnConnectionClosed(apex::concurrency::Delegate<void, int> callback) {
  onConnectionClosed_ = callback;
}

void TcpSocketServer::stop() {
  if (!stopfd_)
    return;
  uint64_t one = 1;
  ssize_t n = -1;
  do {
    n = ::write(stopfd_.get(), &one, sizeof(one));
  } while (n < 0 && errno == EINTR);
  // If pipe is already full (nonblocking), epoll is already woken.
}

const std::unordered_set<int>& TcpSocketServer::getClientFds() const { return clientsSockfds_; }

std::mutex& TcpSocketServer::getMutex() { return clientsMutex_; }

size_t TcpSocketServer::connectionCount() const {
  std::lock_guard<std::mutex> lock(clientsMutex_);
  return clientsSockfds_.size();
}

TcpSocketServer::~TcpSocketServer() {
  {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (int clientfd : clientsSockfds_) {
      ::close(clientfd);
    }
    clientsSockfds_.clear();
  }
  // UniqueFd destructors automatically close stopfd_, epfd_, and serverfd_.
}

} // namespace tcp
} // namespace protocols
} // namespace apex
