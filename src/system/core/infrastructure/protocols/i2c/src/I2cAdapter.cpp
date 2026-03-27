/**
 * @file I2cAdapter.cpp
 * @brief Linux I2C device adapter implementation using i2c-dev.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cAdapter.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

using apex::protocols::TraceDirection;

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/**
 * @brief Build device path from bus number.
 * @param busNumber I2C bus number.
 * @return Device path string.
 */
std::string buildDevicePath(std::uint32_t busNumber) {
  char path[32];
  std::snprintf(path, sizeof(path), "/dev/i2c-%u", busNumber);
  return std::string(path);
}

} // namespace

/* ----------------------------- I2cAdapter ----------------------------- */

I2cAdapter::I2cAdapter(const std::string& devicePath) : devicePath_(devicePath) {}

I2cAdapter::I2cAdapter(std::string&& devicePath) : devicePath_(std::move(devicePath)) {}

I2cAdapter::I2cAdapter(std::uint32_t busNumber) : devicePath_(buildDevicePath(busNumber)) {}

I2cAdapter::~I2cAdapter() {
  if (fd_ >= 0) {
    static_cast<void>(close());
  }
}

I2cAdapter::I2cAdapter(I2cAdapter&& other) noexcept
    : devicePath_(std::move(other.devicePath_)), fd_(other.fd_), configured_(other.configured_),
      slaveAddress_(other.slaveAddress_), slaveAddressSet_(other.slaveAddressSet_),
      config_(other.config_), stats_(other.stats_) {
  other.fd_ = -1;
  other.configured_ = false;
  other.slaveAddressSet_ = false;
}

I2cAdapter& I2cAdapter::operator=(I2cAdapter&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      static_cast<void>(close());
    }
    devicePath_ = std::move(other.devicePath_);
    fd_ = other.fd_;
    configured_ = other.configured_;
    slaveAddress_ = other.slaveAddress_;
    slaveAddressSet_ = other.slaveAddressSet_;
    config_ = other.config_;
    stats_ = other.stats_;
    other.fd_ = -1;
    other.configured_ = false;
    other.slaveAddressSet_ = false;
  }
  return *this;
}

/* ----------------------------- I2cDevice Interface ----------------------------- */

Status I2cAdapter::configure(const I2cConfig& config) noexcept {
  // Close if already open
  if (fd_ >= 0) {
    static_cast<void>(close());
  }

  config_ = config;

  // Open device
  Status st = openDevice();
  if (st != Status::SUCCESS) {
    return st;
  }

  // Enable PEC if requested
  if (config_.enablePec) {
    if (::ioctl(fd_, I2C_PEC, 1) < 0) {
      // PEC not supported, but don't fail - just continue without it
    }
  }

  configured_ = true;
  slaveAddressSet_ = false;
  return Status::SUCCESS;
}

Status I2cAdapter::setSlaveAddress(std::uint16_t address) noexcept {
  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  // Validate address range
  if (config_.addressMode == AddressMode::SEVEN_BIT) {
    if (address > 0x7F) {
      return Status::ERROR_INVALID_ARG;
    }
  } else {
    if (address > 0x3FF) {
      return Status::ERROR_INVALID_ARG;
    }
  }

  slaveAddress_ = address;

  // Apply address to device
  Status st = applySlaveAddress();
  if (st != Status::SUCCESS) {
    return st;
  }

  slaveAddressSet_ = true;
  return Status::SUCCESS;
}

Status I2cAdapter::read(std::uint8_t* buffer, std::size_t length, std::size_t& bytesRead,
                        int timeoutMs) noexcept {
  (void)timeoutMs; // i2c-dev reads are blocking

  bytesRead = 0;

  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (!slaveAddressSet_) {
    return Status::ERROR_INVALID_ARG;
  }

  if (buffer == nullptr || length == 0) {
    return Status::ERROR_INVALID_ARG;
  }

  ssize_t ret = ::read(fd_, buffer, length);
  if (ret < 0) {
    ++stats_.readErrors;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ++stats_.readWouldBlock;
      return Status::WOULD_BLOCK;
    }
    if (errno == ETIMEDOUT) {
      return Status::ERROR_TIMEOUT;
    }
    if (errno == EREMOTEIO || errno == EIO) {
      ++stats_.nackCount;
      return Status::ERROR_NACK;
    }
    return Status::ERROR_IO;
  }

  bytesRead = static_cast<std::size_t>(ret);
  stats_.bytesRx += bytesRead;
  ++stats_.readsCompleted;
  invokeTrace(TraceDirection::RX, buffer, bytesRead);

  return Status::SUCCESS;
}

