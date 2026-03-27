/**
 * @file RfcommAdapter.cpp
 * @brief Linux implementation of RFCOMM Bluetooth connections.
 */

#include "RfcommAdapter.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

// Bluetooth headers (may not be available on all systems)
#ifdef __linux__
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#endif

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

using apex::protocols::TraceDirection;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/**
 * @brief Set socket to non-blocking mode.
 * @param fd Socket file descriptor.
 * @return true on success.
 */
bool setNonBlocking(int fd) noexcept {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return false;
  }
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

/**
 * @brief Wait for socket to be readable/writable.
 * @param fd Socket file descriptor.
 * @param events POLLIN or POLLOUT.
 * @param timeoutMs Timeout in milliseconds.
 * @return Status code.
 */
Status waitReady(int fd, short events, int timeoutMs) noexcept {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = events;
  pfd.revents = 0;

  int ret = ::poll(&pfd, 1, timeoutMs);

  if (ret < 0) {
    if (errno == EINTR) {
      return Status::WOULD_BLOCK;
    }
    return Status::ERROR_IO;
  }

  if (ret == 0) {
    return Status::WOULD_BLOCK;
  }

  if ((pfd.revents & POLLHUP) != 0) {
    return Status::ERROR_CLOSED;
  }

  if ((pfd.revents & POLLERR) != 0) {
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

/**
 * @brief Map errno to Status.
 * @param err errno value.
 * @return Appropriate Status code.
 */
Status errnoToStatus(int err) noexcept {
  switch (err) {
  case EAGAIN:
#if EAGAIN != EWOULDBLOCK
  case EWOULDBLOCK:
#endif
    return Status::WOULD_BLOCK;
  case ETIMEDOUT:
    return Status::ERROR_TIMEOUT;
  case ECONNREFUSED:
    return Status::ERROR_CONNECTION_REFUSED;
  case EHOSTUNREACH:
  case ENETUNREACH:
    return Status::ERROR_HOST_UNREACHABLE;
  case ENOTCONN:
    return Status::ERROR_NOT_CONNECTED;
  case ECONNRESET:
  case EPIPE:
    return Status::ERROR_CLOSED;
  case EINVAL:
  case EBADF:
    return Status::ERROR_INVALID_ARG;
  default:
    return Status::ERROR_IO;
  }
}

} // namespace

/* ----------------------------- RfcommAdapter ----------------------------- */

RfcommAdapter::RfcommAdapter() noexcept { std::strcpy(description_, "RFCOMM (not configured)"); }

RfcommAdapter::RfcommAdapter(int connectedFd) noexcept : fd_(connectedFd), injectedFd_(true) {
  if (fd_ >= 0) {
    configured_ = true;
    connected_ = true;
    setNonBlocking(fd_);
    std::strcpy(description_, "RFCOMM (loopback)");
  } else {
    std::strcpy(description_, "RFCOMM (invalid FD)");
  }
}

RfcommAdapter::~RfcommAdapter() { (void)disconnect(); }

const char* RfcommAdapter::description() const noexcept { return description_; }

Status RfcommAdapter::configure(const RfcommConfig& cfg) {
  // FD injection mode: configuration stored but no socket created
  if (injectedFd_) {
    config_ = cfg;
    return Status::SUCCESS;
  }

  // Validate configuration
  if (!cfg.isValid()) {
    return Status::ERROR_INVALID_ARG;
  }

  // Close existing socket if any
  if (fd_ >= 0) {
    (void)disconnect();
  }

#ifdef __linux__
  // Create Bluetooth socket
  fd_ = ::socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
  if (fd_ < 0) {
    return Status::ERROR_IO;
  }

  // Set non-blocking
  if (!setNonBlocking(fd_)) {
    ::close(fd_);
    fd_ = -1;
    return Status::ERROR_IO;
  }
#else
  // No Bluetooth support - fail gracefully
  return Status::ERROR_IO;
#endif

  config_ = cfg;
  configured_ = true;
  updateDescription();

  return Status::SUCCESS;
}

Status RfcommAdapter::connect() {
  // FD injection mode: already connected
  if (injectedFd_) {
    return connected_ ? Status::SUCCESS : Status::ERROR_NOT_CONFIGURED;
  }

  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (connected_) {
    return Status::ERROR_ALREADY_CONNECTED;
  }

  if (fd_ < 0) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  stats_.connectAttempts++;

#ifdef __linux__
  // Build remote address
  struct sockaddr_rc addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.rc_family = AF_BLUETOOTH;
  addr.rc_channel = config_.channel;

  // Copy address bytes (bdaddr_t is 6 bytes in reverse order from our storage)
  for (int i = 0; i < 6; ++i) {
    addr.rc_bdaddr.b[i] = config_.remoteAddress.bytes[5 - i];
  }

  // Non-blocking connect
  int ret = ::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

  if (ret == 0) {
    // Immediate success (unusual for non-blocking)
    connected_ = true;
    stats_.connectSuccesses++;
    return Status::SUCCESS;
  }

  if (errno != EINPROGRESS) {
    return errnoToStatus(errno);
  }

  // Wait for connection with timeout
  Status waitStatus = waitReady(fd_, POLLOUT, config_.connectTimeoutMs);
  if (waitStatus != Status::SUCCESS) {
    return waitStatus;
  }

  // Check connection result
  int error = 0;
  socklen_t errLen = sizeof(error);
  if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &errLen) < 0) {
    return Status::ERROR_IO;
  }

  if (error != 0) {
    return errnoToStatus(error);
  }

  connected_ = true;
  stats_.connectSuccesses++;
  return Status::SUCCESS;
