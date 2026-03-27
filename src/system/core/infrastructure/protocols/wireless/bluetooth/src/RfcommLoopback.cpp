/**
 * @file RfcommLoopback.cpp
 * @brief Implementation of RfcommLoopback using socketpair().
 */

#include "RfcommLoopback.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

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

  if ((pfd.revents & POLLHUP) != 0 || (pfd.revents & POLLERR) != 0) {
    return Status::ERROR_CLOSED;
  }

  return Status::SUCCESS;
}

} // namespace

/* ----------------------------- RfcommLoopback ----------------------------- */

RfcommLoopback::~RfcommLoopback() { close(); }

RfcommLoopback::RfcommLoopback(RfcommLoopback&& other) noexcept
    : serverFd_(other.serverFd_), clientFd_(other.clientFd_),
      clientReleased_(other.clientReleased_) {
  other.serverFd_ = -1;
  other.clientFd_ = -1;
  other.clientReleased_ = false;
}

RfcommLoopback& RfcommLoopback::operator=(RfcommLoopback&& other) noexcept {
  if (this != &other) {
    close();
    serverFd_ = other.serverFd_;
    clientFd_ = other.clientFd_;
    clientReleased_ = other.clientReleased_;
    other.serverFd_ = -1;
    other.clientFd_ = -1;
    other.clientReleased_ = false;
  }
  return *this;
}

Status RfcommLoopback::open() noexcept {
  if (isOpen()) {
    return Status::ERROR_ALREADY_CONNECTED;
  }

  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    return Status::ERROR_IO;
  }

  serverFd_ = fds[0];
  clientFd_ = fds[1];
  clientReleased_ = false;

  // Set both to non-blocking
  if (!setNonBlocking(serverFd_) || !setNonBlocking(clientFd_)) {
    close();
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

void RfcommLoopback::close() noexcept {
  if (serverFd_ >= 0) {
    ::close(serverFd_);
    serverFd_ = -1;
  }

  // Only close client FD if we still own it
  if (clientFd_ >= 0 && !clientReleased_) {
    ::close(clientFd_);
  }
  clientFd_ = -1;
  clientReleased_ = false;
}

bool RfcommLoopback::isOpen() const noexcept { return serverFd_ >= 0; }

Status RfcommLoopback::serverRead(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                                  int timeoutMs) noexcept {
  bytesRead = 0;

  if (serverFd_ < 0) {
    return Status::ERROR_NOT_CONNECTED;
  }

  if (buffer.empty()) {
    return Status::SUCCESS;
  }

  // Wait for data if timeout specified
  if (timeoutMs != 0) {
    Status waitStatus = waitReady(serverFd_, POLLIN, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      return waitStatus;
    }
  }

  ssize_t n = ::read(serverFd_, buffer.data(), buffer.size());

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return Status::WOULD_BLOCK;
    }
    return Status::ERROR_IO;
  }

  if (n == 0) {
    return Status::ERROR_CLOSED;
  }

  bytesRead = static_cast<std::size_t>(n);
  return Status::SUCCESS;
}

Status RfcommLoopback::serverWrite(apex::compat::bytes_span data, std::size_t& bytesWritten,
                                   int timeoutMs) noexcept {
  bytesWritten = 0;

  if (serverFd_ < 0) {
    return Status::ERROR_NOT_CONNECTED;
  }

  if (data.empty()) {
    return Status::SUCCESS;
  }

  // Wait for writability if timeout specified
  if (timeoutMs != 0) {
    Status waitStatus = waitReady(serverFd_, POLLOUT, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      return waitStatus;
    }
  }

  ssize_t n = ::write(serverFd_, data.data(), data.size());

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return Status::WOULD_BLOCK;
    }
    return Status::ERROR_IO;
  }

  bytesWritten = static_cast<std::size_t>(n);
  return Status::SUCCESS;
}

int RfcommLoopback::releaseClientFd() noexcept {
  if (clientFd_ < 0 || clientReleased_) {
    return -1;
  }

  int fd = clientFd_;
  clientReleased_ = true;
  clientFd_ = -1; // Clear so we don't close it
  return fd;
}

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex
