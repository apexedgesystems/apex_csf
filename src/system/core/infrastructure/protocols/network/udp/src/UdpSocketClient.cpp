/**
 * @file UdpSocketClient.cpp
 * @brief UDP socket client implementation.
 */

#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketClient.hpp"

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
#if defined(__linux__)
#include <net/if.h> // ifindex types (for IPv6 mcast iface)
#endif

using namespace std::chrono;

/* ----------------------------- File Helpers ----------------------------- */

namespace {
/// Helper to get current time in nanoseconds since epoch (for stats timestamps).
inline int64_t nowNs() noexcept {
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

/* -------------------------- UdpSocketClient Methods ------------------------- */

namespace apex {
namespace protocols {
namespace udp {

using apex::protocols::TraceDirection;

UdpSocketClient::UdpSocketClient(const std::string& addr, const std::string& port)
    : addr_(addr), port_(port), desc_(addr + ":" + port) {
  enableLogging(false); // default: logging disabled
}

uint8_t UdpSocketClient::init(UdpSocketMode mode,
                              std::optional<std::reference_wrapper<std::string>> error) {
  mode_ = mode;

  // Resolve address
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  if (mode_ == UdpSocketMode::UNCONNECTED) {
    hints.ai_flags = AI_PASSIVE; // bind locally
  }

  struct addrinfo* addresses = nullptr;
  const char* host =
      (mode_ == UdpSocketMode::UNCONNECTED && addr_.empty()) ? nullptr : addr_.c_str();
  const int RV = ::getaddrinfo(host, port_.c_str(), &hints, &addresses);
  if (RV != 0) {
    if (error)
      error->get() = "getaddrinfo failed: " + std::string(::gai_strerror(RV));
    return UDP_CLIENT_ERR_GAI_FAILURE;
  }

  for (struct addrinfo* info = addresses; info != nullptr; info = info->ai_next) {
    const int SOCK = apex::helpers::net::socketCloexecNonblock(info->ai_family, info->ai_socktype,
                                                               info->ai_protocol);
    if (SOCK < 0)
      continue;

    // Common policy
    (void)apex::helpers::net::setReuseAddr(SOCK, true);
#if defined(SO_REUSEPORT)
    if (reusePort_) {
      int on = 1;
      (void)::setsockopt(SOCK, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
    }
#endif
#if defined(SO_BINDTODEVICE)
    if (!bindDevice_.empty()) {
      (void)::setsockopt(SOCK, SOL_SOCKET, SO_BINDTODEVICE, bindDevice_.c_str(),
                         bindDevice_.size());
    }
#endif
#if defined(SO_BROADCAST)
    if (broadcast_) {
      int on = 1;
      (void)::setsockopt(SOCK, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    }
#endif
#if defined(IP_TOS)
    if (tosDscp_ >= 0 && info->ai_family == AF_INET) {
      int tos = tosDscp_;
      (void)::setsockopt(SOCK, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    }
#endif
#if defined(IPV6_TCLASS)
    if (v6TClass_ >= 0 && info->ai_family == AF_INET6) {
      int tclass = v6TClass_;
      (void)::setsockopt(SOCK, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass));
    }
#endif
#if defined(IPV6_V6ONLY)
    if (info->ai_family == AF_INET6) {
      int v6only = 0;
      (void)::setsockopt(SOCK, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }
#endif
    // Optional pktinfo (dst addr/ifindex) reception
#if defined(IP_PKTINFO)
    if (pktinfoV4_ && info->ai_family == AF_INET) {
      int on = 1;
      (void)::setsockopt(SOCK, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on));
    }
#endif
#if defined(IPV6_RECVPKTINFO)
    if (pktinfoV6_ && info->ai_family == AF_INET6) {
      int on = 1;
      (void)::setsockopt(SOCK, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on));
    }
#elif defined(IPV6_PKTINFO)
    if (pktinfoV6_ && info->ai_family == AF_INET6) {
      int on = 1;
      (void)::setsockopt(sock, IPPROTO_IPV6, IPV6_PKTINFO, &on, sizeof(on));
    }
#endif

    bool ok = false;
    if (mode_ == UdpSocketMode::CONNECTED) {
      if (::connect(SOCK, info->ai_addr, info->ai_addrlen) == 0)
        ok = true;
    } else { // UNCONNECTED
      if (::bind(SOCK, info->ai_addr, info->ai_addrlen) == 0)
        ok = true;
    }

    if (ok) {
      sockfd_.reset(SOCK);
      break;
    }
    ::close(SOCK);
  }
  ::freeaddrinfo(addresses);

  if (!sockfd_) {
    if (error) {
      if (mode_ == UdpSocketMode::CONNECTED)
        error->get() = "Failed to connect UDP socket to " + desc_;
      else
        error->get() = "Failed to bind UDP socket on " + desc_;
    }
    return (mode_ == UdpSocketMode::CONNECTED) ? UDP_CLIENT_ERR_CONNECTION_FAILURE
                                               : UDP_CLIENT_ERR_BIND_FAILURE;
  }

  // epoll (CLOEXEC)
  epfd_.reset(::epoll_create1(EPOLL_CLOEXEC));
  if (!epfd_) {
    if (error)
      error->get() = std::string("epoll_create1 failed: ") + std::strerror(errno);
    sockfd_.reset();
    return UDP_CLIENT_ERR_EPOLL_CREATION;
  }

  // Register socket (edge-triggered)
  if (apex::helpers::net::epollAdd(epfd_.get(), sockfd_.get(), EPOLLIN | EPOLLET) != 0) {
    if (error)
      error->get() = std::string("epoll_ctl ADD failed: ") + std::strerror(errno);
    epfd_.reset();
    sockfd_.reset();
    return UDP_CLIENT_ERR_EPOLL_CONFIG;
  }

  // stop eventfd
  int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (efd >= 0) {
    stopfd_.reset(efd);
    (void)apex::helpers::net::epollAdd(epfd_.get(), stopfd_.get(), EPOLLIN | EPOLLET);
  }

  // Record initialization time for stats
  stats_.initAtNs = nowNs();

  return UDP_CLIENT_SUCCESS;
}

UdpSocketClient::~UdpSocketClient() {
  // UniqueFd destructors close sockfd_, epfd_, stopfd_
}

void UdpSocketClient::enableLogging(bool enable) { loggingEnabled_ = enable; }

// -------------------------- Nonblocking I/O (CONNECTED) ----------------------

ssize_t UdpSocketClient::read(apex::compat::bytes_span bytes,
                              std::optional<std::reference_wrapper<std::string>> error) {
  if (bytes.size() == 0)
    return 0;
  const ssize_t N =
      ::recv(sockfd_.get(), const_cast<unsigned char*>(bytes.data()), bytes.size(), 0);
  if (N >= 0) {
    if (N > 0) {
      stats_.bytesRx += static_cast<size_t>(N);
      ++stats_.datagramsRx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::RX, bytes.data(), static_cast<std::size_t>(N));
    }
    return N;
  }
  if (apex::helpers::net::isWouldBlock(errno))
    return 0;
  ++stats_.errorsRx;
  if (error)
    error->get() = std::string("recv failed: ") + std::strerror(errno);
  return -1;
}

ssize_t UdpSocketClient::write(apex::compat::bytes_span bytes, int /*timeoutMilliSecs*/,
                               std::optional<std::reference_wrapper<std::string>> error) {
  if (bytes.size() == 0)
    return 0;
  const ssize_t N = ::send(sockfd_.get(), bytes.data(), bytes.size(), 0);
  if (N >= 0) {
    if (N > 0) {
      stats_.bytesTx += static_cast<size_t>(N);
      ++stats_.datagramsTx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::TX, bytes.data(), static_cast<std::size_t>(N));
    }
    return N;
  }
  if (apex::helpers::net::isWouldBlock(errno)) {
    // If you ever want writable notifications, enable EPOLLOUT here and surface via onWritable_.
    // (void)apex::helpers::net::epollMod(epfd_.get(), sockfd_.get(), EPOLLIN | EPOLLOUT | EPOLLET);
    return 0;
  }
  ++stats_.errorsTx;
  if (error)
    error->get() = std::string("send failed: ") + std::strerror(errno);
  return -1;
}

// ------------------------ Blocking wait helper --------------------------------

bool UdpSocketClient::waitReadable(int timeoutMilliSecs) {
  epoll_event ev{};
  const int N = ::epoll_wait(epfd_.get(), &ev, 1, timeoutMilliSecs);
  if (N <= 0)
    return false; // timeout or error
  // Check if it's our socket and readable
  return (ev.data.fd == sockfd_.get()) && (ev.events & EPOLLIN);
}

// ----------------------- Nonblocking I/O (UNCONNECTED) -----------------------

ssize_t UdpSocketClient::readFrom(apex::compat::bytes_span bytes, PeerInfo& from,
                                  std::optional<std::reference_wrapper<std::string>> error) {
  from.addrLen = static_cast<socklen_t>(sizeof(from.addr));
  if (bytes.size() == 0)
    return 0;

  const ssize_t N =
      ::recvfrom(sockfd_.get(), const_cast<unsigned char*>(bytes.data()), bytes.size(), 0,
                 reinterpret_cast<struct sockaddr*>(&from.addr), &from.addrLen);
  if (N >= 0) {
    if (N > 0) {
      stats_.bytesRx += static_cast<size_t>(N);
      ++stats_.datagramsRx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::RX, bytes.data(), static_cast<std::size_t>(N));
    }
    return N;
  }
  if (apex::helpers::net::isWouldBlock(errno))
    return 0;
  ++stats_.errorsRx;
  if (error)
    error->get() = std::string("recvfrom failed: ") + std::strerror(errno);
  return -1;
}

ssize_t UdpSocketClient::writeTo(apex::compat::bytes_span bytes, const PeerInfo& to,
                                 int /*timeoutMilliSecs*/,
                                 std::optional<std::reference_wrapper<std::string>> error) {
  if (bytes.size() == 0)
    return 0;

  const ssize_t N = ::sendto(sockfd_.get(), bytes.data(), bytes.size(), 0,
                             reinterpret_cast<const struct sockaddr*>(&to.addr), to.addrLen);
  if (N >= 0) {
    if (N > 0) {
      stats_.bytesTx += static_cast<size_t>(N);
      ++stats_.datagramsTx;
      stats_.lastActivityNs = nowNs();
      invokeTrace(TraceDirection::TX, bytes.data(), static_cast<std::size_t>(N));
    }
    return N;
  }
  if (apex::helpers::net::isWouldBlock(errno)) {
    // Optionally enable EPOLLOUT as in write() if you want a writable callback.
    return 0;
  }
  ++stats_.errorsTx;
  if (error)
    error->get() = std::string("sendto failed: ") + std::strerror(errno);
  return -1;
}

// ------------------------------ Event loop -----------------------------------

void UdpSocketClient::processEvents(int timeoutMilliSecs) {
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
      uint64_t one = 0;
      ssize_t n = -1;
      do {
        n = ::read(stopfd_.get(), &one, sizeof(one));
      } while (n < 0 && errno == EINTR);
      // Nonblocking pipe already drained? EAGAIN is fine -- just return.
      return;
    }

    if (FD == sockfd_.get()) {
      if (EV & (EPOLLERR | EPOLLHUP)) {
        // For UDP, we don't surface a dedicated onError_ in client; user code can poll
        // read()/write() for errors.
        continue;
      }
      if (EV & EPOLLIN) {
        // RT-safe: callback is set once at init, never changes during processEvents.
        // No mutex needed - Delegate is a simple POD-like struct (fnptr + void*).
        if (onReadable_)
          onReadable_();
      }
      if (EV & EPOLLOUT) {
        // RT-safe: same as above.
        if (onWritable_)
          onWritable_();
        // Drop EPOLLOUT unless caller re-enables it after partial sends.
        (void)apex::helpers::net::epollMod(epfd_.get(), sockfd_.get(), EPOLLIN | EPOLLET);
      }
    }
  }
}

void UdpSocketClient::stop() {
  if (!stopfd_)
    return;
  const uint64_t ONE = 1;
  ssize_t n = -1;
  do {
    n = ::write(stopfd_.get(), &ONE, sizeof(ONE));
  } while (n < 0 && errno == EINTR);
  // If nonblocking pipe is full (EAGAIN), the loop will not run; epoll is already woken.
}

void UdpSocketClient::setOnReadable(apex::concurrency::Delegate<void> callback) {
  std::lock_guard<std::mutex> lk(cbMutex_);
  onReadable_ = callback;
}

void UdpSocketClient::setOnWritable(apex::concurrency::Delegate<void> callback) {
  std::lock_guard<std::mutex> lk(cbMutex_);
  onWritable_ = callback;
}

// ------------------------------ Multicast helpers ----------------------------

bool UdpSocketClient::joinMulticastV4(uint32_t groupBe, uint32_t ifaceBe) {
#if defined(IP_ADD_MEMBERSHIP)
  struct ip_mreq mreq;
  std::memset(&mreq, 0, sizeof(mreq));
  mreq.imr_multiaddr.s_addr = groupBe;
  mreq.imr_interface.s_addr = ifaceBe; // 0 => INADDR_ANY
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0;
#else
  (void)groupBe;
  (void)ifaceBe;
  return false;
#endif
}

bool UdpSocketClient::leaveMulticastV4(uint32_t groupBe, uint32_t ifaceBe) {
#if defined(IP_DROP_MEMBERSHIP)
  struct ip_mreq mreq;
  std::memset(&mreq, 0, sizeof(mreq));
  mreq.imr_multiaddr.s_addr = groupBe;
  mreq.imr_interface.s_addr = ifaceBe;
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) == 0;
#else
  (void)groupBe;
  (void)ifaceBe;
  return false;
#endif
}