Status I2cAdapter::write(const std::uint8_t* data, std::size_t length, std::size_t& bytesWritten,
                         int timeoutMs) noexcept {
  (void)timeoutMs;

  bytesWritten = 0;

  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (!slaveAddressSet_) {
    return Status::ERROR_INVALID_ARG;
  }

  if (data == nullptr || length == 0) {
    return Status::ERROR_INVALID_ARG;
  }

  ssize_t ret = ::write(fd_, data, length);
  if (ret < 0) {
    ++stats_.writeErrors;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ++stats_.writeWouldBlock;
      return Status::WOULD_BLOCK;
    }
    if (errno == ETIMEDOUT) {
      return Status::ERROR_TIMEOUT;
    }
    if (errno == EREMOTEIO || errno == EIO) {
      ++stats_.nackCount;
      return Status::ERROR_NACK;
    }
    return Status::ERROR_IO;
  }

  bytesWritten = static_cast<std::size_t>(ret);
  stats_.bytesTx += bytesWritten;
  ++stats_.writesCompleted;
  invokeTrace(TraceDirection::TX, data, bytesWritten);

  return Status::SUCCESS;
}

Status I2cAdapter::writeRead(const std::uint8_t* writeData, std::size_t writeLength,
                             std::uint8_t* readBuffer, std::size_t readLength,
                             std::size_t& bytesRead, int timeoutMs) noexcept {
  (void)timeoutMs;

  bytesRead = 0;

  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (!slaveAddressSet_) {
    return Status::ERROR_INVALID_ARG;
  }

  if (writeData == nullptr || writeLength == 0 || readBuffer == nullptr || readLength == 0) {
    return Status::ERROR_INVALID_ARG;
  }

  // Use I2C_RDWR for combined write-then-read
  struct i2c_msg msgs[2];
  struct i2c_rdwr_ioctl_data rdwr;

  // Write message
  msgs[0].addr = slaveAddress_;
  msgs[0].flags = (config_.addressMode == AddressMode::TEN_BIT) ? I2C_M_TEN : 0;
  msgs[0].len = static_cast<std::uint16_t>(writeLength);
  msgs[0].buf = const_cast<std::uint8_t*>(writeData);

  // Read message (with repeated start)
  msgs[1].addr = slaveAddress_;
  msgs[1].flags = I2C_M_RD;
  if (config_.addressMode == AddressMode::TEN_BIT) {
    msgs[1].flags |= I2C_M_TEN;
  }
  msgs[1].len = static_cast<std::uint16_t>(readLength);
  msgs[1].buf = readBuffer;

  rdwr.msgs = msgs;
  rdwr.nmsgs = 2;

  int ret = ::ioctl(fd_, I2C_RDWR, &rdwr);
  if (ret < 0) {
    ++stats_.readErrors;
    ++stats_.writeErrors;
    if (errno == ETIMEDOUT) {
      return Status::ERROR_TIMEOUT;
    }
    if (errno == EREMOTEIO || errno == EIO) {
      ++stats_.nackCount;
      return Status::ERROR_NACK;
    }
    return Status::ERROR_IO;
  }

  // Update statistics
  stats_.bytesTx += writeLength;
  stats_.bytesRx += readLength;
  ++stats_.writesCompleted;
  ++stats_.readsCompleted;
  bytesRead = readLength;

  invokeTrace(TraceDirection::TX, writeData, writeLength);
  invokeTrace(TraceDirection::RX, readBuffer, readLength);

  return Status::SUCCESS;
}

Status I2cAdapter::close() noexcept {
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }

  int ret = ::close(fd_);
  fd_ = -1;
  configured_ = false;
  slaveAddressSet_ = false;

  return (ret == 0) ? Status::SUCCESS : Status::ERROR_IO;
}

bool I2cAdapter::isOpen() const noexcept { return fd_ >= 0 && configured_; }

int I2cAdapter::fd() const noexcept { return fd_; }

const I2cStats& I2cAdapter::stats() const noexcept { return stats_; }

void I2cAdapter::resetStats() noexcept { stats_.reset(); }

const char* I2cAdapter::devicePath() const noexcept { return devicePath_.c_str(); }

std::uint16_t I2cAdapter::slaveAddress() const noexcept { return slaveAddress_; }

/* ----------------------------- Span API ----------------------------- */

Status I2cAdapter::read(apex::compat::mutable_bytes_span buffer, std::size_t& bytesRead,
                        int timeoutMs) noexcept {
  return read(buffer.data(), buffer.size(), bytesRead, timeoutMs);
}

Status I2cAdapter::write(apex::compat::bytes_span data, std::size_t& bytesWritten,
                         int timeoutMs) noexcept {
  return write(data.data(), data.size(), bytesWritten, timeoutMs);
}

/* ----------------------------- Register Access ----------------------------- */

Status I2cAdapter::readRegister(std::uint8_t regAddr, std::uint8_t& value, int timeoutMs) noexcept {
  std::size_t bytesRead = 0;
  return writeRead(&regAddr, 1, &value, 1, bytesRead, timeoutMs);
}

Status I2cAdapter::writeRegister(std::uint8_t regAddr, std::uint8_t value, int timeoutMs) noexcept {
  std::uint8_t buf[2] = {regAddr, value};
  std::size_t bytesWritten = 0;
  return write(buf, 2, bytesWritten, timeoutMs);
}

Status I2cAdapter::readRegisters(std::uint8_t regAddr, std::uint8_t* buffer, std::size_t length,
                                 std::size_t& bytesRead, int timeoutMs) noexcept {
  return writeRead(&regAddr, 1, buffer, length, bytesRead, timeoutMs);
}

/* ----------------------------- Bus Probing ----------------------------- */

bool I2cAdapter::probeDevice() noexcept {
  if (!configured_ || !slaveAddressSet_) {
    return false;
  }

  // Try a quick read - device should ACK its address
  std::uint8_t dummy = 0;
  ssize_t ret = ::read(fd_, &dummy, 0);

  // If read returns 0 or succeeds, device is present
  // If it fails with EREMOTEIO/EIO, device NACKed
  if (ret < 0 && (errno == EREMOTEIO || errno == EIO)) {
    return false;
  }

  return true;
}

/* ----------------------------- Private Methods ----------------------------- */

Status I2cAdapter::openDevice() noexcept {
  fd_ = ::open(devicePath_.c_str(), O_RDWR);
  if (fd_ < 0) {
    if (errno == ENOENT || errno == ENXIO) {
      return Status::ERROR_CLOSED;
    }
    if (errno == EACCES || errno == EPERM) {
      return Status::ERROR_BUSY;
    }
    return Status::ERROR_IO;
  }
  return Status::SUCCESS;
}

Status I2cAdapter::applySlaveAddress() noexcept {
  unsigned long req = I2C_SLAVE;
  if (config_.forceAccess) {
    req = I2C_SLAVE_FORCE;
  }

  // For 10-bit addressing, set I2C_TENBIT first
  if (config_.addressMode == AddressMode::TEN_BIT) {
    if (::ioctl(fd_, I2C_TENBIT, 1) < 0) {
      return Status::ERROR_UNSUPPORTED;
    }
  } else {
    // Ensure 10-bit mode is off
    static_cast<void>(::ioctl(fd_, I2C_TENBIT, 0));
  }

  if (::ioctl(fd_, req, slaveAddress_) < 0) {
    if (errno == EBUSY) {
      return Status::ERROR_BUSY;
    }
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

} // namespace i2c
} // namespace protocols
} // namespace apex
