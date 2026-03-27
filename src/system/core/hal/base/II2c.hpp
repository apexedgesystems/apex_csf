#ifndef APEX_HAL_II2C_HPP
#define APEX_HAL_II2C_HPP
/**
 * @file II2c.hpp
 * @brief Abstract I2C master interface for embedded systems.
 *
 * Platform-agnostic I2C interface designed for MCUs and embedded Linux.
 * Unlike the protocols/i2c/ library (which uses Linux i2c-dev file
 * descriptors), this interface has no OS dependencies and can be
 * implemented on bare-metal.
 *
 * Design principles:
 *  - No heap allocation
 *  - No POSIX dependencies
 *  - No internal buffers (user provides TX/RX buffers per transfer)
 *  - Blocking (polling) transfers -- deterministic, simple
 *  - Master mode only (slave mode not supported)
 *
 * Implementations:
 *  - Stm32I2c (STM32 I2C peripheral via HAL)
 */

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace hal {

/* ----------------------------- I2cStatus ----------------------------- */

/**
 * @brief Status codes for I2C operations.
 */
enum class I2cStatus : uint8_t {
  OK = 0,            ///< Operation succeeded.
  BUSY,              ///< Peripheral busy (transfer in progress).
  ERROR_TIMEOUT,     ///< Transfer timed out.
  ERROR_NACK,        ///< Slave did not acknowledge (no device or rejected).
  ERROR_BUS,         ///< Bus error (SDA/SCL stuck, misplaced START/STOP).
  ERROR_ARBITRATION, ///< Arbitration lost (multi-master conflict).
  ERROR_NOT_INIT,    ///< I2C not initialized.
  ERROR_INVALID_ARG  ///< Invalid argument (e.g., null pointer, zero length).
};

/**
 * @brief Convert I2cStatus to string.
 * @param s Status value.
 * @return Human-readable string.
 * @note RT-safe: Returns static string literal.
 */
inline const char* toString(I2cStatus s) noexcept {
  switch (s) {
  case I2cStatus::OK:
    return "OK";
  case I2cStatus::BUSY:
    return "BUSY";
  case I2cStatus::ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case I2cStatus::ERROR_NACK:
    return "ERROR_NACK";
  case I2cStatus::ERROR_BUS:
    return "ERROR_BUS";
  case I2cStatus::ERROR_ARBITRATION:
    return "ERROR_ARBITRATION";
  case I2cStatus::ERROR_NOT_INIT:
    return "ERROR_NOT_INIT";
  case I2cStatus::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- I2cSpeed ----------------------------- */

/**
 * @brief I2C bus speed modes.
 *
 * Standard I2C speed modes:
 *  - STANDARD:  100 kHz (all devices support this)
 *  - FAST:      400 kHz (most modern devices)
 *  - FAST_PLUS: 1 MHz   (requires Fast-mode Plus capable hardware)
 */
enum class I2cSpeed : uint8_t {
  STANDARD = 0, ///< 100 kHz (standard mode).
  FAST,         ///< 400 kHz (fast mode).
  FAST_PLUS     ///< 1 MHz (fast mode plus).
};

/* ----------------------------- I2cAddressMode ----------------------------- */

/**
 * @brief I2C slave address mode.
 */
enum class I2cAddressMode : uint8_t {
  SEVEN_BIT = 0, ///< 7-bit addressing (0x00-0x7F, most common).
  TEN_BIT        ///< 10-bit addressing (0x000-0x3FF, rare).
};

/* ----------------------------- I2cConfig ----------------------------- */

/**
 * @brief I2C configuration parameters.
 */
struct I2cConfig {
  I2cSpeed speed = I2cSpeed::STANDARD;                    ///< Bus speed mode.
  I2cAddressMode addressMode = I2cAddressMode::SEVEN_BIT; ///< Address mode.
};

/* ----------------------------- I2cStats ----------------------------- */

/**
 * @brief I2C statistics for monitoring.
 */
struct I2cStats {
  uint32_t bytesRx = 0;           ///< Total bytes received from slaves.
  uint32_t bytesTx = 0;           ///< Total bytes transmitted to slaves.
  uint32_t transferCount = 0;     ///< Number of completed transfers.
  uint32_t nackErrors = 0;        ///< NACK (no acknowledge) count.
  uint32_t busErrors = 0;         ///< Bus error count.
  uint32_t arbitrationErrors = 0; ///< Arbitration lost count.
  uint32_t timeoutErrors = 0;     ///< Timeout count.

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe.
   */
  void reset() noexcept {
    bytesRx = 0;
    bytesTx = 0;
    transferCount = 0;
    nackErrors = 0;
    busErrors = 0;
    arbitrationErrors = 0;
    timeoutErrors = 0;
  }

  /**
   * @brief Get total error count.
   * @return Sum of all error counters.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t totalErrors() const noexcept {
    return nackErrors + busErrors + arbitrationErrors + timeoutErrors;
  }
};

/* ----------------------------- II2c ----------------------------- */

/**
 * @class II2c
 * @brief Abstract I2C master interface for embedded systems.
 *
 * Provides a common API for I2C master communication across different MCU
 * platforms. All transfers are blocking (polling). The slave address is
 * specified per-transfer (no persistent address state).
 *
 * Lifecycle:
 *  1. Construct implementation (platform-specific)
 *  2. Call init() with configuration
 *  3. Call write()/read()/writeRead() for data exchange
 *  4. Call deinit() to release peripheral
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - For RTOS use, wrap with mutex or use from single task.
 *
 * RT-Safety:
 *  - init()/deinit(): NOT RT-safe (peripheral setup)
 *  - write()/read()/writeRead(): RT-safe after init (no allocation, bounded time)
 *  - isBusy(): RT-safe (register read)
 */
class II2c {
public:
  virtual ~II2c() = default;

  /**
   * @brief Initialize the I2C peripheral in master mode.
   * @param config Configuration parameters (speed, address mode).
   * @return OK on success, ERROR_* on failure.
   * @note NOT RT-safe: Configures peripheral registers.
   */
  [[nodiscard]] virtual I2cStatus init(const I2cConfig& config) noexcept = 0;

  /**
   * @brief Deinitialize the I2C peripheral.
   * @note NOT RT-safe: Releases peripheral.
   */
  virtual void deinit() noexcept = 0;

  /**
   * @brief Check if I2C is initialized and ready.
   * @return true if initialized.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

  /**
   * @brief Write data to a slave device (blocking).
   *
   * Sends a START condition, transmits the slave address (write mode),
   * sends @p len bytes from @p data, then sends a STOP condition.
   *
   * @param addr 7-bit or 10-bit slave address (NOT left-shifted).
   * @param data Data to transmit. Must not be null.
   * @param len Number of bytes to transmit. Must be > 0.
   * @return OK on success, ERROR_NOT_INIT if not initialized,
   *         ERROR_INVALID_ARG if null pointer or zero length,
   *         ERROR_NACK if slave does not acknowledge,
   *         ERROR_TIMEOUT if transfer timed out.
   * @note RT-safe: Bounded execution time (polling with timeout).
   */
  [[nodiscard]] virtual I2cStatus write(uint16_t addr, const uint8_t* data,
                                        size_t len) noexcept = 0;

  /**
   * @brief Read data from a slave device (blocking).
   *
   * Sends a START condition, transmits the slave address (read mode),
   * receives @p len bytes into @p data, then sends a STOP condition.
   *
   * @param addr 7-bit or 10-bit slave address (NOT left-shifted).
   * @param data Buffer for received data. Must not be null.
   * @param len Number of bytes to receive. Must be > 0.
   * @return OK on success, ERROR_NOT_INIT if not initialized,
   *         ERROR_INVALID_ARG if null pointer or zero length,
   *         ERROR_NACK if slave does not acknowledge,
   *         ERROR_TIMEOUT if transfer timed out.
   * @note RT-safe: Bounded execution time (polling with timeout).
   */
  [[nodiscard]] virtual I2cStatus read(uint16_t addr, uint8_t* data, size_t len) noexcept = 0;

  /**
   * @brief Combined write-then-read transaction (blocking).
   *
   * Performs a write followed by a repeated START and read, without
   * releasing the bus between the two phases. This is the standard
   * pattern for register access on I2C devices:
   *   START -> addr+W -> txData -> RESTART -> addr+R -> rxData -> STOP
   *
   * @param addr 7-bit or 10-bit slave address (NOT left-shifted).
   * @param txData Data to transmit (e.g., register address). Must not be null.
   * @param txLen Number of bytes to transmit. Must be > 0.
   * @param rxData Buffer for received data. Must not be null.
   * @param rxLen Number of bytes to receive. Must be > 0.
   * @return OK on success.
   * @note RT-safe: Bounded execution time (polling with timeout).
   */
  [[nodiscard]] virtual I2cStatus writeRead(uint16_t addr, const uint8_t* txData, size_t txLen,
                                            uint8_t* rxData, size_t rxLen) noexcept = 0;

  /**
   * @brief Check if I2C peripheral is currently busy.
   * @return true if a transfer is in progress.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isBusy() const noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe.
   */
  [[nodiscard]] virtual const I2cStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe.
   */
  virtual void resetStats() noexcept = 0;

protected:
  II2c() = default;
  II2c(const II2c&) = delete;
  II2c& operator=(const II2c&) = delete;
  II2c(II2c&&) = default;
  II2c& operator=(II2c&&) = default;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_II2C_HPP
