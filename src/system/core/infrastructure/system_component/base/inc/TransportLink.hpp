#ifndef APEX_SYSTEM_CORE_BASE_TRANSPORT_LINK_HPP
#define APEX_SYSTEM_CORE_BASE_TRANSPORT_LINK_HPP
/**
 * @file TransportLink.hpp
 * @brief Transport provisioning for HW_MODEL/DRIVER virtual links.
 *
 * Provides a transport-agnostic provisioning interface. The model declares
 * a TransportKind; provisionTransport() creates the appropriate OS-level
 * link and returns a TransportLink with both endpoints.
 *
 * Supported transports:
 *   - Serial (RS-232/422/485): PTY pair. Model gets master fd, driver
 *     gets slave device path (e.g., "/dev/pts/3").
 *   - Socketpair (SPI/I2C/Unix/BT): Unix socketpair (SOCK_STREAM).
 *     Model gets fds[0], driver gets fds[1].
 *   - CAN: Unix socketpair (SOCK_DGRAM) for message-oriented emulation.
 *   - Network (TCP/UDP): Not yet implemented (returns failure).
 *
 * Adding a new transport:
 *   1. Add the TransportKind enum value (TransportKind.hpp)
 *   2. Add a case in provisionTransport() below
 *   3. No changes to HwModelBase or any executive code
 *
 * Part of the base interface layer - POSIX only, no framework dependencies.
 * All provisioning functions are NOT RT-safe (syscalls).
 */

#include "src/system/core/infrastructure/system_component/base/inc/TransportKind.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace system_core {
namespace system_component {

/* ----------------------------- TransportLink ----------------------------- */

/**
 * @struct TransportLink
 * @brief Result of transport provisioning: two endpoints for model and driver.
 *
 * After provisionTransport() succeeds:
 *   - modelFd: File descriptor for the HW_MODEL side (non-blocking).
 *   - peerFd: File descriptor for the DRIVER side (socketpair transports).
 *   - peerPath: Device path for the DRIVER side (serial transports).
 *   - kind: The transport kind that was provisioned.
 *
 * The caller owns both fds and must close them when done.
 */
struct TransportLink {
  int modelFd = -1;     ///< Model-side fd (master for PTY, fds[0] for socketpair).
  int peerFd = -1;      ///< Driver-side fd (fds[1] for socketpair, -1 for serial).
  char peerPath[128]{}; ///< Driver-side device path (PTY slave, empty for non-serial).
  TransportKind kind{TransportKind::NONE}; ///< Transport kind that was provisioned.

  /** @brief Check if provisioning succeeded. */
  [[nodiscard]] bool isValid() const noexcept { return modelFd >= 0; }

  /** @brief Check if peer is a device path (serial) vs fd (socketpair). */
  [[nodiscard]] bool hasPeerPath() const noexcept { return peerPath[0] != '\0'; }

  /** @brief Check if peer is a file descriptor (socketpair). */
  [[nodiscard]] bool hasPeerFd() const noexcept { return peerFd >= 0; }

  /**
   * @brief Close both fds. Safe to call multiple times.
   * @note NOT RT-safe: close() syscall.
   */
  void close() noexcept {
    if (modelFd >= 0) {
      ::close(modelFd);
      modelFd = -1;
    }
    if (peerFd >= 0) {
      ::close(peerFd);
      peerFd = -1;
    }
  }
};

/* ----------------------------- Provisioners ----------------------------- */

namespace detail {

/**
 * @brief Set a file descriptor to non-blocking mode.
 * @param fd File descriptor.
 * @return true on success.
 * @note NOT RT-safe.
 */
inline bool setNonBlocking(int fd) noexcept {
  const int FLAGS = ::fcntl(fd, F_GETFL, 0);
  if (FLAGS < 0) {
    return false;
  }
  return ::fcntl(fd, F_SETFL, FLAGS | O_NONBLOCK) == 0;
}

/**
 * @brief Provision a PTY pair for serial transports.
 * @param link Output link structure.
 * @return true on success.
 * @note NOT RT-safe: posix_openpt, grantpt, unlockpt, ptsname.
 */
[[nodiscard]] inline bool provisionPty(TransportLink& link) noexcept {
  const int MASTER = ::posix_openpt(O_RDWR | O_NOCTTY);
  if (MASTER < 0) {
    return false;
  }

  if (::grantpt(MASTER) != 0 || ::unlockpt(MASTER) != 0) {
    ::close(MASTER);
    return false;
  }

  const char* SLAVE = ::ptsname(MASTER);
  if (SLAVE == nullptr) {
    ::close(MASTER);
    return false;
  }

  std::strncpy(link.peerPath, SLAVE, sizeof(link.peerPath) - 1);
  link.peerPath[sizeof(link.peerPath) - 1] = '\0';

  setNonBlocking(MASTER);

  link.modelFd = MASTER;
  link.peerFd = -1; // Serial uses path, not fd.
  return true;
}

/**
 * @brief Provision a Unix socketpair.
 * @param link Output link structure.
 * @param type Socket type (SOCK_STREAM or SOCK_DGRAM).
 * @return true on success.
 * @note NOT RT-safe: socketpair syscall.
 */
[[nodiscard]] inline bool provisionSocketpair(TransportLink& link, int type) noexcept {
  int fds[2] = {-1, -1};
  if (::socketpair(AF_UNIX, type, 0, fds) != 0) {
    return false;
  }

  setNonBlocking(fds[0]);
  setNonBlocking(fds[1]);

  link.modelFd = fds[0];
  link.peerFd = fds[1];
  return true;
}

} // namespace detail

/* ----------------------------- API ----------------------------- */

/**
 * @brief Provision a virtual transport link for the given TransportKind.
 *
 * Creates the appropriate OS-level link and populates the TransportLink
 * with both endpoints (model fd + peer fd/path).
 *
 * @param kind Transport kind to provision.
 * @param link Output: populated on success, untouched on failure.
 * @return true on success, false on failure or unsupported transport.
 * @note NOT RT-safe: System calls (posix_openpt, socketpair).
 */
[[nodiscard]] inline bool provisionTransport(TransportKind kind, TransportLink& link) noexcept {
  link = TransportLink{};
  link.kind = kind;

  if (kind == TransportKind::NONE) {
    return true; // No transport needed.
  }

  if (isSerial(kind)) {
    return detail::provisionPty(link);
  }

  if (kind == TransportKind::CAN) {
    // CAN is message-oriented: use SOCK_DGRAM to preserve frame boundaries.
    return detail::provisionSocketpair(link, SOCK_DGRAM);
  }

  if (usesSocketpair(kind)) {
    // SPI, I2C, UNIX_STREAM, BLUETOOTH: byte-stream socketpair.
    return detail::provisionSocketpair(link, SOCK_STREAM);
  }

  // ETH_TCP, ETH_UDP: would need loopback sockets (not yet implemented).
  return false;
}

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_BASE_TRANSPORT_LINK_HPP
