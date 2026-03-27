#ifndef APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_CONFIG_HPP
#define APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_CONFIG_HPP
/**
 * @file RfcommConfig.hpp
 * @brief Configuration types for Bluetooth RFCOMM connections.
 *
 * Provides BluetoothAddress for MAC address handling and RfcommConfig
 * for connection parameters.
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- BluetoothAddress ----------------------------- */

/**
 * @struct BluetoothAddress
 * @brief Bluetooth MAC address (6 bytes).
 *
 * Stored in network byte order (big-endian), same as bdaddr_t.
 * Format: "XX:XX:XX:XX:XX:XX" where XX is uppercase hex.
 */
struct BluetoothAddress {
  std::array<std::uint8_t, 6> bytes{};

  /**
   * @brief Parse address from string.
   * @param str Address string in format "XX:XX:XX:XX:XX:XX".
   * @return Parsed address, or all-zeros on parse failure.
   * @note NOT RT-safe: string parsing.
   */
  static BluetoothAddress fromString(const char* str) noexcept;

  /**
   * @brief Format address as string.
   * @param buf Output buffer (must be at least 18 bytes).
   * @param bufSize Size of output buffer.
   * @return Number of characters written (excluding null terminator).
   * @note RT-safe: No allocation, bounded time.
   */
  std::size_t toString(char* buf, std::size_t bufSize) const noexcept;

  /**
   * @brief Check if address is valid (non-zero).
   * @return true if any byte is non-zero.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] bool isValid() const noexcept;

  /**
   * @brief Check if this is the broadcast address (FF:FF:FF:FF:FF:FF).
   * @return true if broadcast.
   * @note RT-safe: O(1), no allocation.
   */
  [[nodiscard]] bool isBroadcast() const noexcept;

  /**
   * @brief Equality comparison.
   * @param other Address to compare.
   * @return true if equal.
   * @note RT-safe: O(1), no allocation.
   */
  bool operator==(const BluetoothAddress& other) const noexcept { return bytes == other.bytes; }

  /**
   * @brief Inequality comparison.
   * @param other Address to compare.
   * @return true if not equal.
   * @note RT-safe: O(1), no allocation.
   */
  bool operator!=(const BluetoothAddress& other) const noexcept { return bytes != other.bytes; }
};

/* ----------------------------- RfcommConfig ----------------------------- */

/**
 * @struct RfcommConfig
 * @brief Configuration for RFCOMM connection.
 *
 * Used by RfcommAdapter::configure() to establish connection parameters.
 */
struct RfcommConfig {
  BluetoothAddress remoteAddress{}; ///< Remote device address
  std::uint8_t channel{1};          ///< RFCOMM channel (1-30)
  int connectTimeoutMs{5000};       ///< Connection timeout (-1=block, 0=poll, >0=bounded)
  int readTimeoutMs{1000};          ///< Default read timeout
  int writeTimeoutMs{1000};         ///< Default write timeout

  /**
   * @brief Validate configuration.
   * @return true if configuration is valid.
   * @note RT-safe: O(1), no allocation.
   *
   * Validation rules:
   * - remoteAddress must be valid (non-zero)
   * - channel must be 1-30 (RFCOMM spec)
   */
  [[nodiscard]] bool isValid() const noexcept;
};

/* ----------------------------- Constants ----------------------------- */

/// Minimum RFCOMM channel number
inline constexpr std::uint8_t RFCOMM_CHANNEL_MIN = 1;

/// Maximum RFCOMM channel number
inline constexpr std::uint8_t RFCOMM_CHANNEL_MAX = 30;

/// Size of BluetoothAddress string representation (including null terminator)
inline constexpr std::size_t BLUETOOTH_ADDRESS_STRING_SIZE = 18;

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_WIRELESS_BLUETOOTH_RFCOMM_CONFIG_HPP