bool UdpSocketClient::joinMulticastV6(const void* group16, unsigned int ifindex) {
#if defined(IPV6_ADD_MEMBERSHIP) || defined(IPV6_JOIN_GROUP)
  struct ipv6_mreq mreq6;
  std::memset(&mreq6, 0, sizeof(mreq6));
  std::memcpy(&mreq6.ipv6mr_multiaddr, group16, 16);
  mreq6.ipv6mr_interface = ifindex;
  const int OPT =
#if defined(IPV6_JOIN_GROUP)
      IPV6_JOIN_GROUP
#else
      IPV6_ADD_MEMBERSHIP
#endif
      ;
  return ::setsockopt(sockfd_.get(), IPPROTO_IPV6, OPT, &mreq6, sizeof(mreq6)) == 0;
#else
  (void)group16;
  (void)ifindex;
  return false;
#endif
}

bool UdpSocketClient::leaveMulticastV6(const void* group16, unsigned int ifindex) {
#if defined(IPV6_DROP_MEMBERSHIP) || defined(IPV6_LEAVE_GROUP)
  struct ipv6_mreq mreq6;
  std::memset(&mreq6, 0, sizeof(mreq6));
  std::memcpy(&mreq6.ipv6mr_multiaddr, group16, 16);
  mreq6.ipv6mr_interface = ifindex;
  const int OPT =
#if defined(IPV6_LEAVE_GROUP)
      IPV6_LEAVE_GROUP
#else
      IPV6_DROP_MEMBERSHIP
#endif
      ;
  return ::setsockopt(sockfd_.get(), IPPROTO_IPV6, OPT, &mreq6, sizeof(mreq6)) == 0;
#else
  (void)group16;
  (void)ifindex;
  return false;
#endif
}

bool UdpSocketClient::setMulticastLoopV4(bool on) {
#if defined(IP_MULTICAST_LOOP)
  unsigned char v = static_cast<unsigned char>(on ? 1 : 0);
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_MULTICAST_LOOP, &v, sizeof(v)) == 0;
#else
  (void)on;
  return false;
#endif
}

bool UdpSocketClient::setMulticastTtlV4(int ttl) {
#if defined(IP_MULTICAST_TTL)
  unsigned char v = static_cast<unsigned char>(ttl);
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_MULTICAST_TTL, &v, sizeof(v)) == 0;
#else
  (void)ttl;
  return false;
#endif
}

bool UdpSocketClient::setMulticastHopsV6(int hops) {
#if defined(IPV6_MULTICAST_HOPS)
  int v = hops;
  return ::setsockopt(sockfd_.get(), IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &v, sizeof(v)) == 0;
#else
  (void)hops;
  return false;
#endif
}

bool UdpSocketClient::setMulticastIfaceV4(uint32_t ifaceBe) {
#if defined(IP_MULTICAST_IF)
  struct in_addr a;
  a.s_addr = ifaceBe; // 0 => default
  return ::setsockopt(sockfd_.get(), IPPROTO_IP, IP_MULTICAST_IF, &a, sizeof(a)) == 0;
#else
  (void)ifaceBe;
  return false;
#endif
}

