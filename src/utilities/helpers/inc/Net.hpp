#ifndef APEX_UTILITIES_HELPERS_NET_HPP
#define APEX_UTILITIES_HELPERS_NET_HPP
/**
 * @file Net.hpp
 * @brief Minimal, portable socket utilities: FD hygiene, core TCP options,
 *        and safe nonblocking/close-on-exec setup with POSIX fallbacks.
 *
 * WARNING: RT-UNSAFE - ALL FUNCTIONS PERFORM SYSCALLS
 *
 * Every function in this header makes blocking syscalls with unpredictable latency:
 *  - socket(), accept(), setsockopt(), fcntl() - All are syscalls (~1-10us each)
 *  - epoll_ctl(), getsockopt() - More syscalls
 *  - Latency is kernel-dependent and non-deterministic
 *
 * RT-Safety Recommendations:
 *  - NEVER call these functions in RT-critical paths
 *  - Use in setup/teardown code only (before/after RT loop)
 *  - For RT networking on Linux: Consider io_uring (async, zero-copy)
 *  - For RT networking on other platforms: Dedicated RT network stack
 *
 * This header is appropriate for:
 *  - Non-RT server applications
 *  - Setup/initialization code
 *  - Background monitoring/control threads
 *
 * Build Warning for RT Systems:
 *  - Define RT_BUILD to get compile-time warning if included
 *
 * Error Handling:
 *  - No exceptions. All functions return 0 on success, -1 on failure and set errno.
 *  - Linux-specific features (TCP_CORK, TCP_QUICKACK, SO_BUSY_POLL, tcp_info) are gated.
 *  - Use alongside UniqueFd for RAII management of FDs.
 */

#if defined(RT_BUILD)
#warning "Net.hpp: RT-UNSAFE - All functions perform syscalls. Use only in non-RT setup code."
#endif

#include <cerrno>
#include <cstdint>

// POSIX headers
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <netinet/ip.h>
#include <sys/epoll.h>
#endif

namespace apex {
namespace helpers {
namespace net {

/* ----------------------------- FD Helpers ----------------------------- */

/**
 * @brief Set or clear O_NONBLOCK on a file descriptor.
 * @note RT-UNSAFE: fcntl() syscalls.
 */
inline int setNonblock(int fd, bool enable = true) noexcept {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  if (enable) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }
  return ::fcntl(fd, F_SETFL, flags);
}

/**
 * @brief Set or clear FD_CLOEXEC on a file descriptor.
 * @note RT-UNSAFE: fcntl() syscalls.
 */
inline int setCloexec(int fd, bool enable = true) noexcept {
  int flags = ::fcntl(fd, F_GETFD, 0);
  if (flags < 0) {
    return -1;
  }
  if (enable) {
    flags |= FD_CLOEXEC;
  } else {
    flags &= ~FD_CLOEXEC;
  }
  return ::fcntl(fd, F_SETFD, flags);
}

/**
 * @brief Create a socket with CLOEXEC and NONBLOCK set atomically when possible.
 *
 * @param domain  AF_INET / AF_INET6 / AF_UNIX...
 * @param type    SOCK_STREAM / SOCK_DGRAM (flags may be OR-ed)
 * @param protocol Usually 0
 * @return fd on success (>=0), -1 on error (errno set)
 * @note RT-UNSAFE: socket() syscall.
 */
inline int socketCloexecNonblock(int domain, int type, int protocol) noexcept {
  int fd;
#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
  fd = ::socket(domain, type | SOCK_CLOEXEC | SOCK_NONBLOCK, protocol);
  if (fd >= 0) {
    return fd;
  }
#endif
  fd = ::socket(domain, type, protocol);
  if (fd < 0) {
    return -1;
  }
  if (setCloexec(fd, true) != 0 || setNonblock(fd, true) != 0) {
    int saved = errno;
    ::close(fd);
    errno = saved;
    return -1;
  }
  return fd;
}

/**
 * @brief accept(2) with CLOEXEC and NONBLOCK set atomically when possible.
 *
 * @return new fd on success (>=0), -1 on error (errno set)
 * @note RT-UNSAFE: accept()/accept4() syscalls.
 */
inline int acceptCloexecNonblock(int listenfd, struct sockaddr* addr, socklen_t* addrlen) noexcept {
  int fd;
#if defined(__linux__) && defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
  fd = ::accept4(listenfd, addr, addrlen, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (fd >= 0) {
    return fd;
  }
  if (errno != ENOSYS) {
    return -1;
  }
#endif
  fd = ::accept(listenfd, addr, addrlen);
  if (fd < 0) {
    return -1;
  }
  if (setCloexec(fd, true) != 0 || setNonblock(fd, true) != 0) {
    int saved = errno;
    ::close(fd);
    errno = saved;
    return -1;
  }
  return fd;
}

/* ----------------------------- Socket Options ----------------------------- */

/**
 * @brief Enable/disable SO_REUSEADDR.
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setReuseAddr(int fd, bool on = true) noexcept {
  int v = on ? 1 : 0;
  return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
}

/**
 * @brief Enable/disable SO_REUSEPORT (platform-dependent).
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setReusePort(int fd, bool on = true) noexcept {
#if defined(SO_REUSEPORT)
  int v = on ? 1 : 0;
  return ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(v));
#else
  (void)fd;
  (void)on;
  errno = ENOPROTOOPT;
  return -1;
#endif
}

/**
 * @brief Set receive buffer size (SO_RCVBUF).
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setRcvBuf(int fd, int bytes) noexcept {
  return ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
}

/**
 * @brief Set send buffer size (SO_SNDBUF).
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setSndBuf(int fd, int bytes) noexcept {
  return ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes));
}

/**
 * @brief Enable/disable TCP_NODELAY (Nagle's algorithm).
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setNoDelay(int fd, bool on = true) noexcept {
  int v = on ? 1 : 0;
  return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
}

/**
 * @brief Enable/disable TCP_CORK (Linux). Mutually exclusive with TCP_NODELAY per flow.
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setCork(int fd, bool on = true) noexcept {
#if defined(__linux__) && defined(TCP_CORK)
  int v = on ? 1 : 0;
  return ::setsockopt(fd, IPPROTO_TCP, TCP_CORK, &v, sizeof(v));
#else
  (void)fd;
  (void)on;
  errno = ENOPROTOOPT;
  return -1;
#endif
}

/**
 * @brief Enable/disable TCP_QUICKACK (Linux) to reduce delayed ACK latency.
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setQuickAck(int fd, bool on = true) noexcept {
#if defined(__linux__) && defined(TCP_QUICKACK)
  int v = on ? 1 : 0;
  return ::setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &v, sizeof(v));
#else
  (void)fd;
  (void)on;
  errno = ENOPROTOOPT;
  return -1;
#endif
}

/**
 * @brief Configure TCP keepalive: on/off + idle, interval, count.
 * @note RT-UNSAFE: setsockopt() syscalls.
 */
inline int setKeepAlive(int fd, bool on, int idleSec = 0, int intvlSec = 0, int cnt = 0) noexcept {
  int v = on ? 1 : 0;
  if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof(v)) != 0) {
    return -1;
  }

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) ||     \
    defined(__OpenBSD__)
  if (on) {
#if defined(__linux__)
    if (idleSec > 0) {
      if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idleSec, sizeof(idleSec)) != 0) {
        return -1;
      }
    }
    if (intvlSec > 0) {
      if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvlSec, sizeof(intvlSec)) != 0) {
        return -1;
      }
    }
    if (cnt > 0) {
      if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) != 0) {
        return -1;
      }
    }
