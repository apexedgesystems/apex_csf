/**
 * @file UartAdapter.cpp
 * @brief Linux UART device implementation with termios configuration.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

#include "src/utilities/compatibility/inc/compat_attributes.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/serial.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

using apex::protocols::TraceDirection;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

speed_t baudToSpeed(BaudRate rate) noexcept {
  switch (rate) {
  case BaudRate::B_1200:
    return B1200;
  case BaudRate::B_2400:
    return B2400;
  case BaudRate::B_4800:
    return B4800;
  case BaudRate::B_9600:
    return B9600;
  case BaudRate::B_19200:
    return B19200;
  case BaudRate::B_38400:
    return B38400;
  case BaudRate::B_57600:
    return B57600;
  case BaudRate::B_115200:
    return B115200;
  case BaudRate::B_230400:
    return B230400;
  case BaudRate::B_460800:
    return B460800;
  case BaudRate::B_500000:
    return B500000;
  case BaudRate::B_576000:
    return B576000;
  case BaudRate::B_921600:
    return B921600;
  case BaudRate::B_1000000:
    return B1000000;
  case BaudRate::B_1152000:
    return B1152000;
  case BaudRate::B_1500000:
    return B1500000;
  case BaudRate::B_2000000:
    return B2000000;
  case BaudRate::B_2500000:
    return B2500000;
  case BaudRate::B_3000000:
    return B3000000;
  case BaudRate::B_3500000:
    return B3500000;
  case BaudRate::B_4000000:
    return B4000000;
  default:
    return B0;
  }
}

tcflag_t dataBitsToFlag(DataBits bits) noexcept {
  switch (bits) {
  case DataBits::FIVE:
    return CS5;
  case DataBits::SIX:
    return CS6;
  case DataBits::SEVEN:
    return CS7;
  case DataBits::EIGHT:
  default:
    return CS8;
  }
}

} // namespace

/* ----------------------------- UartAdapter Methods ----------------------------- */

UartAdapter::UartAdapter(const std::string& devicePath) : devicePath_(devicePath) {}

UartAdapter::UartAdapter(std::string&& devicePath) : devicePath_(std::move(devicePath)) {}

UartAdapter::~UartAdapter() { static_cast<void>(close()); }

UartAdapter::UartAdapter(UartAdapter&& other) noexcept
    : devicePath_(std::move(other.devicePath_)), fd_(other.fd_), configured_(other.configured_),
      stats_(other.stats_) {
  other.fd_ = -1;
  other.configured_ = false;
  other.stats_.reset();
}

UartAdapter& UartAdapter::operator=(UartAdapter&& other) noexcept {
  if (this != &other) {
    static_cast<void>(close());
    devicePath_ = std::move(other.devicePath_);
    fd_ = other.fd_;
    configured_ = other.configured_;
    stats_ = other.stats_;
    other.fd_ = -1;
    other.configured_ = false;
    other.stats_.reset();
  }
  return *this;
}

Status UartAdapter::configure(const UartConfig& config) noexcept {
  if (fd_ >= 0) {
    static_cast<void>(close());
  }

  Status status = openDevice(config.exclusiveAccess);
  if (status != Status::SUCCESS) {
    return status;
  }

  status = applyTermios(config);
  if (status != Status::SUCCESS) {
    static_cast<void>(close());
    return status;
  }

  if (config.rs485.enabled) {
    status = applyRs485(config.rs485);
    if (status != Status::SUCCESS) {
      static_cast<void>(close());
      return status;
    }
  }

  if (config.lowLatency) {
    status = applyLowLatency(true);
    if (status != Status::SUCCESS) {
      static_cast<void>(close());
      return status;
    }
  }

  if (::tcflush(fd_, TCIOFLUSH) != 0) {
    static_cast<void>(close());
    return Status::ERROR_IO;
  }

  configured_ = true;
  return Status::SUCCESS;
}

Status UartAdapter::read(std::uint8_t* buffer, std::size_t bufferSize, std::size_t& bytesRead,
                         int timeoutMs) noexcept {
  bytesRead = 0;

  if (COMPAT_UNLIKELY(!configured_)) {
    ++stats_.readErrors;
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (COMPAT_UNLIKELY(buffer == nullptr || bufferSize == 0)) {
    ++stats_.readErrors;
    return Status::ERROR_INVALID_ARG;
  }

  // Optimistic read: Try the read first, only poll if it would block.
  // This eliminates a syscall in the common case where data is available.
  ssize_t result = ::read(fd_, buffer, bufferSize);
  if (COMPAT_UNLIKELY(result < 0)) {
    if (COMPAT_LIKELY(errno == EAGAIN || errno == EWOULDBLOCK)) {
      // No data available - poll if timeout allows
      if (timeoutMs == 0) {
        ++stats_.readWouldBlock;
        return Status::WOULD_BLOCK;
      }
      Status waitStatus = waitForReady(false, timeoutMs);
      if (waitStatus != Status::SUCCESS) {
        if (waitStatus == Status::WOULD_BLOCK) {
          ++stats_.readWouldBlock;
        } else {
          ++stats_.readErrors;
        }
        return waitStatus;
      }
      // Retry after poll indicates data ready
      result = ::read(fd_, buffer, bufferSize);
      if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          ++stats_.readWouldBlock;
          return Status::WOULD_BLOCK;
        }
        ++stats_.readErrors;
        return Status::ERROR_IO;
      }
      // Note: result == 0 after poll+retry is WOULD_BLOCK (poll timeout).
      // This can happen with PTYs when timeout expires during poll.
      if (result == 0) {
        ++stats_.readWouldBlock;
        return Status::WOULD_BLOCK;
      }
    } else {
      ++stats_.readErrors;
      return Status::ERROR_IO;
    }
  }
  // result == 0 on first read without EAGAIN means genuine EOF (peer closed)
  // However, we need to verify by polling - PTYs can return 0 spuriously.
  if (result == 0) {
    // Check if this is a true EOF or just no data available
    // Use poll with POLLHUP to detect peer closure
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int pollResult = ::poll(&pfd, 1, 0); // Non-blocking check
    if (pollResult > 0 && (pfd.revents & POLLHUP) != 0) {
      // Peer actually closed
      ++stats_.readErrors;
      return Status::ERROR_CLOSED;
    }
    // No POLLHUP - treat as would-block, poll if timeout allows
    if (timeoutMs == 0) {
      ++stats_.readWouldBlock;
      return Status::WOULD_BLOCK;
    }
    Status waitStatus = waitForReady(false, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      if (waitStatus == Status::WOULD_BLOCK) {
        ++stats_.readWouldBlock;
      } else {
        ++stats_.readErrors;
      }
      return waitStatus;
    }
    // Retry after poll
    result = ::read(fd_, buffer, bufferSize);
    if (result <= 0) {
      if (result == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
        ++stats_.readWouldBlock;
        return Status::WOULD_BLOCK;
      }
      ++stats_.readErrors;
      return Status::ERROR_IO;
    }
  }

  bytesRead = static_cast<std::size_t>(result);
  stats_.bytesRx += bytesRead;
  ++stats_.readsCompleted;
  invokeTrace(TraceDirection::RX, buffer, bytesRead);
  return Status::SUCCESS;
}

Status UartAdapter::write(const std::uint8_t* data, std::size_t dataSize, std::size_t& bytesWritten,
                          int timeoutMs) noexcept {
  bytesWritten = 0;

  if (COMPAT_UNLIKELY(!configured_)) {
    ++stats_.writeErrors;
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (COMPAT_UNLIKELY(data == nullptr || dataSize == 0)) {
    ++stats_.writeErrors;
    return Status::ERROR_INVALID_ARG;
  }

  // Optimistic write: Try the write first, only poll if buffer is full.
  // This eliminates a syscall in the common case where buffer has space.
  ssize_t result = ::write(fd_, data, dataSize);
  if (COMPAT_UNLIKELY(result < 0)) {
    if (COMPAT_LIKELY(errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Buffer full - poll if timeout allows
      if (timeoutMs == 0) {
        ++stats_.writeWouldBlock;
        return Status::WOULD_BLOCK;
      }
      Status waitStatus = waitForReady(true, timeoutMs);
      if (waitStatus != Status::SUCCESS) {
        if (waitStatus == Status::WOULD_BLOCK) {
          ++stats_.writeWouldBlock;
        } else {
          ++stats_.writeErrors;
        }
        return waitStatus;
      }
      // Retry after poll indicates buffer ready
      result = ::write(fd_, data, dataSize);
      if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          ++stats_.writeWouldBlock;
          return Status::WOULD_BLOCK;
        }
        ++stats_.writeErrors;
        return Status::ERROR_IO;
      }
    } else {
      ++stats_.writeErrors;
      return Status::ERROR_IO;
    }
  }

  bytesWritten = static_cast<std::size_t>(result);
  stats_.bytesTx += bytesWritten;
  ++stats_.writesCompleted;
  invokeTrace(TraceDirection::TX, data, bytesWritten);
  return Status::SUCCESS;
}

