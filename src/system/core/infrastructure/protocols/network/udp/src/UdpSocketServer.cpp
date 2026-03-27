/**
 * @file UdpSocketServer.cpp
 * @brief UDP socket server implementation.
 */

#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketServer.hpp"

#include "src/utilities/helpers/inc/Net.hpp" // socket/epoll helpers

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
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

/* -------------------------- UdpSocketServer Methods ------------------------- */

namespace apex {
namespace protocols {
namespace udp {

using apex::protocols::TraceDirection;

UdpSocketServer::UdpSocketServer(const std::string& addr, const std::string& port)
    : addr_(addr), port_(port), desc_(addr + ":" + port),
      rxLogFile_("udp_server_" + desc_ + "_rx.log"), txLogFile_("udp_server_" + desc_ + "_tx.log") {
  enableLogging(false); // default: logging disabled
}

uint8_t UdpSocketServer::init(std::optional<std::reference_wrapper<std::string>> error) {
  // Resolve the local bind address using AI_PASSIVE.
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;    // v4 or v6
  hints.ai_socktype = SOCK_DGRAM; // UDP
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo* addresses = nullptr;
  const int RET_VAL =
      ::getaddrinfo(addr_.empty() ? nullptr : addr_.c_str(), port_.c_str(), &hints, &addresses);
  if (RET_VAL != 0) {
    if (error)
      error->get() = "getaddrinfo failed: " + std::string(::gai_strerror(RET_VAL));
    return UDP_SERVER_ERR_GAI_FAILURE;
  }

  // Create & bind first successful address (NONBLOCK|CLOEXEC). Prefer dual-stack for v6.
  for (struct addrinfo* info = addresses; info != nullptr; info = info->ai_next) {
    const int SOCK = apex::helpers::net::socketCloexecNonblock(info->ai_family, info->ai_socktype,
                                                               info->ai_protocol);
    if (SOCK < 0)
      continue;

    // Core: allow quick rebinding
    (void)apex::helpers::net::setReuseAddr(SOCK, true);

    // Optional: SO_REUSEPORT (must typically be set before bind)
    if (reusePort_) {
#if defined(SO_REUSEPORT)
      int on = 1;
      (void)::setsockopt(SOCK, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
    }

    // Optional: bind traffic to a specific interface (Linux)
#if defined(SO_BINDTODEVICE)
    if (!bindDevice_.empty()) {
      (void)::setsockopt(SOCK, SOL_SOCKET, SO_BINDTODEVICE, bindDevice_.c_str(),
                         bindDevice_.size());
    }
#endif

    // Optional: IPv4 broadcast permission
    if (broadcast_) {
#if defined(SO_BROADCAST)
      int on = 1;
      (void)::setsockopt(SOCK, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
#endif
    }

    // Optional: DSCP/TOS (IPv4)
    if (tosDscp_ >= 0 && info->ai_family == AF_INET) {
#if defined(IP_TOS)
      int tos = tosDscp_;
      (void)::setsockopt(SOCK, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
#endif
    }

    // Optional: Traffic Class (IPv6)
    if (v6TClass_ >= 0 && info->ai_family == AF_INET6) {
#if defined(IPV6_TCLASS)
      int tclass = v6TClass_;
      (void)::setsockopt(SOCK, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass));
#endif
    }

    // Optional: per-packet info (dst iface/addr)
    if (pktinfoV4_ && info->ai_family == AF_INET) {
#if defined(IP_PKTINFO)
      int on = 1;
      (void)::setsockopt(SOCK, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on));
#endif
    }
    if (pktinfoV6_ && info->ai_family == AF_INET6) {
#if defined(IPV6_RECVPKTINFO)
      int on = 1;
      (void)::setsockopt(SOCK, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on));
#endif
    }

#if defined(IPV6_V6ONLY)
    if (info->ai_family == AF_INET6) {
      int v6only = 0; // prefer dual-stack unless user bound explicitly to v6-only address
      (void)::setsockopt(SOCK, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }
#endif

    if (::bind(SOCK, info->ai_addr, info->ai_addrlen) == 0) {
      sockfd_.reset(SOCK);
      break;
    }
    ::close(SOCK);
  }
  ::freeaddrinfo(addresses);

  if (!sockfd_) {
    if (error)
      error->get() = "Failed to bind UDP socket on " + desc_;
    return UDP_SERVER_ERR_BIND;
  }

  // Create the epoll instance (CLOEXEC).
  epfd_.reset(::epoll_create1(EPOLL_CLOEXEC));
  if (!epfd_) {
    if (error)
      error->get() = "epoll_create1 failed: " + std::string(std::strerror(errno));
    sockfd_.reset();
    return UDP_SERVER_ERR_EPOLL_CREATION;
  }

  // Add UDP socket to epoll (edge-triggered).
  if (apex::helpers::net::epollAdd(epfd_.get(), sockfd_.get(), EPOLLIN | EPOLLET) != 0) {
    if (error)
      error->get() = "epoll_ctl ADD failed: " + std::string(std::strerror(errno));
    epfd_.reset();
    sockfd_.reset();
    return UDP_SERVER_ERR_EPOLL_CONFIG;
  }

  // Create stop eventfd and add to epoll (edge-triggered).
  int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (efd >= 0) {
    stopfd_.reset(efd);
    (void)apex::helpers::net::epollAdd(epfd_.get(), stopfd_.get(), EPOLLIN | EPOLLET);
  }

  // Record initialization time for stats
  stats_.initAtNs = nowNs();

  return UDP_SERVER_SUCCESS;
}

UdpSocketServer::~UdpSocketServer() {
  // UniqueFd destructors automatically close sockfd_, epfd_, and stopfd_.
}

void UdpSocketServer::enableLogging(bool enable) { loggingEnabled_ = enable; }

void UdpSocketServer::setLogLevel(int level) { logLevel_ = level; }

// ---- Nonblocking recvfrom into caller buffer (apex::compat::bytes_span) ----
ssize_t UdpSocketServer::read(apex::compat::bytes_span bytes, RecvInfo& info,
                              std::optional<std::reference_wrapper<std::string>> error) {
  info.addrLen = static_cast<socklen_t>(sizeof(info.addr));
  if (bytes.size() == 0) {
    info.nread = 0;
    return 0;
  }

  const ssize_t N =
      ::recvfrom(sockfd_.get(), const_cast<unsigned char*>(bytes.data()), bytes.size(), 0,
                 reinterpret_cast<struct sockaddr*>(&info.addr), &info.addrLen);
  if (N >= 0) {
    info.nread = N;
    // Update stats on success
    if (N > 0) {
      stats_.bytesRx += static_cast<size_t>(N);
      ++stats_.datagramsRx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::RX, bytes.data(), static_cast<std::size_t>(N));
    }
    return N;
  }
  if (apex::helpers::net::isWouldBlock(errno)) {
    info.nread = 0; // would block: nothing available now
    return 0;
  }
  ++stats_.errorsRx;
  if (error)
    error->get() = std::string("recvfrom failed: ") + std::strerror(errno);
  info.nread = -1;
  return -1;
}

// ---- Nonblocking sendto using apex::compat::bytes_span ----
ssize_t UdpSocketServer::write(const RecvInfo& client, apex::compat::bytes_span bytes,
                               int /*timeoutMilliSecs*/,
                               std::optional<std::reference_wrapper<std::string>> error) {
  if (bytes.size() == 0)
    return 0;

  const ssize_t NSENT =
      ::sendto(sockfd_.get(), bytes.data(), bytes.size(), 0,
               reinterpret_cast<const struct sockaddr*>(&client.addr), client.addrLen);
  if (NSENT >= 0) {
    // Update stats on success
    if (NSENT > 0) {
      stats_.bytesTx += static_cast<size_t>(NSENT);
      ++stats_.datagramsTx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::TX, bytes.data(), static_cast<std::size_t>(NSENT));
    }
    if (loggingEnabled_ && NSENT > 0 && logLevel_ > 1) {
      std::printf("[udp:%s] sent %zd bytes\n", desc_.c_str(), NSENT);
    }
    return NSENT;
  }

