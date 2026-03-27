/**
 * @file PtyPair.cpp
 * @brief Implementation of pseudo-terminal pair for UART testing.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

Status waitForReady(int fd, bool forWrite, int timeoutMs) noexcept {
  if (fd < 0) {
    return Status::ERROR_CLOSED;
  }

  struct pollfd pfd{};
  pfd.fd = fd;
  pfd.events = forWrite ? POLLOUT : POLLIN;

  int result = ::poll(&pfd, 1, timeoutMs);
  if (result < 0) {
    return Status::ERROR_IO;
  }
  if (result == 0) {
    return Status::WOULD_BLOCK;
  }
  if ((pfd.revents & POLLERR) != 0 || (pfd.revents & POLLHUP) != 0) {
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

} // namespace

/* ----------------------------- PtyPair Methods ----------------------------- */

PtyPair::~PtyPair() { static_cast<void>(close()); }

PtyPair::PtyPair(PtyPair&& other) noexcept
    : masterFd_(other.masterFd_), slaveFd_(other.slaveFd_),
      slavePath_(std::move(other.slavePath_)) {
  other.masterFd_ = -1;
  other.slaveFd_ = -1;
}

PtyPair& PtyPair::operator=(PtyPair&& other) noexcept {
  if (this != &other) {
    static_cast<void>(close());
    masterFd_ = other.masterFd_;
    slaveFd_ = other.slaveFd_;
    slavePath_ = std::move(other.slavePath_);
    other.masterFd_ = -1;
    other.slaveFd_ = -1;
  }
  return *this;
}

Status PtyPair::open() noexcept {
  if (isOpen()) {
    return Status::SUCCESS;
  }

  int master = -1;
  int slave = -1;

  if (::openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
    return Status::ERROR_IO;
  }

  char* name = ::ptsname(master);
  if (name == nullptr) {
    ::close(master);
    ::close(slave);
    return Status::ERROR_IO;
  }

  int flags = ::fcntl(master, F_GETFL, 0);
  if (flags == -1 || ::fcntl(master, F_SETFL, flags | O_NONBLOCK) == -1) {
    ::close(master);
    ::close(slave);
    return Status::ERROR_IO;
  }

  masterFd_ = master;
  slaveFd_ = slave;
  slavePath_ = name;

  return Status::SUCCESS;
}

Status PtyPair::close() noexcept {
  Status result = Status::SUCCESS;

  if (slaveFd_ >= 0) {
    if (::close(slaveFd_) != 0) {
      result = Status::ERROR_IO;
    }
    slaveFd_ = -1;
  }

  if (masterFd_ >= 0) {
    if (::close(masterFd_) != 0) {
      result = Status::ERROR_IO;
    }
    masterFd_ = -1;
  }

  slavePath_.clear();
  return result;
}

bool PtyPair::isOpen() const noexcept { return masterFd_ >= 0; }

const char* PtyPair::slavePath() const noexcept {
  return slavePath_.empty() ? "" : slavePath_.c_str();
}

int PtyPair::masterFd() const noexcept { return masterFd_; }

Status PtyPair::readMaster(std::uint8_t* buffer, std::size_t bufferSize, std::size_t& bytesRead,
                           int timeoutMs) noexcept {
  bytesRead = 0;

  if (!isOpen()) {
    return Status::ERROR_CLOSED;
  }

  if (buffer == nullptr || bufferSize == 0) {
    return Status::ERROR_INVALID_ARG;
  }

  if (timeoutMs != 0) {
    Status waitStatus = waitForReady(masterFd_, false, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      return waitStatus;
    }
  }

  ssize_t result = ::read(masterFd_, buffer, bufferSize);
  if (result < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return Status::WOULD_BLOCK;
    }
    return Status::ERROR_IO;
  }
  if (result == 0) {
    return Status::ERROR_CLOSED;
  }

  bytesRead = static_cast<std::size_t>(result);
  return Status::SUCCESS;
}

Status PtyPair::writeMaster(const std::uint8_t* data, std::size_t dataSize,
                            std::size_t& bytesWritten, int timeoutMs) noexcept {
  bytesWritten = 0;

  if (!isOpen()) {
    return Status::ERROR_CLOSED;
  }

  if (data == nullptr || dataSize == 0) {
    return Status::ERROR_INVALID_ARG;
  }

  if (timeoutMs != 0) {
    Status waitStatus = waitForReady(masterFd_, true, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      return waitStatus;
    }
  }

  ssize_t result = ::write(masterFd_, data, dataSize);
  if (result < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return Status::WOULD_BLOCK;
    }
    return Status::ERROR_IO;
  }

  bytesWritten = static_cast<std::size_t>(result);
  return Status::SUCCESS;
}

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex
