/**
 * @file I2cSocketDevice.cpp
 * @brief I2cDevice implementation over a Unix socketpair.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cSocketDevice.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- I2cSocketDevice Methods ----------------------------- */

I2cSocketDevice::I2cSocketDevice(int socketFd, bool ownsFd) noexcept
    : fd_{socketFd}, ownsFd_{ownsFd} {}

I2cSocketDevice::~I2cSocketDevice() {
  if (fd_ >= 0 && ownsFd_) {
    ::close(fd_);
  }
}

I2cSocketDevice::I2cSocketDevice(I2cSocketDevice&& other) noexcept
    : fd_{other.fd_}, ownsFd_{other.ownsFd_}, configured_{other.configured_},
      slaveAddr_{other.slaveAddr_}, stats_{other.stats_} {
  other.fd_ = -1;
  other.ownsFd_ = false;
  other.configured_ = false;
}

I2cSocketDevice& I2cSocketDevice::operator=(I2cSocketDevice&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0 && ownsFd_) {
      ::close(fd_);
    }
    fd_ = other.fd_;
    ownsFd_ = other.ownsFd_;
    configured_ = other.configured_;
    slaveAddr_ = other.slaveAddr_;
    stats_ = other.stats_;
    other.fd_ = -1;
    other.ownsFd_ = false;
    other.configured_ = false;
  }
  return *this;
}

Status I2cSocketDevice::configure(const I2cConfig& /*config*/) noexcept {
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  configured_ = true;
  return Status::SUCCESS;
}

Status I2cSocketDevice::setSlaveAddress(std::uint16_t address) noexcept {
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  slaveAddr_ = address;
  return Status::SUCCESS;
}

Status I2cSocketDevice::read(std::uint8_t* buffer, std::size_t length, std::size_t& bytesRead,
                             int /*timeoutMs*/) noexcept {
  bytesRead = 0;
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (buffer == nullptr || length == 0) {
    return Status::ERROR_INVALID_ARG;
  }
  if (length > 0xFFFF) {
    return Status::ERROR_INVALID_ARG;
  }

  const auto STATUS =
      doTransaction(slaveAddr_, nullptr, 0, buffer, static_cast<std::uint16_t>(length), bytesRead);
  if (STATUS == Status::SUCCESS) {
    stats_.bytesRx += bytesRead;
    ++stats_.readsCompleted;
  } else if (STATUS == Status::ERROR_NACK) {
    ++stats_.nackCount;
    ++stats_.readErrors;
  } else {
    ++stats_.readErrors;
  }
  return STATUS;
}

Status I2cSocketDevice::write(const std::uint8_t* data, std::size_t length,
                              std::size_t& bytesWritten, int /*timeoutMs*/) noexcept {
  bytesWritten = 0;
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (data == nullptr || length == 0) {
    return Status::ERROR_INVALID_ARG;
  }
  if (length > 0xFFFF) {
    return Status::ERROR_INVALID_ARG;
  }

  std::size_t dummy = 0;
  const auto STATUS =
      doTransaction(slaveAddr_, data, static_cast<std::uint16_t>(length), nullptr, 0, dummy);
  if (STATUS == Status::SUCCESS) {
    bytesWritten = length;
    stats_.bytesTx += length;
    ++stats_.writesCompleted;
  } else if (STATUS == Status::ERROR_NACK) {
    ++stats_.nackCount;
    ++stats_.writeErrors;
  } else {
    ++stats_.writeErrors;
  }
  return STATUS;
}

Status I2cSocketDevice::writeRead(const std::uint8_t* writeData, std::size_t writeLength,
                                  std::uint8_t* readBuffer, std::size_t readLength,
                                  std::size_t& bytesRead, int /*timeoutMs*/) noexcept {
  bytesRead = 0;
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (writeData == nullptr || writeLength == 0) {
    return Status::ERROR_INVALID_ARG;
  }
  if (readBuffer == nullptr || readLength == 0) {
    return Status::ERROR_INVALID_ARG;
  }
  if (writeLength > 0xFFFF || readLength > 0xFFFF) {
    return Status::ERROR_INVALID_ARG;
  }

  const auto STATUS = doTransaction(slaveAddr_, writeData, static_cast<std::uint16_t>(writeLength),
                                    readBuffer, static_cast<std::uint16_t>(readLength), bytesRead);
  if (STATUS == Status::SUCCESS) {
    stats_.bytesTx += writeLength;
    stats_.bytesRx += bytesRead;
    ++stats_.writesCompleted;
    ++stats_.readsCompleted;
  } else if (STATUS == Status::ERROR_NACK) {
    ++stats_.nackCount;
    ++stats_.writeErrors;
  } else {
    ++stats_.writeErrors;
  }
  return STATUS;
}

Status I2cSocketDevice::close() noexcept {
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  if (ownsFd_) {
    ::close(fd_);
  }
  fd_ = -1;
  configured_ = false;
  return Status::SUCCESS;
}

bool I2cSocketDevice::isOpen() const noexcept { return fd_ >= 0 && configured_; }

int I2cSocketDevice::fd() const noexcept { return fd_; }

const I2cStats& I2cSocketDevice::stats() const noexcept { return stats_; }

void I2cSocketDevice::resetStats() noexcept { stats_.reset(); }

const char* I2cSocketDevice::devicePath() const noexcept { return "socketpair"; }

std::uint16_t I2cSocketDevice::slaveAddress() const noexcept { return slaveAddr_; }

/* ----------------------------- File Helpers ----------------------------- */

Status I2cSocketDevice::doTransaction(std::uint16_t addr, const std::uint8_t* wData,
                                      std::uint16_t wLen, std::uint8_t* rBuf, std::uint16_t rLen,
                                      std::size_t& bytesRead) noexcept {
  bytesRead = 0;
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }

  // Send header: [addr:2 LE][wLen:2 LE][rLen:2 LE]
  struct {
    std::uint16_t addr;
    std::uint16_t wLen;
    std::uint16_t rLen;
  } __attribute__((packed)) header{addr, wLen, rLen};

  if (!sendAll(&header, sizeof(header))) {
    return Status::ERROR_IO;
  }

  // Send write data (if any)
  if (wLen > 0 && wData != nullptr) {
    if (!sendAll(wData, wLen)) {
      return Status::ERROR_IO;
    }
  }

  // Receive response: [status:1][rData:rLen]
  std::uint8_t statusByte = 0;
  if (!recvAll(&statusByte, 1)) {
    return Status::ERROR_IO;
  }

  if (statusByte != 0) {
    return Status::ERROR_NACK;
  }

  // Read response data (if any)
  if (rLen > 0 && rBuf != nullptr) {
    if (!recvAll(rBuf, rLen)) {
      return Status::ERROR_IO;
    }
    bytesRead = rLen;
  }

  return Status::SUCCESS;
}

bool I2cSocketDevice::sendAll(const void* data, std::size_t len) noexcept {
  const auto* ptr = static_cast<const std::uint8_t*>(data);
  std::size_t sent = 0;
  while (sent < len) {
    auto n = ::write(fd_, ptr + sent, len - sent);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) {
        continue;
      }
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

bool I2cSocketDevice::recvAll(void* data, std::size_t len) noexcept {
  auto* ptr = static_cast<std::uint8_t*>(data);
  std::size_t received = 0;
  while (received < len) {
    auto n = ::read(fd_, ptr + received, len - received);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) {
        continue;
      }
      return false;
    }
    received += static_cast<std::size_t>(n);
  }
  return true;
}

} // namespace i2c
} // namespace protocols
} // namespace apex
