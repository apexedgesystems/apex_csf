/**
 * @file SpiSocketDevice.cpp
 * @brief SpiDevice implementation over a Unix socketpair.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiSocketDevice.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- Constants ----------------------------- */

/// Zero buffer for transmit-only transfers (send zeros on MOSI).
static constexpr std::size_t ZERO_BUF_SIZE = 256;
static const std::uint8_t ZERO_BUF[ZERO_BUF_SIZE] = {};

/* ----------------------------- SpiSocketDevice Methods ----------------------------- */

SpiSocketDevice::SpiSocketDevice(int socketFd, bool ownsFd) noexcept
    : fd_{socketFd}, ownsFd_{ownsFd} {}

SpiSocketDevice::~SpiSocketDevice() {
  if (fd_ >= 0 && ownsFd_) {
    ::close(fd_);
  }
}

SpiSocketDevice::SpiSocketDevice(SpiSocketDevice&& other) noexcept
    : fd_{other.fd_}, ownsFd_{other.ownsFd_}, configured_{other.configured_},
      config_{other.config_}, stats_{other.stats_} {
  other.fd_ = -1;
  other.ownsFd_ = false;
  other.configured_ = false;
}

SpiSocketDevice& SpiSocketDevice::operator=(SpiSocketDevice&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0 && ownsFd_) {
      ::close(fd_);
    }
    fd_ = other.fd_;
    ownsFd_ = other.ownsFd_;
    configured_ = other.configured_;
    config_ = other.config_;
    stats_ = other.stats_;
    other.fd_ = -1;
    other.ownsFd_ = false;
    other.configured_ = false;
  }
  return *this;
}

Status SpiSocketDevice::configure(const SpiConfig& config) noexcept {
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  config_ = config;
  configured_ = true;
  return Status::SUCCESS;
}

Status SpiSocketDevice::transfer(const std::uint8_t* txData, std::uint8_t* rxData,
                                 std::size_t length, int /*timeoutMs*/) noexcept {
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }
  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (length == 0) {
    return Status::SUCCESS;
  }

  // Send request header: [length:4 LE]
  std::uint32_t lenLE = length;
  if (!sendAll(&lenLE, sizeof(lenLE))) {
    ++stats_.transferErrors;
    return Status::ERROR_IO;
  }

  // Send TX data (or zeros if txData is null)
  if (txData != nullptr) {
    if (!sendAll(txData, length)) {
      ++stats_.transferErrors;
      return Status::ERROR_IO;
    }
  } else {
    // Send zeros in chunks
    std::size_t remaining = length;
    while (remaining > 0) {
      const std::size_t CHUNK = std::min(remaining, ZERO_BUF_SIZE);
      if (!sendAll(ZERO_BUF, CHUNK)) {
        ++stats_.transferErrors;
        return Status::ERROR_IO;
      }
      remaining -= CHUNK;
    }
  }

  stats_.bytesTx += length;

  // Receive response: [rxData:length]
  if (rxData != nullptr) {
    if (!recvAll(rxData, length)) {
      ++stats_.transferErrors;
      return Status::ERROR_IO;
    }
  } else {
    // Discard received data in chunks
    std::uint8_t discard[ZERO_BUF_SIZE];
    std::size_t remaining = length;
    while (remaining > 0) {
      const std::size_t CHUNK = std::min(remaining, ZERO_BUF_SIZE);
      if (!recvAll(discard, CHUNK)) {
        ++stats_.transferErrors;
        return Status::ERROR_IO;
      }
      remaining -= CHUNK;
    }
  }

  stats_.bytesRx += length;
  ++stats_.transfersCompleted;
  return Status::SUCCESS;
}

Status SpiSocketDevice::close() noexcept {
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

bool SpiSocketDevice::isOpen() const noexcept { return fd_ >= 0 && configured_; }

int SpiSocketDevice::fd() const noexcept { return fd_; }

const SpiStats& SpiSocketDevice::stats() const noexcept { return stats_; }

void SpiSocketDevice::resetStats() noexcept { stats_.reset(); }

const char* SpiSocketDevice::devicePath() const noexcept { return "socketpair"; }

const SpiConfig& SpiSocketDevice::config() const noexcept { return config_; }

/* ----------------------------- File Helpers ----------------------------- */

bool SpiSocketDevice::sendAll(const void* data, std::size_t len) noexcept {
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

bool SpiSocketDevice::recvAll(void* data, std::size_t len) noexcept {
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

} // namespace spi
} // namespace protocols
} // namespace apex