Status UartAdapter::writeVectored(const struct iovec* iov, int iovcnt, std::size_t& bytesWritten,
                                  int timeoutMs) noexcept {
  bytesWritten = 0;

  if (COMPAT_UNLIKELY(!configured_)) {
    ++stats_.writeErrors;
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (COMPAT_UNLIKELY(iov == nullptr || iovcnt <= 0)) {
    ++stats_.writeErrors;
    return Status::ERROR_INVALID_ARG;
  }

  // Optimistic writev: Try first, poll on EAGAIN
  ssize_t result = ::writev(fd_, iov, iovcnt);
  if (COMPAT_UNLIKELY(result < 0)) {
    if (COMPAT_LIKELY(errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (timeoutMs == 0) {
        ++stats_.writeWouldBlock;
        return Status::WOULD_BLOCK;
      }
      Status waitStatus = waitForReady(true, timeoutMs);
      if (waitStatus != Status::SUCCESS) {
        if (waitStatus == Status::WOULD_BLOCK) {
          ++stats_.writeWouldBlock;
        } else {
          ++stats_.writeErrors;
        }
        return waitStatus;
      }
      result = ::writev(fd_, iov, iovcnt);
      if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          ++stats_.writeWouldBlock;
          return Status::WOULD_BLOCK;
        }
        ++stats_.writeErrors;
        return Status::ERROR_IO;
      }
    } else {
      ++stats_.writeErrors;
      return Status::ERROR_IO;
    }
  }

  bytesWritten = static_cast<std::size_t>(result);
  stats_.bytesTx += bytesWritten;
  ++stats_.writesCompleted;
  // Note: Vectored I/O tracing would require assembly of scattered buffers.
  // For detailed tracing, use the non-vectored write() API instead.
  return Status::SUCCESS;
}

Status UartAdapter::readVectored(struct iovec* iov, int iovcnt, std::size_t& bytesRead,
                                 int timeoutMs) noexcept {
  bytesRead = 0;

  if (COMPAT_UNLIKELY(!configured_)) {
    ++stats_.readErrors;
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (COMPAT_UNLIKELY(iov == nullptr || iovcnt <= 0)) {
    ++stats_.readErrors;
    return Status::ERROR_INVALID_ARG;
  }

  // Optimistic readv: Try first, poll on EAGAIN
  ssize_t result = ::readv(fd_, iov, iovcnt);
  if (COMPAT_UNLIKELY(result < 0)) {
    if (COMPAT_LIKELY(errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (timeoutMs == 0) {
        ++stats_.readWouldBlock;
        return Status::WOULD_BLOCK;
      }
      Status waitStatus = waitForReady(false, timeoutMs);
      if (waitStatus != Status::SUCCESS) {
        if (waitStatus == Status::WOULD_BLOCK) {
          ++stats_.readWouldBlock;
        } else {
          ++stats_.readErrors;
        }
        return waitStatus;
      }
      result = ::readv(fd_, iov, iovcnt);
      if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          ++stats_.readWouldBlock;
          return Status::WOULD_BLOCK;
        }
        ++stats_.readErrors;
        return Status::ERROR_IO;
      }
      if (result == 0) {
        ++stats_.readWouldBlock;
        return Status::WOULD_BLOCK;
      }
    } else {
      ++stats_.readErrors;
      return Status::ERROR_IO;
    }
  }

  if (result == 0) {
    // Check for EOF vs no data (same logic as read())
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int pollResult = ::poll(&pfd, 1, 0);
    if (pollResult > 0 && (pfd.revents & POLLHUP) != 0) {
      ++stats_.readErrors;
      return Status::ERROR_CLOSED;
    }
    if (timeoutMs == 0) {
      ++stats_.readWouldBlock;
      return Status::WOULD_BLOCK;
    }
    Status waitStatus = waitForReady(false, timeoutMs);
    if (waitStatus != Status::SUCCESS) {
      if (waitStatus == Status::WOULD_BLOCK) {
        ++stats_.readWouldBlock;
      } else {
        ++stats_.readErrors;
      }
      return waitStatus;
    }
    result = ::readv(fd_, iov, iovcnt);
    if (result <= 0) {
      if (result == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
        ++stats_.readWouldBlock;
        return Status::WOULD_BLOCK;
      }
      ++stats_.readErrors;
      return Status::ERROR_IO;
    }
  }

  bytesRead = static_cast<std::size_t>(result);
  stats_.bytesRx += bytesRead;
  ++stats_.readsCompleted;
  // Note: Vectored I/O tracing would require assembly of scattered buffers.
  // For detailed tracing, use the non-vectored read() API instead.
  return Status::SUCCESS;
}

/* ----------------------------- Span API ----------------------------- */

Status UartAdapter::read(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                         int timeoutMs) noexcept {
  return read(buffer.data(), buffer.size(), bytesRead, timeoutMs);
}

Status UartAdapter::write(apex::compat::bytes_span data, std::size_t& bytesWritten,
                          int timeoutMs) noexcept {
  return write(data.data(), data.size(), bytesWritten, timeoutMs);
}

