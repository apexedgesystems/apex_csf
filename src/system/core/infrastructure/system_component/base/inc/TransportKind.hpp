#ifndef APEX_SYSTEM_CORE_BASE_TRANSPORT_KIND_HPP
#define APEX_SYSTEM_CORE_BASE_TRANSPORT_KIND_HPP
/**
 * @file TransportKind.hpp
 * @brief Transport type classification for HW_MODEL/DRIVER pairing.
 *
 * Part of the base interface layer - no heavy dependencies.
 * Used by HwModelBase to declare the transport an emulated hardware
 * model requires. The executive uses this to create the appropriate
 * virtual link (PTY, socketpair, vcan, loopback socket) between the
 * HW_MODEL and its matching DRIVER.
 *
 * All functions are RT-safe: O(1), no allocation, noexcept.
 */

#include <stdint.h>

namespace system_core {
namespace system_component {

/* ----------------------------- TransportKind ----------------------------- */

/**
 * @enum TransportKind
 * @brief Transport type for HW_MODEL/DRIVER virtual link creation.
 *
 * When an HW_MODEL declares a TransportKind, the executive creates the
 * appropriate virtual link during startup:
 *
 *   - SERIAL_*: PtyPair (master fd to HW_MODEL, slave path to DRIVER)
 *   - CAN: vcan kernel interface (both sides use CANBusAdapter)
 *   - SPI: Unix socketpair with SpiSocketDevice wrapper
 *   - I2C: Unix socketpair with I2cSocketDevice wrapper
 *   - ETH_TCP: Loopback TCP (server to HW_MODEL, client to DRIVER)
 *   - ETH_UDP: Loopback UDP (server to HW_MODEL, client to DRIVER)
 *   - UNIX_STREAM: socketpair (one fd per side)
 *   - BLUETOOTH: Unix socketpair with RFCOMM-compatible wrapper
 *
 * NONE indicates no transport requirement (default).
 */
enum class TransportKind : uint8_t {
  NONE = 0,        ///< No transport (default).
  SERIAL_232 = 1,  ///< RS-232 (UART, standard termios).
  SERIAL_422 = 2,  ///< RS-422 (differential, point-to-point).
  SERIAL_485 = 3,  ///< RS-485 (half-duplex, multi-drop).
  CAN = 4,         ///< CAN bus (SocketCAN).
  SPI = 5,         ///< SPI bus (full-duplex, master/slave).
  I2C = 6,         ///< I2C bus (half-duplex, addressed).
  ETH_TCP = 7,     ///< TCP socket.
  ETH_UDP = 8,     ///< UDP socket.
  UNIX_STREAM = 9, ///< Unix domain socket (stream).
  BLUETOOTH = 10,  ///< Bluetooth RFCOMM.
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Human-readable string for TransportKind.
 * @param kind Transport kind value.
 * @return Static string (no allocation).
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(TransportKind kind) noexcept {
  switch (kind) {
  case TransportKind::NONE:
    return "NONE";
  case TransportKind::SERIAL_232:
    return "SERIAL_232";
  case TransportKind::SERIAL_422:
    return "SERIAL_422";
  case TransportKind::SERIAL_485:
    return "SERIAL_485";
  case TransportKind::CAN:
    return "CAN";
  case TransportKind::SPI:
    return "SPI";
  case TransportKind::I2C:
    return "I2C";
  case TransportKind::ETH_TCP:
    return "ETH_TCP";
  case TransportKind::ETH_UDP:
    return "ETH_UDP";
  case TransportKind::UNIX_STREAM:
    return "UNIX_STREAM";
  case TransportKind::BLUETOOTH:
    return "BLUETOOTH";
  }
  return "UNKNOWN";
}

/**
 * @brief Check if transport kind is a serial variant.
 * @param kind Transport kind value.
 * @return true if SERIAL_232, SERIAL_422, or SERIAL_485.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isSerial(TransportKind kind) noexcept {
  return kind == TransportKind::SERIAL_232 || kind == TransportKind::SERIAL_422 ||
         kind == TransportKind::SERIAL_485;
}

/**
 * @brief Check if transport kind is a network variant.
 * @param kind Transport kind value.
 * @return true if ETH_TCP, ETH_UDP, or UNIX_STREAM.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isNetwork(TransportKind kind) noexcept {
  return kind == TransportKind::ETH_TCP || kind == TransportKind::ETH_UDP ||
         kind == TransportKind::UNIX_STREAM;
}

/**
 * @brief Check if transport uses a Unix socketpair for emulation.
 * @param kind Transport kind value.
 * @return true if SPI, I2C, UNIX_STREAM, or BLUETOOTH.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool usesSocketpair(TransportKind kind) noexcept {
  return kind == TransportKind::SPI || kind == TransportKind::I2C ||
         kind == TransportKind::UNIX_STREAM || kind == TransportKind::BLUETOOTH;
}

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_BASE_TRANSPORT_KIND_HPP