#elif defined(__APPLE__)
    if (idleSec > 0) {
      if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &idleSec, sizeof(idleSec)) != 0) {
        return -1;
      }
    }
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    if (idleSec > 0) {
      if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idleSec, sizeof(idleSec)) != 0) {
        return -1;
      }
    }
    if (intvlSec > 0) {
      if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvlSec, sizeof(intvlSec)) != 0) {
        return -1;
      }
    }
    if (cnt > 0) {
      if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) != 0) {
        return -1;
      }
    }
#endif
  }
#else
  (void)idleSec;
  (void)intvlSec;
  (void)cnt;
#endif
  return 0;
}

/**
 * @brief Set SO_BUSY_POLL (Linux) in microseconds.
 * @note RT-UNSAFE: setsockopt() syscall.
 */
inline int setBusyPoll(int fd, int microseconds) noexcept {
#if defined(__linux__) && defined(SO_BUSY_POLL)
  return ::setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &microseconds, sizeof(microseconds));
#else
  (void)fd;
  (void)microseconds;
  errno = ENOPROTOOPT;
  return -1;
#endif
}

/* ----------------------------- Epoll Helpers (Linux) ----------------------------- */

/**
 * @brief Add a file descriptor to epoll with the given event mask.
 * @note RT-UNSAFE: epoll_ctl() syscall.
 */
inline int epollAdd(int epfd, int fd, uint32_t events) noexcept {
#if defined(__linux__)
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  return ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
#else
  (void)epfd;
  (void)fd;
  (void)events;
  errno = ENOSYS;
  return -1;
#endif
}

/**
 * @brief Modify an existing file descriptor's epoll event mask.
 * @note RT-UNSAFE: epoll_ctl() syscall.
 */
inline int epollMod(int epfd, int fd, uint32_t events) noexcept {
#if defined(__linux__)
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  return ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
#else
  (void)epfd;
  (void)fd;
  (void)events;
  errno = ENOSYS;
  return -1;
#endif
}

/**
 * @brief Remove a file descriptor from epoll.
 * @note RT-UNSAFE: epoll_ctl() syscall.
 */
inline int epollDel(int epfd, int fd) noexcept {
#if defined(__linux__)
  return ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
#else
  (void)epfd;
  (void)fd;
  errno = ENOSYS;
  return -1;
#endif
}

/* ----------------------------- TCP Info (Linux) ----------------------------- */

/**
 * @brief Snapshot Linux tcp_info for an FD. Useful for sparse telemetry.
 * @note RT-UNSAFE: getsockopt() syscall.
 */
inline int getTcpInfo(int fd, struct tcp_info* info) noexcept {
#if defined(__linux__)
  socklen_t len = static_cast<socklen_t>(sizeof(*info));
  return ::getsockopt(fd, IPPROTO_TCP, TCP_INFO, info, &len);
#else
  (void)fd;
  (void)info;
  errno = ENOSYS;
  return -1;
#endif
}

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief True if errno is EAGAIN/EWOULDBLOCK (nonblocking would-block).
 * @note RT-SAFE: No syscall, just errno check.
 */
inline bool isWouldBlock(int err) noexcept {
#if defined(EWOULDBLOCK)
  return err == EAGAIN || err == EWOULDBLOCK;
#else
  return err == EAGAIN;
#endif
}

} // namespace net
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_NET_HPP