Status UartAdapter::flush(bool flushRx, bool flushTx) noexcept {
  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  int queue = 0;
  if (flushRx && flushTx) {
    queue = TCIOFLUSH;
  } else if (flushRx) {
    queue = TCIFLUSH;
  } else if (flushTx) {
    queue = TCOFLUSH;
  } else {
    return Status::SUCCESS;
  }

  if (::tcflush(fd_, queue) != 0) {
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

Status UartAdapter::close() noexcept {
  Status result = Status::SUCCESS;

  if (fd_ >= 0) {
    if (::close(fd_) != 0) {
      result = Status::ERROR_IO;
    }
    fd_ = -1;
  }

  configured_ = false;
  return result;
}

bool UartAdapter::isOpen() const noexcept { return configured_; }

int UartAdapter::fd() const noexcept { return fd_; }

const UartStats& UartAdapter::stats() const noexcept { return stats_; }

void UartAdapter::resetStats() noexcept { stats_.reset(); }

const char* UartAdapter::devicePath() const noexcept { return devicePath_.c_str(); }

/* ----------------------------- Private Methods ----------------------------- */

Status UartAdapter::openDevice(bool exclusiveAccess) noexcept {
  fd_ = ::open(devicePath_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    if (errno == ENOENT || errno == ENODEV) {
      return Status::ERROR_CLOSED;
    }
    if (errno == EBUSY) {
      return Status::ERROR_BUSY;
    }
    return Status::ERROR_IO;
  }

  if (exclusiveAccess) {
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      ::close(fd_);
      fd_ = -1;
      return Status::ERROR_BUSY;
    }
  }

  return Status::SUCCESS;
}

Status UartAdapter::applyTermios(const UartConfig& config) noexcept {
  struct termios tty{};

  if (::tcgetattr(fd_, &tty) != 0) {
    return Status::ERROR_IO;
  }

  speed_t speed = baudToSpeed(config.baudRate);
  if (speed == B0) {
    return Status::ERROR_INVALID_ARG;
  }
  ::cfsetispeed(&tty, speed);
  ::cfsetospeed(&tty, speed);

  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= dataBitsToFlag(config.dataBits);

  switch (config.parity) {
  case Parity::ODD:
    tty.c_cflag |= PARENB | PARODD;
    break;
  case Parity::EVEN:
    tty.c_cflag |= PARENB;
    tty.c_cflag &= ~PARODD;
    break;
  case Parity::NONE:
  default:
    tty.c_cflag &= ~PARENB;
    break;
  }

  if (config.stopBits == StopBits::TWO) {
    tty.c_cflag |= CSTOPB;
  } else {
    tty.c_cflag &= ~CSTOPB;
  }

  switch (config.flowControl) {
  case FlowControl::HARDWARE:
    tty.c_cflag |= CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    break;
  case FlowControl::SOFTWARE:
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag |= IXON | IXOFF;
    break;
  case FlowControl::NONE:
  default:
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    break;
  }

  tty.c_cflag |= CREAD | CLOCAL;

  tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);

  tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

  tty.c_oflag &= ~(OPOST | ONLCR);

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;

  if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

Status UartAdapter::applyRs485(const UartConfig::Rs485Config& rs485) noexcept {
  struct serial_rs485 rs485conf{};

  rs485conf.flags = SER_RS485_ENABLED;
  if (rs485.rtsOnSend) {
    rs485conf.flags |= SER_RS485_RTS_ON_SEND;
  }
  if (rs485.rtsAfterSend) {
    rs485conf.flags |= SER_RS485_RTS_AFTER_SEND;
  }
  rs485conf.delay_rts_before_send = rs485.delayRtsBeforeSendUs;
  rs485conf.delay_rts_after_send = rs485.delayRtsAfterSendUs;

  if (::ioctl(fd_, TIOCSRS485, &rs485conf) != 0) {
    if (errno == ENOTTY || errno == EINVAL) {
      return Status::ERROR_UNSUPPORTED;
    }
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

Status UartAdapter::applyLowLatency(bool enable) noexcept {
  struct serial_struct ser_info{};

  if (::ioctl(fd_, TIOCGSERIAL, &ser_info) != 0) {
    if (errno == ENOTTY || errno == EINVAL) {
      return Status::ERROR_UNSUPPORTED;
    }
    return Status::ERROR_IO;
  }

  if (enable) {
    ser_info.flags |= ASYNC_LOW_LATENCY;
  } else {
    ser_info.flags &= ~ASYNC_LOW_LATENCY;
  }

  if (::ioctl(fd_, TIOCSSERIAL, &ser_info) != 0) {
    if (errno == ENOTTY || errno == EINVAL) {
      return Status::ERROR_UNSUPPORTED;
    }
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

Status UartAdapter::waitForReady(bool forWrite, int timeoutMs) noexcept {
  struct pollfd pfd{};
  pfd.fd = fd_;
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

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex
