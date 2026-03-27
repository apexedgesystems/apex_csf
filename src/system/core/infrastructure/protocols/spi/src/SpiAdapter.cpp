/**
 * @file SpiAdapter.cpp
 * @brief Linux SPI device adapter implementation using spidev.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiAdapter.hpp"

#include <asm-generic/ioctl.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

using apex::protocols::TraceDirection;

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- Constants ----------------------------- */

/// Maximum transfers per batch (Linux spidev limit is typically 4096 bytes total).
inline constexpr std::size_t MAX_BATCH_TRANSFERS = 32;

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/**
 * @brief Build device path from bus and chip-select numbers.
 * @param bus SPI bus number.
 * @param chipSelect Chip select number.
 * @return Device path string.
 */
std::string buildDevicePath(std::uint32_t bus, std::uint32_t chipSelect) {
  // Format: /dev/spidevX.Y
  char path[32];
  std::snprintf(path, sizeof(path), "/dev/spidev%u.%u", bus, chipSelect);
  return std::string(path);
}

/**
 * @brief Convert SpiConfig to spidev mode bits.
 * @param config SPI configuration.
 * @return Linux spidev mode value.
 */
std::uint32_t configToMode(const SpiConfig& config) noexcept {
  std::uint32_t mode = static_cast<std::uint32_t>(config.mode);

  if (config.bitOrder == BitOrder::LSB_FIRST) {
    mode |= SPI_LSB_FIRST;
  }
  if (config.csHigh) {
    mode |= SPI_CS_HIGH;
  }
  if (config.threeWire) {
    mode |= SPI_3WIRE;
  }
  if (config.loopback) {
    mode |= SPI_LOOP;
  }
  if (config.noCs) {
    mode |= SPI_NO_CS;
  }
  if (config.ready) {
    mode |= SPI_READY;
  }

  return mode;
}

} // namespace

/* ----------------------------- SpiAdapter ----------------------------- */

SpiAdapter::SpiAdapter(const std::string& devicePath) : devicePath_(devicePath) {}

SpiAdapter::SpiAdapter(std::string&& devicePath) : devicePath_(std::move(devicePath)) {}

SpiAdapter::SpiAdapter(std::uint32_t bus, std::uint32_t chipSelect)
    : devicePath_(buildDevicePath(bus, chipSelect)) {}

SpiAdapter::~SpiAdapter() {
  if (fd_ >= 0) {
    static_cast<void>(close());
  }
}

SpiAdapter::SpiAdapter(SpiAdapter&& other) noexcept
    : devicePath_(std::move(other.devicePath_)), fd_(other.fd_), configured_(other.configured_),
      config_(other.config_), stats_(other.stats_) {
  other.fd_ = -1;
  other.configured_ = false;
}

SpiAdapter& SpiAdapter::operator=(SpiAdapter&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      static_cast<void>(close());
    }
    devicePath_ = std::move(other.devicePath_);
    fd_ = other.fd_;
    configured_ = other.configured_;
    config_ = other.config_;
    stats_ = other.stats_;
    other.fd_ = -1;
    other.configured_ = false;
  }
  return *this;
}

/* ----------------------------- SpiDevice Interface ----------------------------- */

Status SpiAdapter::configure(const SpiConfig& config) noexcept {
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

  // Apply configuration
  st = applyConfig();
  if (st != Status::SUCCESS) {
    ::close(fd_);
    fd_ = -1;
    return st;
  }

  configured_ = true;
  return Status::SUCCESS;
}

Status SpiAdapter::transfer(const std::uint8_t* txData, std::uint8_t* rxData, std::size_t length,
                            int timeoutMs) noexcept {
  (void)timeoutMs; // spidev transfers are blocking; timeout not directly supported

  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (length == 0) {
    return Status::SUCCESS;
  }

  // Build transfer structure
  struct spi_ioc_transfer xfer{};
  xfer.tx_buf = reinterpret_cast<std::uint64_t>(txData);
  xfer.rx_buf = reinterpret_cast<std::uint64_t>(rxData);
  xfer.len = static_cast<std::uint32_t>(length);
  xfer.speed_hz = config_.maxSpeedHz;
  xfer.bits_per_word = config_.bitsPerWord;
  xfer.delay_usecs = 0;
  xfer.cs_change = 0;

  // Perform transfer
  int ret = ::ioctl(fd_, SPI_IOC_MESSAGE(1), &xfer);
  if (ret < 0) {
    ++stats_.transferErrors;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ++stats_.transferWouldBlock;
      return Status::WOULD_BLOCK;
    }
    if (errno == ETIMEDOUT) {
      return Status::ERROR_TIMEOUT;
    }
    return Status::ERROR_IO;
  }

  // Update statistics
  ++stats_.transfersCompleted;
  if (txData != nullptr) {
    stats_.bytesTx += length;
    invokeTrace(TraceDirection::TX, txData, length);
  }
  if (rxData != nullptr) {
    stats_.bytesRx += length;
    invokeTrace(TraceDirection::RX, rxData, length);
  }

  return Status::SUCCESS;
}

Status SpiAdapter::close() noexcept {
  if (fd_ < 0) {
    return Status::ERROR_CLOSED;
  }

  int ret = ::close(fd_);
  fd_ = -1;
  configured_ = false;

  return (ret == 0) ? Status::SUCCESS : Status::ERROR_IO;
}

bool SpiAdapter::isOpen() const noexcept { return fd_ >= 0 && configured_; }

int SpiAdapter::fd() const noexcept { return fd_; }

const SpiStats& SpiAdapter::stats() const noexcept { return stats_; }

void SpiAdapter::resetStats() noexcept { stats_.reset(); }

const char* SpiAdapter::devicePath() const noexcept { return devicePath_.c_str(); }

const SpiConfig& SpiAdapter::config() const noexcept { return config_; }

/* ----------------------------- Span API ----------------------------- */

Status SpiAdapter::transfer(apex::compat::bytes_span txData,
                            apex::compat::mutable_bytes_span rxData, int timeoutMs) noexcept {
  if (txData.size() != rxData.size()) {
    return Status::ERROR_INVALID_ARG;
  }
  return transfer(txData.data(), rxData.data(), txData.size(), timeoutMs);
}

Status SpiAdapter::write(apex::compat::bytes_span data, int timeoutMs) noexcept {
  return transfer(data.data(), nullptr, data.size(), timeoutMs);
}

Status SpiAdapter::read(apex::compat::mutable_bytes_span buffer, int timeoutMs) noexcept {
  return transfer(nullptr, buffer.data(), buffer.size(), timeoutMs);
}

/* ----------------------------- Batch Transfer ----------------------------- */

Status SpiAdapter::transferBatch(const TransferDesc* transfers, std::size_t count,
                                 int timeoutMs) noexcept {
  (void)timeoutMs;

  if (!configured_) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (count == 0) {
    return Status::SUCCESS;
  }

  if (count > MAX_BATCH_TRANSFERS) {
    return Status::ERROR_INVALID_ARG;
  }

  // Build transfer array
  struct spi_ioc_transfer xfers[MAX_BATCH_TRANSFERS];
  std::memset(xfers, 0, sizeof(xfers));

  std::size_t totalTx = 0;
  std::size_t totalRx = 0;

  for (std::size_t i = 0; i < count; ++i) {
    xfers[i].tx_buf = reinterpret_cast<std::uint64_t>(transfers[i].txBuf);
    xfers[i].rx_buf = reinterpret_cast<std::uint64_t>(transfers[i].rxBuf);
    xfers[i].len = static_cast<std::uint32_t>(transfers[i].length);
    xfers[i].speed_hz = config_.maxSpeedHz;
    xfers[i].bits_per_word = config_.bitsPerWord;
    xfers[i].delay_usecs = transfers[i].delayUsecs;
    xfers[i].cs_change = transfers[i].csChange ? 1 : 0;

    if (transfers[i].txBuf != nullptr) {
      totalTx += transfers[i].length;
    }
    if (transfers[i].rxBuf != nullptr) {
      totalRx += transfers[i].length;
    }
  }

  // Perform batch transfer
  // Note: SPI_IOC_MESSAGE requires a compile-time constant for the size parameter.
  // We use the _IOC macro directly with the actual byte count.
  unsigned long request = _IOC(_IOC_WRITE, SPI_IOC_MAGIC, 0,
                               static_cast<unsigned int>(count * sizeof(struct spi_ioc_transfer)));
  int ret = ::ioctl(fd_, request, xfers);
  if (ret < 0) {
    ++stats_.transferErrors;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ++stats_.transferWouldBlock;
      return Status::WOULD_BLOCK;
    }
    if (errno == ETIMEDOUT) {
      return Status::ERROR_TIMEOUT;
    }
    return Status::ERROR_IO;
  }

  // Update statistics
  stats_.transfersCompleted += count;
  stats_.bytesTx += totalTx;
  stats_.bytesRx += totalRx;

  // Trace if enabled
  for (std::size_t i = 0; i < count; ++i) {
    if (transfers[i].txBuf != nullptr) {
      invokeTrace(TraceDirection::TX, transfers[i].txBuf, transfers[i].length);
    }
    if (transfers[i].rxBuf != nullptr) {
      invokeTrace(TraceDirection::RX, transfers[i].rxBuf, transfers[i].length);
    }
  }

  return Status::SUCCESS;
}

/* ----------------------------- Private Methods ----------------------------- */

Status SpiAdapter::openDevice() noexcept {
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

Status SpiAdapter::applyConfig() noexcept {
  // Set mode (includes CPOL, CPHA, and other flags)
  std::uint32_t mode = configToMode(config_);
  if (::ioctl(fd_, SPI_IOC_WR_MODE32, &mode) < 0) {
    return Status::ERROR_IO;
  }

  // Set bits per word
  std::uint8_t bits = config_.bitsPerWord;
  if (::ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
    return Status::ERROR_IO;
  }

  // Set max speed
  std::uint32_t speed = config_.maxSpeedHz;
  if (::ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
    return Status::ERROR_IO;
  }

  // Read back actual values (driver may adjust)
  if (::ioctl(fd_, SPI_IOC_RD_MODE32, &mode) < 0) {
    return Status::ERROR_IO;
  }
  if (::ioctl(fd_, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0) {
    return Status::ERROR_IO;
  }
  if (::ioctl(fd_, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0) {
    return Status::ERROR_IO;
  }

  // Update config with actual values
  config_.bitsPerWord = bits;
  config_.maxSpeedHz = speed;

  return Status::SUCCESS;
}

} // namespace spi
} // namespace protocols
} // namespace apex