bool UdpSocketClient::setMulticastIfaceV6(unsigned int ifindex) {
#if defined(IPV6_MULTICAST_IF)
  unsigned int idx = ifindex; // 0 => default
  return ::setsockopt(sockfd_.get(), IPPROTO_IPV6, IPV6_MULTICAST_IF, &idx, sizeof(idx)) == 0;
#else
  (void)ifindex;
  return false;
#endif
}

// --------------------------- Connected-peer helpers --------------------------

bool UdpSocketClient::connectPeer(const PeerInfo& to,
                                  std::optional<std::reference_wrapper<std::string>> error) {
  if (::connect(sockfd_.get(), reinterpret_cast<const struct sockaddr*>(&to.addr), to.addrLen) ==
      0) {
    return true;
  }
  if (error)
    error->get() = std::string("connectPeer failed: ") + std::strerror(errno);
  return false;
}

bool UdpSocketClient::disconnectPeer(std::optional<std::reference_wrapper<std::string>> error) {
  // POSIX disconnect for datagram sockets: connect(AF_UNSPEC)
  struct sockaddr sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_family = AF_UNSPEC;
  if (::connect(sockfd_.get(), &sa, sizeof(sa)) == 0) {
    return true;
  }
  // Some stacks return errors (e.g., EAFNOSUPPORT/EINVAL); treat as best-effort.
  if (error)
    error->get() = std::string("disconnectPeer failed: ") + std::strerror(errno);
  return false;
}

bool UdpSocketClient::resolvePeer(const std::string& host, const std::string& port, PeerInfo& out,
                                  std::optional<std::reference_wrapper<std::string>> error) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  struct addrinfo* res = nullptr;
  const int RV = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
  if (RV != 0) {
    if (error)
      error->get() = "resolvePeer getaddrinfo failed: " + std::string(::gai_strerror(RV));
    return false;
  }
  bool ok = false;
  for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
    if (ai->ai_addrlen <= sizeof(out.addr)) {
      out = PeerInfo{}; // safe value-init for non-trivial type
      std::memcpy(&out.addr, ai->ai_addr, ai->ai_addrlen);
      out.addrLen = static_cast<socklen_t>(ai->ai_addrlen);
      ok = true;
      break;
    }
  }
  ::freeaddrinfo(res);
  if (!ok && error)
    error->get() = "resolvePeer: no suitable address found";
  return ok;
}

} // namespace udp
} // namespace protocols
} // namespace apex
