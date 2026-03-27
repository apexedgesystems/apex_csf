#ifndef APEX_PROTOCOLS_SPI_CONFIG_HPP
#define APEX_PROTOCOLS_SPI_CONFIG_HPP
/**
 * @file SpiConfig.hpp
 * @brief SPI device configuration types for mode, speed, and bit settings.
 *
 * Provides strongly-typed enumerations for all standard SPI configuration
 * parameters. These are used by SpiAdapter::configure() to set up the
 * SPI device via spidev ioctls.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- Enumerations ----------------------------- */

/**
 * @enum SpiMode
 * @brief SPI clock polarity (CPOL) and phase (CPHA) modes.
 *
 * Mode | CPOL | CPHA | Clock Idle | Data Capture
 * -----|------|------|------------|-------------
 *  0   |  0   |  0   | Low        | Rising edge
 *  1   |  0   |  1   | Low        | Falling edge
 *  2   |  1   |  0   | High       | Falling edge
 *  3   |  1   |  1   | High       | Rising edge
 */
enum class SpiMode : std::uint8_t {
  MODE_0 = 0, ///< CPOL=0, CPHA=0 (most common)
  MODE_1 = 1, ///< CPOL=0, CPHA=1
  MODE_2 = 2, ///< CPOL=1, CPHA=0
  MODE_3 = 3  ///< CPOL=1, CPHA=1
};

/**
 * @enum BitOrder
 * @brief Bit transmission order.
 */
enum class BitOrder : std::uint8_t {
  MSB_FIRST = 0, ///< Most significant bit first (standard).
  LSB_FIRST = 1  ///< Least significant bit first.
};

/* ----------------------------- SpiConfig ----------------------------- */

/**
 * @struct SpiConfig
 * @brief Complete SPI device configuration.
 *
 * Aggregates all parameters needed to configure an SPI device. Default
 * values represent a common configuration: Mode 0, MSB first, 8 bits/word,
 * 1 MHz clock.
 *
 * @note RT-safe: Simple POD struct, no allocation.
 */
struct SpiConfig {
  SpiMode mode = SpiMode::MODE_0;          ///< Clock polarity and phase.
  BitOrder bitOrder = BitOrder::MSB_FIRST; ///< Bit transmission order.
  std::uint8_t bitsPerWord = 8;            ///< Bits per word (typically 8).
  std::uint32_t maxSpeedHz = 1000000;      ///< Maximum clock speed in Hz.

  /**
   * @brief Chip select behavior.
   *
   * When csHigh is true, chip select is active-high (inverted).
   * Default is active-low (standard).
   */
  bool csHigh = false;

  /**
   * @brief Three-wire mode (bidirectional).
   *
   * When enabled, MOSI and MISO share a single data line.
   * Requires hardware support.
   */
  bool threeWire = false;

  /**
   * @brief Loopback mode for testing.
   *
   * When enabled, transmitted data is looped back to receive.
   * Useful for testing without external hardware.
   */
  bool loopback = false;

  /**
   * @brief No chip select mode.
   *
   * When enabled, chip select is not asserted during transfers.
   * Useful for daisy-chained devices or manual CS control.
   */
  bool noCs = false;

  /**
   * @brief Slave-ready mode.
   *
   * When enabled, waits for slave ready signal before transfer.
   * Requires hardware support.
   */
  bool ready = false;

  /**
   * @brief Get CPOL (clock polarity) from mode.
   * @return true if clock idles high.
   */
  [[nodiscard]] bool cpol() const noexcept { return static_cast<std::uint8_t>(mode) & 0x02; }

  /**
   * @brief Get CPHA (clock phase) from mode.
   * @return true if data captured on trailing edge.
   */
  [[nodiscard]] bool cpha() const noexcept { return static_cast<std::uint8_t>(mode) & 0x01; }

  /**
   * @brief Get speed in MHz for display.
   * @return Speed in MHz.
   */
  [[nodiscard]] double speedMHz() const noexcept {
    return static_cast<double>(maxSpeedHz) / 1000000.0;
  }
};

/* ----------------------------- toString Functions ----------------------------- */

/**
 * @brief Convert SpiMode to human-readable string.
 * @param mode SPI mode to convert.
 * @return String literal (e.g., "MODE_0").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(SpiMode mode) noexcept;

/**
 * @brief Convert BitOrder to human-readable string.
 * @param order Bit order to convert.
 * @return String literal (e.g., "MSB_FIRST").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(BitOrder order) noexcept;

} // namespace spi
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SPI_CONFIG_HPP