#else
  return Status::ERROR_IO;
#endif
}

Status RfcommAdapter::disconnect() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }

  if (connected_) {
    stats_.disconnects++;
  }

  connected_ = false;

  // For injected FD, we're done
  if (injectedFd_) {
    injectedFd_ = false; // Reset so adapter can be reused
    configured_ = false;
    return Status::SUCCESS;
  }

  // Production mode: socket is closed but config retained
  return Status::SUCCESS;
}

bool RfcommAdapter::isConnected() const noexcept { return connected_ && fd_ >= 0; }

Status RfcommAdapter::read(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                           int timeoutMs) noexcept {
  bytesRead = 0;

  if (!isConnected()) {
    stats_.readErrors++;
    return Status::ERROR_NOT_CONNECTED;
  }

  if (buffer.empty()) {
    return Status::SUCCESS;
  }

  // Optimistic read first
  ssize_t n = ::read(fd_, buffer.data(), buffer.size());

  if (n > 0) {
    bytesRead = static_cast<std::size_t>(n);
    stats_.bytesRx += bytesRead;
    stats_.readsCompleted++;
    invokeTrace(TraceDirection::RX, buffer.data(), bytesRead);
    return Status::SUCCESS;
  }

  if (n == 0) {
    stats_.readErrors++;
    return Status::ERROR_CLOSED;
  }

  // Handle EAGAIN - wait if timeout specified
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    if (timeoutMs == 0) {
      stats_.readWouldBlock++;
      return Status::WOULD_BLOCK;
    }

    Status waitStatus = waitReady(fd_, POLLIN, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      if (waitStatus == Status::WOULD_BLOCK) {
        stats_.readWouldBlock++;
      } else {
        stats_.readErrors++;
      }
      return waitStatus;
    }

    // Retry after wait
    n = ::read(fd_, buffer.data(), buffer.size());

    if (n > 0) {
      bytesRead = static_cast<std::size_t>(n);
      stats_.bytesRx += bytesRead;
      stats_.readsCompleted++;
      invokeTrace(TraceDirection::RX, buffer.data(), bytesRead);
      return Status::SUCCESS;
    }

    if (n == 0) {
      stats_.readErrors++;
      return Status::ERROR_CLOSED;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      stats_.readWouldBlock++;
      return Status::WOULD_BLOCK;
    }
  }

  stats_.readErrors++;
  return errnoToStatus(errno);
}

Status RfcommAdapter::write(apex::compat::bytes_span data, std::size_t& bytesWritten,
                            int timeoutMs) noexcept {
  bytesWritten = 0;

  if (!isConnected()) {
    stats_.writeErrors++;
    return Status::ERROR_NOT_CONNECTED;
  }

  if (data.empty()) {
    return Status::SUCCESS;
  }

  // Optimistic write first
  ssize_t n = ::write(fd_, data.data(), data.size());

  if (n > 0) {
    bytesWritten = static_cast<std::size_t>(n);
    stats_.bytesTx += bytesWritten;
    stats_.writesCompleted++;
    invokeTrace(TraceDirection::TX, data.data(), bytesWritten);
    return Status::SUCCESS;
  }

  // Handle EAGAIN - wait if timeout specified
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    if (timeoutMs == 0) {
      stats_.writeWouldBlock++;
      return Status::WOULD_BLOCK;
    }

    Status waitStatus = waitReady(fd_, POLLOUT, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      if (waitStatus == Status::WOULD_BLOCK) {
        stats_.writeWouldBlock++;
      } else {
        stats_.writeErrors++;
      }
      return waitStatus;
    }

    // Retry after wait
    n = ::write(fd_, data.data(), data.size());

    if (n > 0) {
      bytesWritten = static_cast<std::size_t>(n);
      stats_.bytesTx += bytesWritten;
      stats_.writesCompleted++;
      invokeTrace(TraceDirection::TX, data.data(), bytesWritten);
      return Status::SUCCESS;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      stats_.writeWouldBlock++;
      return Status::WOULD_BLOCK;
    }
  }

  stats_.writeErrors++;
  return errnoToStatus(errno);
}

const RfcommStats& RfcommAdapter::stats() const noexcept { return stats_; }

void RfcommAdapter::resetStats() noexcept { stats_.reset(); }

int RfcommAdapter::fd() const noexcept { return fd_; }

void RfcommAdapter::updateDescription() noexcept {
  char addrStr[BLUETOOTH_ADDRESS_STRING_SIZE];
  config_.remoteAddress.toString(addrStr, sizeof(addrStr));

  std::snprintf(description_, sizeof(description_), "RFCOMM %s ch%u", addrStr, config_.channel);
}

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex
