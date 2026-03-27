#ifndef APEX_HAL_ISPI_HPP
#define APEX_HAL_ISPI_HPP
/**
 * @file ISpi.hpp
 * @brief Abstract SPI master interface for embedded systems.
 *
 * Platform-agnostic SPI interface designed for MCUs and embedded Linux.
 * Covers master-mode SPI with full-duplex transfer, TX-only, and RX-only
 * convenience methods. All transfers are blocking (polling).
 *
 * Design principles:
 *  - No heap allocation
 *  - No POSIX dependencies
 *  - No internal buffers (user provides TX/RX buffers per transfer)
 *  - Blocking (polling) transfers -- deterministic, simple
 *  - Software chip select (CS managed via GPIO, not hardware NSS)
 *
 * Implementations:
 *  - Stm32Spi (STM32 SPI peripheral via HAL)
 */

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace hal {

/* ----------------------------- SpiStatus ----------------------------- */

/**
 * @brief Status codes for SPI operations.
 */
enum class SpiStatus : uint8_t {
  OK = 0,           ///< Operation succeeded.
  BUSY,             ///< Peripheral busy (transfer in progress).
  ERROR_TIMEOUT,    ///< Transfer timed out.
  ERROR_OVERRUN,    ///< RX overrun (data lost).
  ERROR_CRC,        ///< CRC mismatch.
  ERROR_MODF,       ///< Mode fault (multi-master conflict).
  ERROR_NOT_INIT,   ///< SPI not initialized.
  ERROR_INVALID_ARG ///< Invalid argument (e.g., null pointer, zero length).
};

/**
 * @brief Convert SpiStatus to string.
 * @param s Status value.
 * @return Human-readable string.
 * @note RT-safe: Returns static string literal.
 */
inline const char* toString(SpiStatus s) noexcept {
  switch (s) {
  case SpiStatus::OK:
    return "OK";
  case SpiStatus::BUSY:
    return "BUSY";
  case SpiStatus::ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case SpiStatus::ERROR_OVERRUN:
    return "ERROR_OVERRUN";
  case SpiStatus::ERROR_CRC:
    return "ERROR_CRC";
  case SpiStatus::ERROR_MODF:
    return "ERROR_MODF";
  case SpiStatus::ERROR_NOT_INIT:
    return "ERROR_NOT_INIT";
  case SpiStatus::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- SpiMode ----------------------------- */

/**
 * @brief SPI clock polarity and phase mode.
 *
 * Standard SPI modes defined by CPOL (clock polarity) and CPHA (clock phase):
 *  - MODE_0: CPOL=0 CPHA=0 (idle low, sample on rising edge)
 *  - MODE_1: CPOL=0 CPHA=1 (idle low, sample on falling edge)
 *  - MODE_2: CPOL=1 CPHA=0 (idle high, sample on falling edge)
 *  - MODE_3: CPOL=1 CPHA=1 (idle high, sample on rising edge)
 */
enum class SpiMode : uint8_t {
  MODE_0 = 0, ///< CPOL=0 CPHA=0.
  MODE_1,     ///< CPOL=0 CPHA=1.
  MODE_2,     ///< CPOL=1 CPHA=0.
  MODE_3      ///< CPOL=1 CPHA=1.
};

/* ----------------------------- SpiBitOrder ----------------------------- */

/**
 * @brief SPI bit transmission order.
 */
enum class SpiBitOrder : uint8_t {
  MSB_FIRST = 0, ///< Most significant bit transmitted first (default).
  LSB_FIRST      ///< Least significant bit transmitted first.
};

/* ----------------------------- SpiDataSize ----------------------------- */

/**
 * @brief SPI data frame size.
 */
enum class SpiDataSize : uint8_t {
  BITS_8 = 0, ///< 8-bit data frames (default).
  BITS_16     ///< 16-bit data frames.
};

/* ----------------------------- SpiConfig ----------------------------- */

/**
 * @brief SPI configuration parameters.
 */
struct SpiConfig {
  uint32_t maxClockHz = 1000000;                 ///< Maximum SCK frequency in Hz.
  SpiMode mode = SpiMode::MODE_0;                ///< Clock polarity/phase mode.
  SpiBitOrder bitOrder = SpiBitOrder::MSB_FIRST; ///< Bit transmission order.
  SpiDataSize dataSize = SpiDataSize::BITS_8;    ///< Data frame size.
};

/* ----------------------------- SpiStats ----------------------------- */

/**
 * @brief SPI statistics for monitoring.
 */
struct SpiStats {
  uint32_t bytesTransferred = 0; ///< Total bytes transferred (TX + RX).
  uint32_t transferCount = 0;    ///< Number of completed transfers.
  uint32_t crcErrors = 0;        ///< CRC mismatch count.
  uint32_t modfErrors = 0;       ///< Mode fault count.
  uint32_t overrunErrors = 0;    ///< RX overrun count.

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe.
   */
  void reset() noexcept {
    bytesTransferred = 0;
    transferCount = 0;
    crcErrors = 0;
    modfErrors = 0;
    overrunErrors = 0;
  }

  /**
   * @brief Get total error count.
   * @return Sum of all error counters.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t totalErrors() const noexcept {
    return crcErrors + modfErrors + overrunErrors;
  }
};

/* ----------------------------- ISpi ----------------------------- */

/**
 * @class ISpi
 * @brief Abstract SPI master interface for embedded systems.
 *
 * Provides a common API for SPI master communication across different MCU
 * platforms. All transfers are blocking (polling). The implementation manages
 * chip select (CS) assertion internally via GPIO.
 *
 * Lifecycle:
 *  1. Construct implementation (platform-specific)
 *  2. Call init() with configuration
 *  3. Call transfer()/write()/read() for data exchange
 *  4. Call deinit() to release peripheral
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - For RTOS use, wrap with mutex or use from single task.
 *
 * RT-Safety:
 *  - init()/deinit(): NOT RT-safe (peripheral setup)
 *  - transfer()/write()/read(): RT-safe after init (no allocation, bounded time)
 *  - isBusy(): RT-safe (register read)
 */
class ISpi {
public:
  virtual ~ISpi() = default;

  /**
   * @brief Initialize the SPI peripheral in master mode.
   * @param config Configuration parameters (clock, mode, bit order, data size).
   * @return OK on success, ERROR_* on failure.
   * @note NOT RT-safe: Configures peripheral registers.
   */
  [[nodiscard]] virtual SpiStatus init(const SpiConfig& config) noexcept = 0;

  /**
   * @brief Deinitialize the SPI peripheral.
   * @note NOT RT-safe: Releases peripheral.
   */
  virtual void deinit() noexcept = 0;

  /**
   * @brief Check if SPI is initialized and ready.
   * @return true if initialized.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

  /**
   * @brief Full-duplex SPI transfer (blocking).
   *
   * Simultaneously transmits @p txData and receives into @p rxData.
   * Both buffers must be at least @p len bytes. CS is asserted for the
   * duration of the transfer and deasserted on completion.
   *
   * @param txData Data to transmit (len bytes). Must not be null.
   * @param rxData Buffer for received data (len bytes). Must not be null.
   * @param len Number of bytes to transfer. Must be > 0.
   * @return OK on success, ERROR_NOT_INIT if not initialized,
   *         ERROR_INVALID_ARG if null pointers or zero length,
   *         ERROR_TIMEOUT if transfer timed out.
   * @note RT-safe: Bounded execution time (polling with timeout).
   */
  [[nodiscard]] virtual SpiStatus transfer(const uint8_t* txData, uint8_t* rxData,
                                           size_t len) noexcept = 0;

  /**
   * @brief Transmit-only SPI transfer (blocking).
   *
   * Transmits @p data, discards received bytes. CS is asserted for the
   * duration of the transfer.
   *
   * @param data Data to transmit (len bytes). Must not be null.
   * @param len Number of bytes to transmit. Must be > 0.
   * @return OK on success.
   * @note RT-safe: Bounded execution time (polling with timeout).
   */
  [[nodiscard]] virtual SpiStatus write(const uint8_t* data, size_t len) noexcept = 0;

  /**
   * @brief Receive-only SPI transfer (blocking).
   *
   * Sends 0xFF dummy bytes while capturing received data into @p data.
   * CS is asserted for the duration of the transfer.
   *
   * @param data Buffer for received data (len bytes). Must not be null.
   * @param len Number of bytes to receive. Must be > 0.
   * @return OK on success.
   * @note RT-safe: Bounded execution time (polling with timeout).
   */
  [[nodiscard]] virtual SpiStatus read(uint8_t* data, size_t len) noexcept = 0;

  /**
   * @brief Check if SPI peripheral is currently busy.
   * @return true if a transfer is in progress.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isBusy() const noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe.
   */
  [[nodiscard]] virtual const SpiStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe.
   */
  virtual void resetStats() noexcept = 0;

protected:
  ISpi() = default;
  ISpi(const ISpi&) = delete;
  ISpi& operator=(const ISpi&) = delete;
  ISpi(ISpi&&) = default;
  ISpi& operator=(ISpi&&) = default;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_ISPI_HPP
