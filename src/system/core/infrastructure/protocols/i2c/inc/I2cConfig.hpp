#ifndef APEX_PROTOCOLS_I2C_CONFIG_HPP
#define APEX_PROTOCOLS_I2C_CONFIG_HPP
/**
 * @file I2cConfig.hpp
 * @brief I2C device configuration types.
 *
 * Provides configuration for I2C master devices using the Linux i2c-dev
 * interface.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- Enumerations ----------------------------- */

/**
 * @enum AddressMode
 * @brief I2C addressing mode.
 */
enum class AddressMode : std::uint8_t {
  SEVEN_BIT = 0, ///< Standard 7-bit addressing (0x00-0x7F).
  TEN_BIT = 1    ///< Extended 10-bit addressing (0x000-0x3FF).
};

/* ----------------------------- I2cConfig ----------------------------- */

/**
 * @struct I2cConfig
 * @brief Complete I2C device configuration.
 *
 * Configuration for an I2C bus master. Default values represent a
 * standard configuration: 7-bit addressing, no PEC.
 *
 * @note RT-safe: Simple POD struct, no allocation.
 */
struct I2cConfig {
  AddressMode addressMode = AddressMode::SEVEN_BIT; ///< Addressing mode.

  /**
   * @brief Enable Packet Error Checking (SMBus PEC).
   *
   * When enabled, an 8-bit CRC is appended to each transaction
   * for error detection. Requires hardware/driver support.
   */
  bool enablePec = false;

  /**
   * @brief Force access even if device is already bound to a kernel driver.
   *
   * Use with caution - may conflict with kernel drivers.
   */
  bool forceAccess = false;

  /**
   * @brief Retry count for failed transactions.
   *
   * Number of times to retry a transaction on failure.
   * Set to 0 for no retries.
   */
  std::uint8_t retryCount = 0;
};

/* ----------------------------- toString Functions ----------------------------- */

/**
 * @brief Convert AddressMode to human-readable string.
 * @param mode Address mode to convert.
 * @return String literal (e.g., "SEVEN_BIT").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(AddressMode mode) noexcept;

} // namespace i2c
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_I2C_CONFIG_HPP
