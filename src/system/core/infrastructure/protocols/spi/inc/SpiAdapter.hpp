#ifndef APEX_PROTOCOLS_SPI_ADAPTER_HPP
#define APEX_PROTOCOLS_SPI_ADAPTER_HPP
/**
 * @file SpiAdapter.hpp
 * @brief Linux SPI device adapter using spidev interface.
 *
 * Provides a complete implementation of SpiDevice for Linux SPI controllers
 * accessed via the spidev user-space driver (/dev/spidevX.Y).
 *
 * Features:
 *  - Full spidev ioctl configuration (mode, speed, bits per word)
 *  - Full-duplex and half-duplex transfers
 *  - Multi-transfer batching via SPI_IOC_MESSAGE
 *  - Statistics tracking
 *  - Optional byte-level tracing via ByteTrace mixin
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/spi/inc/SpiDevice.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <string>

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- SpiAdapter ----------------------------- */

/**
 * @class SpiAdapter
 * @brief Linux SPI device implementation using spidev.
 *
 * Lifecycle:
 *  1. Construct with device path (does not open yet)
 *  2. Call configure() to open and set up the device
 *  3. Call transfer()/read()/write() for I/O
 *  4. Call close() or let destructor clean up
 *
 * @note NOT thread-safe: External synchronization required for concurrent access.
 */
class SpiAdapter : public SpiDevice, public apex::protocols::ByteTrace {
public:
  /**
   * @brief Construct adapter for a device path.
   * @param devicePath Path to SPI device (e.g., "/dev/spidev0.0").
   * @note Does not open the device; call configure() to open.
   */
  explicit SpiAdapter(const std::string& devicePath);

  /**
   * @brief Construct adapter for a device path (move variant).
   * @param devicePath Path to SPI device.
   */
  explicit SpiAdapter(std::string&& devicePath);

  /**
   * @brief Construct adapter by bus and chip-select numbers.
   * @param bus SPI bus number (X in spidevX.Y).
   * @param chipSelect Chip select number (Y in spidevX.Y).
   */
  SpiAdapter(std::uint32_t bus, std::uint32_t chipSelect);

  ~SpiAdapter() override;

  SpiAdapter(const SpiAdapter&) = delete;
  SpiAdapter& operator=(const SpiAdapter&) = delete;
  SpiAdapter(SpiAdapter&& other) noexcept;
  SpiAdapter& operator=(SpiAdapter&& other) noexcept;

  /* ----------------------------- SpiDevice Interface ----------------------------- */

  [[nodiscard]] Status configure(const SpiConfig& config) noexcept override;

  [[nodiscard]] Status transfer(const std::uint8_t* txData, std::uint8_t* rxData,
                                std::size_t length, int timeoutMs) noexcept override;

  [[nodiscard]] Status close() noexcept override;

  [[nodiscard]] bool isOpen() const noexcept override;

  [[nodiscard]] int fd() const noexcept override;

  [[nodiscard]] const SpiStats& stats() const noexcept override;

  void resetStats() noexcept override;

  [[nodiscard]] const char* devicePath() const noexcept override;

  [[nodiscard]] const SpiConfig& config() const noexcept override;

  /* ----------------------------- Span API ----------------------------- */

  /**
   * @brief Perform full-duplex transfer using spans.
   * @param txData Data to transmit.
   * @param rxData Buffer for received data (must be same size as txData).
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if transfer complete, ERROR_* on failure.
   * @note RT-safe: Inline wrapper, no additional overhead.
   */
  [[nodiscard]] Status transfer(apex::compat::bytes_span txData,
                                apex::compat::mutable_bytes_span rxData, int timeoutMs) noexcept;

  /**
   * @brief Transmit data using span (receive data discarded).
   * @param data Data to transmit.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if transfer complete, ERROR_* on failure.
   * @note RT-safe: Inline wrapper.
   */
  [[nodiscard]] Status write(apex::compat::bytes_span data, int timeoutMs) noexcept;

  /**
   * @brief Receive data using span (zeros transmitted).
   * @param buffer Buffer for received data.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if transfer complete, ERROR_* on failure.
   * @note RT-safe: Inline wrapper.
   */
  [[nodiscard]] Status read(apex::compat::mutable_bytes_span buffer, int timeoutMs) noexcept;

  /* ----------------------------- Batch Transfer ----------------------------- */

  /**
   * @brief Transfer descriptor for batch operations.
   *
   * Describes a single transfer within a batch. Multiple transfers
   * can be executed atomically without deasserting chip select.
   */
  struct TransferDesc {
    const std::uint8_t* txBuf = nullptr; ///< Transmit buffer (nullptr = zeros).
    std::uint8_t* rxBuf = nullptr;       ///< Receive buffer (nullptr = discard).
    std::size_t length = 0;              ///< Transfer length in bytes.
    bool csChange = false;               ///< Deassert CS after this transfer.
    std::uint16_t delayUsecs = 0;        ///< Delay after transfer (microseconds).
  };

  /**
   * @brief Execute multiple transfers atomically.
   * @param transfers Array of transfer descriptors.
   * @param count Number of transfers.
   * @param timeoutMs Timeout in milliseconds.
   * @return SUCCESS if all transfers complete, ERROR_* on failure.
   * @note RT-safe when device is configured.
   * @note Chip select remains asserted between transfers unless csChange is set.
   */
  [[nodiscard]] Status transferBatch(const TransferDesc* transfers, std::size_t count,
                                     int timeoutMs) noexcept;

private:
  std::string devicePath_;
  int fd_ = -1;
  bool configured_ = false;
  SpiConfig config_;
  SpiStats stats_;

  Status openDevice() noexcept;
  Status applyConfig() noexcept;
};

} // namespace spi
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SPI_ADAPTER_HPP