  if (apex::helpers::net::isWouldBlock(errno)) {
    // Typically EPOLLOUT is not needed for UDP; if desired we could enable it and surface a
    // writable callback. (void)apex::helpers::net::epollMod(epfd_.get(), sockfd_.get(), EPOLLIN |
    // EPOLLOUT | EPOLLET);
    return 0;
  }

  ++stats_.errorsTx;
  if (error)
    error->get() = "sendto failed: " + std::string(std::strerror(errno));
  return -1;
}

// ---- Batched I/O (recvmmsg/sendmmsg) ----

int UdpSocketServer::readBatch(apex::compat::bytes_span* bufs, RecvInfo* infos, size_t count) {
  if (count == 0)
    return 0;

#if defined(__linux__) && defined(_GNU_SOURCE)
  // Use recvmmsg for efficient batch reception
  constexpr size_t MAX_BATCH = 64;
  const size_t N = (count > MAX_BATCH) ? MAX_BATCH : count;

  std::array<struct mmsghdr, MAX_BATCH> msgs{};
  std::array<struct iovec, MAX_BATCH> iovs{};

  for (size_t i = 0; i < N; ++i) {
    infos[i].addrLen = static_cast<socklen_t>(sizeof(infos[i].addr));
    iovs[i].iov_base = const_cast<unsigned char*>(bufs[i].data());
    iovs[i].iov_len = bufs[i].size();

    msgs[i].msg_hdr.msg_name = &infos[i].addr;
    msgs[i].msg_hdr.msg_namelen = infos[i].addrLen;
    msgs[i].msg_hdr.msg_iov = &iovs[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
    msgs[i].msg_hdr.msg_control = nullptr;
    msgs[i].msg_hdr.msg_controllen = 0;
    msgs[i].msg_hdr.msg_flags = 0;
  }

  const int RECEIVED =
      ::recvmmsg(sockfd_.get(), msgs.data(), static_cast<unsigned int>(N), MSG_DONTWAIT, nullptr);
  if (RECEIVED < 0) {
    if (apex::helpers::net::isWouldBlock(errno))
      return 0;
    ++stats_.errorsRx;
    return -1;
  }

  // Fill in actual lengths and update stats
  for (int i = 0; i < RECEIVED; ++i) {
    infos[i].nread = static_cast<ssize_t>(msgs[i].msg_len);
    infos[i].addrLen = msgs[i].msg_hdr.msg_namelen;
    if (msgs[i].msg_len > 0) {
      stats_.bytesRx += msgs[i].msg_len;
      ++stats_.datagramsRx;
    }
  }
  if (RECEIVED > 0)
    stats_.lastActivityNs = nowNs();

  return RECEIVED;
#else
  // Fallback: single recv per call
  ssize_t n = read(bufs[0], infos[0], std::nullopt);
  if (n < 0)
    return -1;
  return (n >= 0) ? 1 : 0;
#endif
}

int UdpSocketServer::writeBatch(const RecvInfo* clients, const apex::compat::bytes_span* bufs,
                                size_t count) {
  if (count == 0)
    return 0;

#if defined(__linux__) && defined(_GNU_SOURCE)
  // Use sendmmsg for efficient batch transmission
  constexpr size_t MAX_BATCH = 64;
  const size_t N = (count > MAX_BATCH) ? MAX_BATCH : count;

  std::array<struct mmsghdr, MAX_BATCH> msgs{};
  std::array<struct iovec, MAX_BATCH> iovs{};

  for (size_t i = 0; i < N; ++i) {
    iovs[i].iov_base = const_cast<unsigned char*>(bufs[i].data());
    iovs[i].iov_len = bufs[i].size();

    msgs[i].msg_hdr.msg_name = const_cast<struct sockaddr_storage*>(&clients[i].addr);
    msgs[i].msg_hdr.msg_namelen = clients[i].addrLen;
    msgs[i].msg_hdr.msg_iov = &iovs[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
    msgs[i].msg_hdr.msg_control = nullptr;
    msgs[i].msg_hdr.msg_controllen = 0;
    msgs[i].msg_hdr.msg_flags = 0;
  }

  const int SENT = ::sendmmsg(sockfd_.get(), msgs.data(), static_cast<unsigned int>(N), 0);
  if (SENT < 0) {
    if (apex::helpers::net::isWouldBlock(errno))
      return 0;
    ++stats_.errorsTx;
    return -1;
  }

  // Update stats
  for (int i = 0; i < SENT; ++i) {
    if (msgs[i].msg_len > 0) {
      stats_.bytesTx += msgs[i].msg_len;
      ++stats_.datagramsTx;
    }
  }
  if (SENT > 0)
    stats_.lastActivityNs = nowNs();

  return SENT;
#else
  // Fallback: single send per call
  ssize_t n = write(clients[0], bufs[0], 0, std::nullopt);
  if (n < 0)
    return -1;
  return (n >= 0) ? 1 : 0;
#endif
}

void UdpSocketServer::processEvents(int timeoutMilliSecs) {
  constexpr int MAX_EVENTS = 64;
  std::array<epoll_event, MAX_EVENTS> events{};
  const int NFDS = ::epoll_wait(epfd_.get(), events.data(), MAX_EVENTS, timeoutMilliSecs);
  if (NFDS <= 0)
    return;

  for (int i = 0; i < NFDS; ++i) {
    const int FD = events[i].data.fd;
    const uint32_t EV = events[i].events;

    // Stop signal
    if (stopfd_ && FD == stopfd_.get()) {
      uint64_t v = 0;
      ssize_t n = -1;
      do {
        n = ::read(stopfd_.get(), &v, sizeof(v));
      } while (n < 0 && errno == EINTR);
      // Nonblocking pipe already drained? EAGAIN is fine -- just return.
      return; // let outer loop exit
    }

    // UDP socket
    if (FD == sockfd_.get()) {
      if (EV & (EPOLLERR | EPOLLHUP)) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (onError_) {
          onError_(std::string("epoll reported error/hangup on UDP socket: ") +
                   std::strerror(errno));
        }
        continue;
      }

      if (EV & EPOLLIN) {
        // RT-safe: callback is set once at init, never changes during processEvents.
        // No mutex needed - Delegate is a simple POD-like struct (fnptr + void*).
        if (onDatagramReceived_)
          onDatagramReceived_(); // callback should drain recvfrom() until EAGAIN
      }
    }
  }
}

void UdpSocketServer::stop() {
  if (!stopfd_)
    return;
  const uint64_t ONE = 1;
  ssize_t n = -1;
  do {
    n = ::write(stopfd_.get(), &ONE, sizeof(ONE));
  } while (n < 0 && errno == EINTR);
  // If the nonblocking pipe is already full (EAGAIN), epoll is already woken.
}

void UdpSocketServer::setOnDatagramReceived(apex::concurrency::Delegate<void> callback) {
  std::lock_guard<std::mutex> lock(callbackMutex_);
  onDatagramReceived_ = callback;
}

void UdpSocketServer::setOnError(const std::function<void(const std::string&)>& callback) {
  std::lock_guard<std::mutex> lock(callbackMutex_);
  onError_ = callback;
}

// ----------------------------- Multicast helpers -----------------------------

bool UdpSocketServer::joinMulticastV4(uint32_t groupBe, uint32_t ifaceBe) {
#if defined(IP_ADD_MEMBERSHIP)
  ip_mreq m{};
  m.imr_multiaddr.s_addr = groupBe;
  m.imr_interface.s_addr = ifaceBe; // 0 = INADDR_ANY
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) == 0;
#else
  (void)groupBe;
  (void)ifaceBe;
  return false;
#endif
}

bool UdpSocketServer::leaveMulticastV4(uint32_t groupBe, uint32_t ifaceBe) {
#if defined(IP_DROP_MEMBERSHIP)
  ip_mreq m{};
  m.imr_multiaddr.s_addr = groupBe;
  m.imr_interface.s_addr = ifaceBe;
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_DROP_MEMBERSHIP, &m, sizeof(m)) == 0;
#else
  (void)groupBe;
  (void)ifaceBe;
  return false;
#endif
}

bool UdpSocketServer::joinMulticastV6(const void* group16, unsigned int ifindex) {
#if defined(IPV6_JOIN_GROUP)
  ipv6_mreq m{};
  std::memcpy(&m.ipv6mr_multiaddr, group16, 16);
  m.ipv6mr_interface = ifindex; // 0 = kernel picks
  return ::setsockopt(sockfd_.get(), IPPROTO_IPV6, IPV6_JOIN_GROUP, &m, sizeof(m)) == 0;
#else
  (void)group16;
  (void)ifindex;
  return false;
#endif
}

bool UdpSocketServer::leaveMulticastV6(const void* group16, unsigned int ifindex) {
#if defined(IPV6_LEAVE_GROUP)
  ipv6_mreq m{};
  std::memcpy(&m.ipv6mr_multiaddr, group16, 16);
  m.ipv6mr_interface = ifindex;
  return ::setsockopt(sockfd_.get(), IPPROTO_IPV6, IPV6_LEAVE_GROUP, &m, sizeof(m)) == 0;
#else
  (void)group16;
  (void)ifindex;
  return false;
#endif
}

bool UdpSocketServer::setMulticastLoopV4(bool on) {
#if defined(IP_MULTICAST_LOOP)
  unsigned char v = on ? 1u : 0u;
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_MULTICAST_LOOP, &v, sizeof(v)) == 0;
#else
  (void)on;
  return false;
#endif
}

bool UdpSocketServer::setMulticastTtlV4(int ttl) {
#if defined(IP_MULTICAST_TTL)
  unsigned char v = static_cast<unsigned char>(ttl);
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_MULTICAST_TTL, &v, sizeof(v)) == 0;
#else
  (void)ttl;
  return false;
#endif
}

bool UdpSocketServer::setMulticastHopsV6(int hops) {
#if defined(IPV6_MULTICAST_HOPS)
  int v = hops;
  return ::setsockopt(sockfd_.get(), IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &v, sizeof(v)) == 0;
#else
  (void)hops;
  return false;
#endif
}

} // namespace udp
} // namespace protocols
} // namespace apex
